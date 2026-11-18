#include "UI/MainWindow.h"
#include "UI/Logo.h"
#include "UI/Theme.h"
#include "Core/Logger.h"
#include "Settings/Settings.h"
#include "UI/RegionPicker.h"

#include <dwmapi.h>
#include <windowsx.h>
#include <algorithm>
#include <cmath>

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_CAPTION_COLOR
#define DWMWA_CAPTION_COLOR 35
#endif
#ifndef DWMWA_TEXT_COLOR
#define DWMWA_TEXT_COLOR 36
#endif

using Microsoft::WRL::ComPtr;
using namespace nf::theme;

namespace nf {

namespace {
constexpr UINT_PTR kAnimTimer = 1;
constexpr UINT_PTR kMetricsTimer = 2;
constexpr int kHotkeyId = 1;
constexpr float kNavTop = 96.0f, kNavH = 40.0f, kNavStep = 44.0f;
constexpr wchar_t kClass[] = L"NeoFrameMainWindow";
const wchar_t* const kNavNames[] = { L"Главная", L"Frame Generation", L"Захват", L"Производительность", L"Настройки" };
}

// ------------------------------------------------------------------ lifecycle
bool MainWindow::Create(HINSTANCE inst) {
    inst_ = inst;
    settings_ = SettingsStore::Load();
    if (!engine_.Init(initError_)) Logger::Error("Engine init failed");
    engine_.SetSettings(settings_);
    RefreshSources();

    if (!CreateDeviceIndependent()) return false;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClass;
    if (!RegisterClassExW(&wc)) { Logger::Error("RegisterClassEx failed"); return false; }

    hwnd_ = CreateWindowExW(0, kClass, L"NeoFrame", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                            1180, 780, nullptr, nullptr, inst, this);
    if (!hwnd_) { Logger::Error("CreateWindowEx failed"); return false; }

    dpi_ = static_cast<float>(GetDpiForWindow(hwnd_));
    ApplyDwmStyle();
    icon_ = CreateLogoIcon(d2d_.Get(), 64);
    if (icon_) {
        SendMessageW(hwnd_, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon_));
        SendMessageW(hwnd_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon_));
    }
    if (!RegisterHotKey(hwnd_, kHotkeyId, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'F'))
        Logger::Warn("Hotkey Ctrl+Alt+F is taken by another app");
    SetTimer(hwnd_, kMetricsTimer, 500, nullptr);
    indicatorY_ = NavRect(page_).top + 11.0f;
    return true;
}

void MainWindow::Show(int cmd) {
    ShowWindow(hwnd_, cmd);
    UpdateWindow(hwnd_);
}

