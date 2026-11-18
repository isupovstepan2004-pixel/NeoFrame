#include <Windows.h>
#include <unknwn.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <windows.graphics.capture.interop.h>
#include <dwmapi.h>
#include <atomic>
#include <algorithm>
#include <format>
#include <mutex>

#include "Capture/CaptureSession.h"
#include "Core/Logger.h"

using Microsoft::WRL::ComPtr;
namespace wgc = winrt::Windows::Graphics::Capture;
namespace wdx = winrt::Windows::Graphics::DirectX;
namespace wd3d = winrt::Windows::Graphics::DirectX::Direct3D11;

// Provided by d3d11.lib; declared here to avoid pulling the SDK interop header.
extern "C" HRESULT __stdcall CreateDirect3D11DeviceFromDXGIDevice(::IDXGIDevice* dxgiDevice, ::IInspectable** graphicsDevice);

struct __declspec(uuid("A9B3D012-3828-4036-A5BF-6C66EEA1B4C0")) IDirect3DDxgiInterfaceAccess : ::IUnknown {
    virtual HRESULT __stdcall GetInterface(GUID const& id, void** object) = 0;
};

namespace nf {

// ---------------------------------------------------------------- enumeration
std::vector<MonitorInfo> EnumerateMonitors() {
    std::vector<MonitorInfo> list;
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR m, HDC, LPRECT, LPARAM lp) -> BOOL {
        auto* out = reinterpret_cast<std::vector<MonitorInfo>*>(lp);
        MONITORINFOEXW mi{};
        mi.cbSize = sizeof(mi);
        if (GetMonitorInfoW(m, &mi)) {
            MonitorInfo info{ m, mi.rcMonitor, L"", (mi.dwFlags & MONITORINFOF_PRIMARY) != 0 };
            out->push_back(info);
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&list));
    int n = 1;
    for (auto& m : list) {
        m.name = std::format(L"Монитор {} — {}×{}{}", n++, m.rect.right - m.rect.left,
                             m.rect.bottom - m.rect.top, m.primary ? L" (основной)" : L"");
    }
    return list;
}

namespace {
struct EnumCtx { std::vector<WindowInfo>* out; DWORD pid; };
}

