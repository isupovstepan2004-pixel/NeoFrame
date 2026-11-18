#include "Renderer/Overlay.h"
#include "Core/Logger.h"
#include <format>

using Microsoft::WRL::ComPtr;

namespace nf {

namespace { constexpr wchar_t kOverlayClass[] = L"NeoFrameOverlay"; }

LRESULT CALLBACK Overlay::WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_NCHITTEST: return HTTRANSPARENT;       // let mouse reach the game
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_ERASEBKGND: return 1;
    }
    return DefWindowProcW(h, m, w, l);
}

bool Overlay::Create(Gpu& gpu, const RECT& mon, std::wstring& err) {
    gpu_ = &gpu;
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.lpszClassName = kOverlayClass;
    RegisterClassExW(&wc);   // fails harmlessly if already registered

    w_ = static_cast<UINT>(mon.right - mon.left);
    h_ = static_cast<UINT>(mon.bottom - mon.top);
    hwnd_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED | WS_EX_TRANSPARENT,
                            kOverlayClass, L"NeoFrame Overlay", WS_POPUP, mon.left, mon.top,
                            static_cast<int>(w_), static_cast<int>(h_), nullptr, nullptr, inst, nullptr);
    if (!hwnd_) {
        err = L"Не удалось создать окно вывода.";
        return false;
    }
    SetLayeredWindowAttributes(hwnd_, 0, 255, LWA_ALPHA);
    SetWindowDisplayAffinity(hwnd_, 0x00000011 /*WDA_EXCLUDEFROMCAPTURE*/);

    tearingCap_ = gpu.TearingSupported();
    DXGI_SWAP_CHAIN_DESC1 d{};
    d.Width = w_; d.Height = h_;
    d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    d.BufferCount = 2;
    d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    d.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    d.Flags = tearingCap_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
    HRESULT hr = gpu.Factory()->CreateSwapChainForHwnd(gpu.Device(), hwnd_, &d, nullptr, nullptr, sc_.GetAddressOf());
    if (FAILED(hr)) {
        err = std::format(L"Не удалось создать swap chain (0x{:08X})", static_cast<unsigned>(hr));
        Destroy();
        return false;
    }
    gpu.Factory()->MakeWindowAssociation(hwnd_, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);
    ComPtr<ID3D11Texture2D> bb;
    sc_->GetBuffer(0, IID_PPV_ARGS(bb.GetAddressOf()));
    if (FAILED(gpu.Device()->CreateRenderTargetView(bb.Get(), nullptr, rtv_.GetAddressOf()))) {
        err = L"Не удалось создать render target.";
        Destroy();
        return false;
    }
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    SetWindowPos(hwnd_, HWND_TOPMOST, mon.left, mon.top, static_cast<int>(w_), static_cast<int>(h_),
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    return true;
}

void Overlay::Destroy() {
    rtv_.Reset();
    sc_.Reset();
    if (hwnd_) { DestroyWindow(hwnd_); hwnd_ = nullptr; }
}

void Overlay::Pump() {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

void Overlay::Clear() {
    const float black[4] = { 0, 0, 0, 1 };
    gpu_->Context()->ClearRenderTargetView(rtv_.Get(), black);
}

void Overlay::Present(bool allowTearing) {
    UINT flags = (tearingCap_ && allowTearing) ? DXGI_PRESENT_ALLOW_TEARING : 0;
    sc_->Present(0, flags);
}

} // namespace nf