bool MainWindow::CreateDeviceIndependent() {
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d_.GetAddressOf()))) return false;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(dw_.GetAddressOf())))) return false;
    auto make = [&](ComPtr<IDWriteTextFormat>& f, float size, DWRITE_FONT_WEIGHT w) {
        dw_->CreateTextFormat(L"Segoe UI", nullptr, w, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                              size, L"ru-ru", f.GetAddressOf());
        if (f) f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    };
    make(fLogo_, 20.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    make(fTitle_, 22.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    make(fBody_, 14.0f, DWRITE_FONT_WEIGHT_NORMAL);
    make(fBodyB_, 14.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    make(fSmall_, 12.0f, DWRITE_FONT_WEIGHT_NORMAL);
    make(fValue_, 24.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    return fLogo_ && fTitle_ && fBody_ && fBodyB_ && fSmall_ && fValue_;
}

void MainWindow::ApplyDwmStyle() {
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd_, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    COLORREF cap = RGB(0x17, 0x17, 0x17), txt = RGB(0xF5, 0xF5, 0xF5);
    DwmSetWindowAttribute(hwnd_, DWMWA_CAPTION_COLOR, &cap, sizeof(cap));
    DwmSetWindowAttribute(hwnd_, DWMWA_TEXT_COLOR, &txt, sizeof(txt));
}

void MainWindow::CreateDeviceResources() {
    if (rt_) return;
    RECT rc; GetClientRect(hwnd_, &rc);
    if (FAILED(d2d_->CreateHwndRenderTarget(D2D1::RenderTargetProperties(),
            D2D1::HwndRenderTargetProperties(hwnd_, D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top)),
            rt_.GetAddressOf()))) {
        Logger::Error("CreateHwndRenderTarget failed");
        return;
    }
    rt_->SetDpi(dpi_, dpi_);
    rt_->CreateSolidColorBrush(kText, brush_.GetAddressOf());
    previewBmp_.Reset();
    previewDirty_ = !previewPixels_.empty();
}

void MainWindow::Resize() {
    if (!rt_) return;
    RECT rc; GetClientRect(hwnd_, &rc);
    rt_->Resize(D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top));
}

// ----------------------------------------------------------------- navigation
D2D1_RECT_F MainWindow::NavRect(int i) const {
    float top = kNavTop + i * kNavStep;
    return D2D1::RectF(12.0f, top, kSidebarW - 12.0f, top + kNavH);
}

void MainWindow::SelectPage(int i) {
    if (i == page_) return;
    page_ = i;
    SetTimer(hwnd_, kAnimTimer, 16, nullptr);
    if (i == kCapture) RefreshSources();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// -------------------------------------------------------------------- widgets
void MainWindow::AddHit(const D2D1_RECT_F& r, std::function<void()> fn) { hits_.push_back({ r, std::move(fn) }); }

bool MainWindow::Hover(const D2D1_RECT_F& r) const { return mx_ >= r.left && mx_ <= r.right && my_ >= r.top && my_ <= r.bottom; }

void MainWindow::Fill(const D2D1_RECT_F& r, float rad, const D2D1_COLOR_F& c) {
    brush_->SetColor(c);
    rt_->FillRoundedRectangle(D2D1::RoundedRect(r, rad, rad), brush_.Get());
}

void MainWindow::Stroke(const D2D1_RECT_F& r, float rad, const D2D1_COLOR_F& c) {
    brush_->SetColor(c);
    rt_->DrawRoundedRectangle(D2D1::RoundedRect(r, rad, rad), brush_.Get(), 1.0f);
}

void MainWindow::CardBox(const D2D1_RECT_F& r) { Fill(r, kRadius, kCard); Stroke(r, kRadius, kBorder); }

void MainWindow::Text(const std::wstring& s, IDWriteTextFormat* f, const D2D1_RECT_F& r, const D2D1_COLOR_F& c) {
    brush_->SetColor(c);
    rt_->DrawText(s.c_str(), static_cast<UINT32>(s.size()), f, r, brush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void MainWindow::TextC(const std::wstring& s, IDWriteTextFormat* f, const D2D1_RECT_F& r, const D2D1_COLOR_F& c) {
    f->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    Text(s, f, r, c);
    f->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
}

void MainWindow::Toggle(const D2D1_RECT_F& r, bool on, bool enabled, std::function<void()> fn) {
    const float h = r.bottom - r.top, w = r.right - r.left;
    D2D1_COLOR_F track = on ? kAccent : kTrackOff;
    if (!enabled) track.a = 0.35f;
    Fill(r, h / 2, track);
    const float kr = h / 2 - 4;
    const float kx = on ? r.right - h / 2 : r.left + h / 2;
    brush_->SetColor(on ? kOnAccent : kText);
    rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(kx, r.top + h / 2), kr, kr), brush_.Get());
    (void)w;
    if (enabled) AddHit(r, std::move(fn));
}

void MainWindow::Segment(const D2D1_RECT_F& r, const std::vector<std::wstring>& opts, int sel, std::function<void(int)> fn) {
    Fill(r, 9, kBg2);
    Stroke(r, 9, kBorder);
    const float w = (r.right - r.left) / opts.size();
    for (size_t i = 0; i < opts.size(); ++i) {
        D2D1_RECT_F c = D2D1::RectF(r.left + i * w + 3, r.top + 3, r.left + (i + 1) * w - 3, r.bottom - 3);
        const bool on = static_cast<int>(i) == sel;
        if (on) Fill(c, 7, kAccent);
        else if (Hover(c)) Fill(c, 7, kCardHover);
        TextC(opts[i], fBody_.Get(), c, on ? kOnAccent : kText2);
        int idx = static_cast<int>(i);
        AddHit(c, [fn, idx] { fn(idx); });
    }
}

// style: 0 secondary, 1 primary (accent), 2 danger
void MainWindow::Button(const D2D1_RECT_F& r, const std::wstring& label, int style, bool enabled, std::function<void()> fn) {
    const bool hov = enabled && Hover(r);
    D2D1_COLOR_F bg = style == 1 ? kAccent : style == 2 ? kDanger : kBg2;
    D2D1_COLOR_F fg = style == 0 ? kText : kOnAccent;
    if (hov) { bg.r = std::min(1.0f, bg.r + 0.06f); bg.g = std::min(1.0f, bg.g + 0.06f); bg.b = std::min(1.0f, bg.b + 0.06f); }
    if (!enabled) { bg.a = 0.35f; fg.a = 0.55f; }
    Fill(r, 10, bg);
    if (style == 0) Stroke(r, 10, kBorder);
    TextC(label, fBodyB_.Get(), r, fg);
    if (enabled) AddHit(r, std::move(fn));
}

void MainWindow::StatCard(float x, float y, float w, float h, const std::wstring& label,
                          const std::wstring& value, const std::wstring& note) {
    CardBox(D2D1::RectF(x, y, x + w, y + h));
    Text(label, fSmall_.Get(), D2D1::RectF(x + 18, y + 14, x + w - 18, y + 34), kText2);
    Text(value, fValue_.Get(), D2D1::RectF(x + 18, y + 36, x + w - 18, y + 72), kText);
    Text(note, fSmall_.Get(), D2D1::RectF(x + 18, y + h - 28, x + w - 18, y + h - 8), kText2);
}

void MainWindow::SettingRow(float x, float y, float w, float h, const std::wstring& title, const std::wstring& desc) {
    CardBox(D2D1::RectF(x, y, x + w, y + h));
    Text(title, fBodyB_.Get(), D2D1::RectF(x + 20, y + h / 2 - 22, x + w * 0.55f, y + h / 2), kText);
    Text(desc, fSmall_.Get(), D2D1::RectF(x + 20, y + h / 2 + 2, x + w * 0.55f, y + h / 2 + 22), kText2);
}

// ------------------------------------------------------------------- drawing
void MainWindow::DrawSidebar(float h) {
    Fill(D2D1::RectF(0, 0, kSidebarW, h), 0, kBg2);
    brush_->SetColor(kBorder);
    rt_->DrawLine(D2D1::Point2F(kSidebarW, 0), D2D1::Point2F(kSidebarW, h), brush_.Get(), 1.0f);

    DrawLogo(rt_.Get(), 20, 24, 40);
    Text(L"NeoFrame", fLogo_.Get(), D2D1::RectF(72, 28, kSidebarW - 12, 58), kText);

    for (int i = 0; i < kPageCount; ++i) {
        auto r = NavRect(i);
        if (i == page_) Fill(r, 8, kCard);
        else if (Hover(r)) Fill(r, 8, kCardHover);
        Text(kNavNames[i], fBody_.Get(), D2D1::RectF(r.left + 18, r.top + 9, r.right - 8, r.bottom), i == page_ ? kText : kText2);
        AddHit(r, [this, i] { SelectPage(i); });
    }
    brush_->SetColor(kAccent);
    rt_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(12.0f, indicatorY_, 15.0f, indicatorY_ + 18.0f), 1.5f, 1.5f), brush_.Get());
    Text(L"Ctrl + Alt + F — старт / стоп", fSmall_.Get(), D2D1::RectF(20, h - 34, kSidebarW - 8, h - 12), kText2);
}