std::vector<WindowInfo> EnumerateWindows(DWORD excludePid) {
    std::vector<WindowInfo> list;
    EnumCtx ctx{ &list, excludePid };
    EnumWindows([](HWND h, LPARAM lp) -> BOOL {
        auto* c = reinterpret_cast<EnumCtx*>(lp);
        if (!IsWindowVisible(h) || IsIconic(h) || GetWindow(h, GW_OWNER) != nullptr) return TRUE;
        if (GetWindowLongW(h, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return TRUE;
        DWORD pid = 0; GetWindowThreadProcessId(h, &pid);
        if (pid == c->pid) return TRUE;
        BOOL cloaked = FALSE;
        DwmGetWindowAttribute(h, 14 /*DWMWA_CLOAKED*/, &cloaked, sizeof(cloaked));
        if (cloaked) return TRUE;
        RECT r{}; GetWindowRect(h, &r);
        if (r.right - r.left < 160 || r.bottom - r.top < 100) return TRUE;
        wchar_t title[256]{};
        if (GetWindowTextW(h, title, 256) <= 0) return TRUE;
        if (wcscmp(title, L"Program Manager") == 0) return TRUE;
        c->out->push_back({ h, title });
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));
    return list;
}

// -------------------------------------------------------------------- session
struct CaptureSession::Impl {
    Gpu* gpu = nullptr;
    wgc::GraphicsCaptureItem item{ nullptr };
    wgc::Direct3D11CaptureFramePool pool{ nullptr };
    wgc::GraphicsCaptureSession session{ nullptr };
    wd3d::IDirect3DDevice device{ nullptr };
    winrt::event_token frameToken{}, closedToken{};
    winrt::Windows::Graphics::SizeInt32 poolSize{};
    bool active = false;
    std::atomic<bool> stopping{ false };

    mutable std::mutex mtx;
    ComPtr<ID3D11Texture2D> latest;
    uint32_t w = 0, h = 0;
    RECT crop{};
    bool useCrop = false;

    std::atomic<uint64_t> seq{ 0 };
    std::atomic<int64_t> ts{ 0 };
    HANDLE ev = nullptr;
    std::atomic<bool> closed{ false };
    std::atomic<double> fps{ 0 };
    std::atomic<int64_t> lastFrameQpc{ 0 };
    uint32_t cnt = 0;
    int64_t lastFpsQpc = 0;
    int64_t freq = 1;

    Impl() {
        ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        LARGE_INTEGER f; QueryPerformanceFrequency(&f); freq = f.QuadPart;
    }
    ~Impl() { if (ev) CloseHandle(ev); }

    bool Begin(Gpu& g, wgc::GraphicsCaptureItem const& it, bool cursor, std::wstring& err) {
        gpu = &g;
        try {
            if (!wgc::GraphicsCaptureSession::IsSupported()) {
                err = L"Windows Graphics Capture не поддерживается (нужна Windows 10 версии 1903 или новее).";
                return false;
            }
            ComPtr<IDXGIDevice> dxgi;
            g.Device()->QueryInterface(IID_PPV_ARGS(dxgi.GetAddressOf()));
            winrt::com_ptr<::IInspectable> insp;
            winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgi.Get(), insp.put()));
            device = insp.as<wd3d::IDirect3DDevice>();

            item = it;
            closed = false; stopping = false; seq = 0;
            poolSize = item.Size();
            pool = wgc::Direct3D11CaptureFramePool::CreateFreeThreaded(
                device, wdx::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, poolSize);
            frameToken = pool.FrameArrived([this](wgc::Direct3D11CaptureFramePool const& s,
                                                  winrt::Windows::Foundation::IInspectable const&) { OnFrame(s); });
            closedToken = item.Closed([this](wgc::GraphicsCaptureItem const&,
                                             winrt::Windows::Foundation::IInspectable const&) {
                closed = true;
                SetEvent(ev);
            });
            session = pool.CreateCaptureSession(item);
            try { session.IsCursorCaptureEnabled(cursor); } catch (...) {}
            try { session.IsBorderRequired(false); } catch (...) {}   // Windows 11 only
            session.StartCapture();
            active = true;
            return true;
        } catch (winrt::hresult_error const& e) {
            err = std::format(L"Не удалось начать захват: {} (0x{:08X})", std::wstring(e.message()),
                              static_cast<unsigned>(e.code().value));
            Logger::Error("Capture start failed");
            Teardown();
            return false;
        }
    }

    void Teardown() {
        stopping = true;
        try {
            if (pool) pool.FrameArrived(frameToken);
            if (item) item.Closed(closedToken);
            if (session) session.Close();
            if (pool) pool.Close();
        } catch (...) {}
        session = nullptr; pool = nullptr; item = nullptr; device = nullptr;
        active = false;
        std::lock_guard lk(mtx);
        latest.Reset();
        w = h = 0;
    }

    void OnFrame(wgc::Direct3D11CaptureFramePool const& sender) {
        if (stopping) return;
        try {
            auto frame = sender.TryGetNextFrame();
            if (!frame) return;
            auto size = frame.ContentSize();
            auto access = frame.Surface().as<IDirect3DDxgiInterfaceAccess>();
            ComPtr<ID3D11Texture2D> tex;
            if (FAILED(access->GetInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(tex.GetAddressOf()))))
                return;

            D3D11_BOX box{ 0, 0, 0, static_cast<UINT>(size.Width), static_cast<UINT>(size.Height), 1 };
            if (useCrop) {
                box.left = static_cast<UINT>(std::max<LONG>(0, crop.left));
                box.top = static_cast<UINT>(std::max<LONG>(0, crop.top));
                box.right = static_cast<UINT>(std::min<LONG>(size.Width, crop.right));
                box.bottom = static_cast<UINT>(std::min<LONG>(size.Height, crop.bottom));
            }
            if (box.right <= box.left + 16 || box.bottom <= box.top + 16) return;
            const uint32_t ow = box.right - box.left, oh = box.bottom - box.top;

            {
                std::lock_guard lk(mtx);
                if (!latest || w != ow || h != oh) {
                    D3D11_TEXTURE2D_DESC d{};
                    d.Width = ow; d.Height = oh; d.MipLevels = 1; d.ArraySize = 1;
                    d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
                    d.SampleDesc.Count = 1;
                    d.Usage = D3D11_USAGE_DEFAULT;
                    d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                    ComPtr<ID3D11Texture2D> nt;
                    if (FAILED(gpu->Device()->CreateTexture2D(&d, nullptr, nt.GetAddressOf()))) return;
                    latest = nt; w = ow; h = oh;
                }
                gpu->Context()->CopySubresourceRegion(latest.Get(), 0, 0, 0, 0, tex.Get(), 0, &box);
            }
            LARGE_INTEGER now; QueryPerformanceCounter(&now);
            ts = now.QuadPart;
            lastFrameQpc = now.QuadPart;
            ++seq;
            SetEvent(ev);

            ++cnt;
            if (lastFpsQpc == 0) lastFpsQpc = now.QuadPart;
            if (now.QuadPart - lastFpsQpc >= freq / 2) {
                fps = static_cast<double>(cnt) * freq / static_cast<double>(now.QuadPart - lastFpsQpc);
                cnt = 0; lastFpsQpc = now.QuadPart;
            }
            // Follow window resizes: recreate the pool at the new content size.
            if (size.Width != poolSize.Width || size.Height != poolSize.Height) {
                poolSize = size;
                sender.Recreate(device, wdx::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, size);
            }
        } catch (...) {
            // Capture target may be closing; the Closed event reports it.
        }
    }
};

