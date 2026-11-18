#include "UI/MainWindow.h"
#include "UI/Theme.h"
#include "Core/Logger.h"

#include <dwmapi.h>
#include <windowsx.h>
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
constexpr float kNavTop = 96.0f;
constexpr float kNavH = 40.0f;
constexpr float kNavStep = 44.0f;
constexpr wchar_t kClass[] = L"NeoFrameMainWindow";
}

bool MainWindow::Create(HINSTANCE inst) {
    pages_ = {
        { L"Главная", L"Главная", L"Состояние приложения и источника изображения", {
            { L"Источник", L"Не выбран", L"Захват недоступен: этап 4" },
            { L"FPS источника", L"—", L"Появится после этапа 4" },
            { L"Целевой FPS", L"—", L"Появится после этапа 6" },
            { L"Сгенерировано кадров", L"—", L"Появится после этапа 6" },
            { L"Задержка обработки", L"—", L"Появится после этапа 8" },
            { L"Загрузка GPU", L"—", L"Появится после этапа 8" } } },
        { L"Frame Generation", L"Frame Generation", L"Генерация промежуточных кадров", {
            { L"Frame Generation", L"Недоступно", L"Не реализовано: этапы 5–6" },
            { L"Режим", L"×2 / ×3", L"Будет доступен после этапа 6" },
            { L"Алгоритм", L"Optical Flow", L"Планируется: DX11 compute, этап 5" } } },
        { L"Захват", L"Захват изображения", L"Windows Graphics Capture", {
            { L"Монитор", L"Недоступно", L"Этап 4" },
            { L"Окно", L"Недоступно", L"Этап 4" },
            { L"Область", L"Недоступно", L"Этап 4" } } },
        { L"Производительность", L"Производительность", L"Мониторинг в реальном времени", {
            { L"FPS источника", L"—", L"Этап 8" },
            { L"Выводимый FPS", L"—", L"Этап 8" },
            { L"Время обработки", L"—", L"Этап 8" },
            { L"GPU / CPU", L"—", L"Этап 8" },
            { L"Видеопамять", L"—", L"Этап 8" } } },
        { L"Настройки", L"Настройки", L"Параметры приложения", {
            { L"Язык", L"Русский", L"Выбор языка: позже" },
            { L"Автозапуск", L"Недоступно", L"Этап 3" } } },
    };

    if (!CreateDeviceIndependent()) return false;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClass;
    if (!RegisterClassExW(&wc)) {
        Logger::Error("RegisterClassEx failed");
        return false;
    }

    hwnd_ = CreateWindowExW(0, kClass, L"NeoFrame", WS_OVERLAPPEDWINDOW,
                            CW_USEDEFAULT, CW_USEDEFAULT, 1180, 760,
                            nullptr, nullptr, inst, this);
    if (!hwnd_) {
        Logger::Error("CreateWindowEx failed");
        return false;
    }
    dpi_ = static_cast<float>(GetDpiForWindow(hwnd_));
    ApplyDwmStyle();
    indicatorY_ = NavRect(selected_).top + 11.0f;
    return true;
}

void MainWindow::Show(int cmd) {
    ShowWindow(hwnd_, cmd);
    UpdateWindow(hwnd_);
}