void MainWindow::Render() {
    CreateDeviceResources();
    if (!rt_) return;
    if (previewDirty_ && !previewPixels_.empty()) {
        rt_->CreateBitmap(D2D1::SizeU(Preview::kW, Preview::kH), previewPixels_.data(), Preview::kW * 4,
                          D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE)),
                          previewBmp_.ReleaseAndGetAddressOf());
        previewDirty_ = false;
    }
    hits_.clear();
    rt_->BeginDraw();
    rt_->SetTransform(D2D1::Matrix3x2F::Identity());
    rt_->Clear(kBg);
    const auto size = rt_->GetSize();
    cx_ = kSidebarW + kPad;
    cw_ = size.width - kSidebarW - 2 * kPad;
    ch_ = size.height;

    DrawSidebar(size.height);
    switch (page_) {
    case kHome: DrawHome(); break;
    case kFrameGen: DrawFrameGen(); break;
    case kCapture: DrawCapture(); break;
    case kPerf: DrawPerf(); break;
    case kSettings: DrawSettings(); break;
    }
    if (rt_->EndDraw() == D2DERR_RECREATE_TARGET) { brush_.Reset(); previewBmp_.Reset(); rt_.Reset(); }
}

// ------------------------------------------------------------------- actions
void MainWindow::RefreshSources() {
    monitors_ = EnumerateMonitors();
    windows_ = EnumerateWindows(GetCurrentProcessId());
    windowScroll_ = 0;
}

void MainWindow::ApplySettings() {
    engine_.SetSettings(settings_);
    SettingsStore::Save(settings_);
}