CaptureSession::CaptureSession() : p_(std::make_unique<Impl>()) {}
CaptureSession::~CaptureSession() { Stop(); }

bool CaptureSession::StartMonitor(Gpu& gpu, HMONITOR mon, const RECT* crop, bool cursor, std::wstring& err) {
    Stop();
    try {
        auto interop = winrt::get_activation_factory<wgc::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
        wgc::GraphicsCaptureItem item{ nullptr };
        winrt::check_hresult(interop->CreateForMonitor(mon, winrt::guid_of<wgc::IGraphicsCaptureItem>(),
                                                       winrt::put_abi(item)));
        p_->useCrop = crop != nullptr;
        if (crop) p_->crop = *crop;
        return p_->Begin(gpu, item, cursor, err);
    } catch (winrt::hresult_error const& e) {
        err = std::format(L"Не удалось открыть монитор для захвата (0x{:08X})", static_cast<unsigned>(e.code().value));
        return false;
    }
}

bool CaptureSession::StartWindow(Gpu& gpu, HWND wnd, bool cursor, std::wstring& err) {
    Stop();
    try {
        auto interop = winrt::get_activation_factory<wgc::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
        wgc::GraphicsCaptureItem item{ nullptr };
        winrt::check_hresult(interop->CreateForWindow(wnd, winrt::guid_of<wgc::IGraphicsCaptureItem>(),
                                                      winrt::put_abi(item)));
        p_->useCrop = false;
        return p_->Begin(gpu, item, cursor, err);
    } catch (winrt::hresult_error const& e) {
        err = std::format(L"Не удалось открыть окно для захвата (0x{:08X})", static_cast<unsigned>(e.code().value));
        return false;
    }
}

void CaptureSession::Stop() { if (p_->active) p_->Teardown(); }
bool CaptureSession::Active() const { return p_->active; }

void CaptureSession::SetCursor(bool enabled) {
    try { if (p_->session) p_->session.IsCursorCaptureEnabled(enabled); } catch (...) {}
}

ComPtr<ID3D11Texture2D> CaptureSession::Latest(uint32_t& w, uint32_t& h) const {
    std::lock_guard lk(p_->mtx);
    w = p_->w; h = p_->h;
    return p_->latest;
}

uint64_t CaptureSession::Sequence() const { return p_->seq; }
int64_t CaptureSession::Timestamp() const { return p_->ts; }
HANDLE CaptureSession::Event() const { return p_->ev; }
bool CaptureSession::Closed() const { return p_->closed; }

double CaptureSession::Fps() const {
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    if (now.QuadPart - p_->lastFrameQpc > p_->freq) return 0.0;   // no frames for 1 s
    return p_->fps;
}

} // namespace nf