bool MainWindow::CreateDeviceIndependent() {
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d_.GetAddressOf()))) {
        Logger::Error("D2D1CreateFactory failed");
        return false;
    }
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(dw_.GetAddressOf())))) {
        Logger::Error("DWriteCreateFactory failed");
        return false;
    }
    auto make = [&](ComPtr<IDWriteTextFormat>& f, float size, DWRITE_FONT_WEIGHT w) {
        dw_->CreateTextFormat(L"Segoe UI", nullptr, w, DWRITE_FONT_STYLE_NORMAL,
                              DWRITE_FONT_STRETCH_NORMAL, size, L"ru-ru", f.GetAddressOf());
        if (f) f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    };
    make(fLogo_, 20.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    make(fTitle_, 22.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    make(fBody_, 14.0f, DWRITE_FONT_WEIGHT_NORMAL);
    make(fSmall_, 12.0f, DWRITE_FONT_WEIGHT_NORMAL);
    make(fValue_, 24.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    return fLogo_ && fTitle_ && fBody_ && fSmall_ && fValue_;
}

void MainWindow::ApplyDwmStyle() {
    // Dark title bar matching the app background (Windows 11; ignored on older builds).
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd_, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    COLORREF cap = RGB(0x17, 0x17, 0x17), txt = RGB(0xF5, 0xF5, 0xF5);
    DwmSetWindowAttribute(hwnd_, DWMWA_CAPTION_COLOR, &cap, sizeof(cap));
    DwmSetWindowAttribute(hwnd_, DWMWA_TEXT_COLOR, &txt, sizeof(txt));
}

void MainWindow::CreateDeviceResources() {
    if (rt_) return;
    RECT rc; GetClientRect(hwnd_, &rc);
    auto props = D2D1::RenderTargetProperties();
    auto hprops = D2D1::HwndRenderTargetProperties(
        hwnd_, D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top));
    if (FAILED(d2d_->CreateHwndRenderTarget(props, hprops, rt_.GetAddressOf()))) {
        Logger::Error("CreateHwndRenderTarget failed");
        return;
    }
    rt_->SetDpi(dpi_, dpi_);
    rt_->CreateSolidColorBrush(kText, brush_.GetAddressOf());
}

void MainWindow::Resize() {
    if (!rt_) return;
    RECT rc; GetClientRect(hwnd_, &rc);
    rt_->Resize(D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top));
}

D2D1_RECT_F MainWindow::NavRect(int i) const {
    float top = kNavTop + i * kNavStep;
    return D2D1::RectF(12.0f, top, kSidebarW - 12.0f, top + kNavH);
}

int MainWindow::HitNav(float x, float y) const {
    for (int i = 0; i < static_cast<int>(pages_.size()); ++i) {
        auto r = NavRect(i);
        if (x >= r.left && x <= r.right && y >= r.top && y <= r.bottom) return i;
    }
    return -1;
}

void MainWindow::SelectPage(int i) {
    if (i < 0 || i == selected_) return;
    selected_ = i;
    SetTimer(hwnd_, kAnimTimer, 16, nullptr);  // animate the accent indicator
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::DrawLabel(const std::wstring& s, IDWriteTextFormat* f,
                           const D2D1_RECT_F& r, const D2D1_COLOR_F& c) {
    brush_->SetColor(c);
    rt_->DrawText(s.c_str(), static_cast<UINT32>(s.size()), f, r, brush_.Get(),
                  D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void MainWindow::Render() {
    CreateDeviceResources();
    if (!rt_) return;

    rt_->BeginDraw();
    rt_->SetTransform(D2D1::Matrix3x2F::Identity());
    rt_->Clear(kBg);
    const auto size = rt_->GetSize();

    // Sidebar
    brush_->SetColor(kBg2);
    rt_->FillRectangle(D2D1::RectF(0, 0, kSidebarW, size.height), brush_.Get());
    brush_->SetColor(kBorder);
    rt_->DrawLine(D2D1::Point2F(kSidebarW, 0), D2D1::Point2F(kSidebarW, size.height), brush_.Get(), 1.0f);

    // Logo: accent square + wordmark
    brush_->SetColor(kAccent);
    rt_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(24, 30, 48, 54), 7, 7), brush_.Get());
    DrawLabel(L"NeoFrame", fLogo_.Get(), D2D1::RectF(60, 28, kSidebarW - 12, 58), kText);

    // Navigation
    for (int i = 0; i < static_cast<int>(pages_.size()); ++i) {
        auto r = NavRect(i);
        if (i == selected_ || i == hover_) {
            brush_->SetColor(i == selected_ ? kCard : kCardHover);
            rt_->FillRoundedRectangle(D2D1::RoundedRect(r, 8, 8), brush_.Get());
        }
        DrawLabel(pages_[i].nav, fBody_.Get(), D2D1::RectF(r.left + 18, r.top + 9, r.right - 8, r.bottom),
                  i == selected_ ? kText : kText2);
    }
    brush_->SetColor(kAccent);
    rt_->FillRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(12.0f, indicatorY_, 15.0f, indicatorY_ + 18.0f), 1.5f, 1.5f), brush_.Get());

    // Content
    const Page& p = pages_[selected_];
    const float x0 = kSidebarW + kPad;
    const float contentW = size.width - kSidebarW - 2 * kPad;
    DrawLabel(p.title, fTitle_.Get(), D2D1::RectF(x0, 28, x0 + contentW, 62), kText);
    DrawLabel(p.subtitle, fBody_.Get(), D2D1::RectF(x0, 64, x0 + contentW, 90), kText2);

    const int cols = contentW >= 760.0f ? 3 : 2;
    const float cardW = (contentW - (cols - 1) * kGap) / cols;
    const float cardH = 118.0f;
    for (size_t i = 0; i < p.cards.size(); ++i) {
        const float cx = x0 + (i % cols) * (cardW + kGap);
        const float cy = 120.0f + (i / cols) * (cardH + kGap);
        auto rr = D2D1::RoundedRect(D2D1::RectF(cx, cy, cx + cardW, cy + cardH), kRadius, kRadius);
        brush_->SetColor(kCard);
        rt_->FillRoundedRectangle(rr, brush_.Get());
        brush_->SetColor(kBorder);
        rt_->DrawRoundedRectangle(rr, brush_.Get(), 1.0f);
        DrawLabel(p.cards[i].label, fSmall_.Get(), D2D1::RectF(cx + 18, cy + 14, cx + cardW - 18, cy + 34), kText2);
        DrawLabel(p.cards[i].value, fValue_.Get(), D2D1::RectF(cx + 18, cy + 38, cx + cardW - 18, cy + 74), kText);
        DrawLabel(p.cards[i].note, fSmall_.Get(), D2D1::RectF(cx + 18, cy + 84, cx + cardW - 18, cy + 106), kText2);
    }

    if (rt_->EndDraw() == D2DERR_RECREATE_TARGET) {
        brush_.Reset();
        rt_.Reset();  // device lost: recreated on the next frame
    }
}