void MainWindow::ToggleRun() {
    std::wstring err;
    if (engine_.Running()) {
        engine_.Stop();
        SetNotice(L"");
    } else if (!engine_.Start(err)) {
        SetNotice(err);
    } else {
        SetNotice(L"");
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::PickRegion() {
    if (engine_.Running()) { SetNotice(L"Остановите генерацию, чтобы сменить источник."); return; }
    ShowWindow(hwnd_, SW_MINIMIZE);
    Sleep(250);
    RECT r{};
    bool ok = PickScreenRegion(inst_, r);
    ShowWindow(hwnd_, SW_RESTORE);
    SetForegroundWindow(hwnd_);
    if (!ok) return;
    HMONITOR hm = MonitorFromRect(&r, MONITOR_DEFAULTTONEAREST);
    for (auto& m : monitors_) {
        if (m.handle != hm) continue;
        RECT c{ std::max(r.left, m.rect.left), std::max(r.top, m.rect.top), std::min(r.right, m.rect.right), std::min(r.bottom, m.rect.bottom) };
        std::wstring err;
        if (engine_.SelectRegion(m, c, err)) {
            selMonitor_ = nullptr; selWindow_ = nullptr; selRegion_ = true;
            SetNotice(L"");
        } else {
            SetNotice(err);
        }
        return;
    }
}

void MainWindow::OnMetricsTimer() {
    metrics_ = engine_.GetMetrics();
    histSrc_.push_back(static_cast<float>(metrics_.srcFps));
    histOut_.push_back(static_cast<float>(metrics_.outFps));
    while (histSrc_.size() > 120) histSrc_.pop_front();
    while (histOut_.size() > 120) histOut_.pop_front();
    std::wstring reason;
    if (engine_.TakeAutoStopped(reason)) {
        engine_.Stop();
        SetNotice(reason);
    }
    if (page_ == kCapture && engine_.GrabPreview(previewPixels_)) previewDirty_ = true;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// ------------------------------------------------------------------- messages
LRESULT CALLBACK MainWindow::WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    MainWindow* self = nullptr;
    if (m == WM_NCCREATE) {
        self = static_cast<MainWindow*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
        self->hwnd_ = h;
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    }
    return self ? self->Handle(m, w, l) : DefWindowProcW(h, m, w, l);
}

LRESULT MainWindow::Handle(UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_PAINT: {
        PAINTSTRUCT ps; BeginPaint(hwnd_, &ps);
        Render();
        EndPaint(hwnd_, &ps);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_SIZE: Resize(); InvalidateRect(hwnd_, nullptr, FALSE); return 0;
    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(l);
        mmi->ptMinTrackSize = { static_cast<LONG>(940 * dpi_ / 96.0f), static_cast<LONG>(640 * dpi_ / 96.0f) };
        return 0;
    }
    case WM_DPICHANGED: {
        dpi_ = static_cast<float>(HIWORD(w));
        if (rt_) rt_->SetDpi(dpi_, dpi_);
        auto* r = reinterpret_cast<RECT*>(l);
        SetWindowPos(hwnd_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (!trackingMouse_) {
            TRACKMOUSEEVENT t{ sizeof(t), TME_LEAVE, hwnd_, 0 };
            TrackMouseEvent(&t);
            trackingMouse_ = true;
        }
        mx_ = GET_X_LPARAM(l) * 96.0f / dpi_;
        my_ = GET_Y_LPARAM(l) * 96.0f / dpi_;
        bool over = false;
        for (auto& hit : hits_) if (Hover(hit.r)) { over = true; break; }
        SetCursor(LoadCursorW(nullptr, over ? IDC_HAND : IDC_ARROW));
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    }
    case WM_MOUSELEAVE:
        trackingMouse_ = false; mx_ = my_ = -1;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDOWN: {
        float x = GET_X_LPARAM(l) * 96.0f / dpi_, y = GET_Y_LPARAM(l) * 96.0f / dpi_;
        for (auto it = hits_.rbegin(); it != hits_.rend(); ++it) {
            if (x >= it->r.left && x <= it->r.right && y >= it->r.top && y <= it->r.bottom) {
                auto fn = it->fn;       // copy: the callback may trigger a repaint
                fn();
                break;
            }
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    }
    case WM_MOUSEWHEEL:
        if (page_ == kCapture && Hover(windowListRect_)) {
            windowScroll_ -= GET_WHEEL_DELTA_WPARAM(w) / WHEEL_DELTA;
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return 0;
    case WM_HOTKEY:
        if (w == kHotkeyId) ToggleRun();
        return 0;
    case WM_TIMER:
        if (w == kAnimTimer) {
            float target = NavRect(page_).top + 11.0f;
            indicatorY_ += (target - indicatorY_) * 0.25f;
            if (std::fabs(target - indicatorY_) < 0.5f) { indicatorY_ = target; KillTimer(hwnd_, kAnimTimer); }
            InvalidateRect(hwnd_, nullptr, FALSE);
        } else if (w == kMetricsTimer) {
            OnMetricsTimer();
        }
        return 0;
    case WM_DESTROY:
        UnregisterHotKey(hwnd_, kHotkeyId);
        KillTimer(hwnd_, kMetricsTimer);
        engine_.Shutdown();
        SettingsStore::Save(settings_);
        if (icon_) DestroyIcon(icon_);
        Logger::Info("Main window destroyed");
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd_, m, w, l);
}

} // namespace nf