LRESULT CALLBACK MainWindow::WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    MainWindow* self = nullptr;
    if (m == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(l);
        self = static_cast<MainWindow*>(cs->lpCreateParams);
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
    case WM_SIZE:
        Resize();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    case WM_GETMINMAXINFO: {
        auto mmi = reinterpret_cast<MINMAXINFO*>(l);
        mmi->ptMinTrackSize = { static_cast<LONG>(720 * dpi_ / 96.0f), static_cast<LONG>(480 * dpi_ / 96.0f) };
        return 0;
    }
    case WM_DPICHANGED: {
        dpi_ = static_cast<float>(HIWORD(w));
        if (rt_) rt_->SetDpi(dpi_, dpi_);
        auto r = reinterpret_cast<RECT*>(l);
        SetWindowPos(hwnd_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (!trackingMouse_) {
            TRACKMOUSEEVENT t{ sizeof(t), TME_LEAVE, hwnd_, 0 };
            TrackMouseEvent(&t);
            trackingMouse_ = true;
        }
        int hv = HitNav(GET_X_LPARAM(l) * 96.0f / dpi_, GET_Y_LPARAM(l) * 96.0f / dpi_);
        if (hv != hover_) {
            hover_ = hv;
            SetCursor(LoadCursorW(nullptr, hv >= 0 ? IDC_HAND : IDC_ARROW));
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        trackingMouse_ = false;
        hover_ = -1;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDOWN:
        SelectPage(HitNav(GET_X_LPARAM(l) * 96.0f / dpi_, GET_Y_LPARAM(l) * 96.0f / dpi_));
        return 0;
    case WM_TIMER:
        if (w == kAnimTimer) {
            float target = NavRect(selected_).top + 11.0f;
            indicatorY_ += (target - indicatorY_) * 0.25f;
            if (std::fabs(target - indicatorY_) < 0.5f) {
                indicatorY_ = target;
                KillTimer(hwnd_, kAnimTimer);
            }
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return 0;
    case WM_DESTROY:
        Logger::Info("Main window destroyed");
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd_, m, w, l);
}

} // namespace nf
