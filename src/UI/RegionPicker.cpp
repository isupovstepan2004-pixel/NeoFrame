#include "UI/RegionPicker.h"
#include <windowsx.h>
#include <algorithm>

namespace nf {

namespace {
struct PickState {
    bool dragging = false, done = false, ok = false;
    POINT a{}, b{};
    int originX = 0, originY = 0;
    RECT Rect() const {
        return RECT{ std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y) };
    }
};

LRESULT CALLBACK PickProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    auto* st = reinterpret_cast<PickState*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    switch (m) {
    case WM_NCCREATE:
        st = static_cast<PickState*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
        return TRUE;
    case WM_SETCURSOR:
        SetCursor(LoadCursorW(nullptr, IDC_CROSS));
        return TRUE;
    case WM_LBUTTONDOWN:
        st->dragging = true;
        st->a = st->b = POINT{ GET_X_LPARAM(l), GET_Y_LPARAM(l) };
        SetCapture(h);
        return 0;
    case WM_MOUSEMOVE:
        if (st && st->dragging) {
            st->b = POINT{ GET_X_LPARAM(l), GET_Y_LPARAM(l) };
            InvalidateRect(h, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONUP:
        if (st && st->dragging) {
            st->dragging = false;
            ReleaseCapture();
            RECT r = st->Rect();
            st->ok = (r.right - r.left) >= 64 && (r.bottom - r.top) >= 64;
            st->done = true;
        }
        return 0;
    case WM_KEYDOWN:
        if (w == VK_ESCAPE) { st->ok = false; st->done = true; }
        return 0;
    case WM_RBUTTONDOWN:
        st->ok = false; st->done = true;
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT cr; GetClientRect(h, &cr);
        HBRUSH bg = CreateSolidBrush(RGB(0, 0, 0));
        FillRect(dc, &cr, bg);
        DeleteObject(bg);
        if (st && (st->dragging || st->ok)) {
            RECT r = st->Rect();
            HBRUSH fill = CreateSolidBrush(RGB(30, 58, 95));
            FillRect(dc, &r, fill);
            DeleteObject(fill);
            HBRUSH edge = CreateSolidBrush(RGB(96, 165, 250));
            FrameRect(dc, &r, edge);
            DeleteObject(edge);
        }
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(245, 245, 245));
        const wchar_t* hint = L"Выделите область мышью  •  Esc — отмена";
        TextOutW(dc, 24, 24, hint, static_cast<int>(wcslen(hint)));
        EndPaint(h, &ps);
        return 0;
    }
    }
    return DefWindowProcW(h, m, w, l);
}
}

bool PickScreenRegion(HINSTANCE inst, RECT& out) {
    static const wchar_t kCls[] = L"NeoFrameRegionPicker";
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = PickProc;
    wc.hInstance = inst;
    wc.lpszClassName = kCls;
    RegisterClassExW(&wc);

    PickState st;
    st.originX = GetSystemMetrics(SM_XVIRTUALSCREEN);
    st.originY = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int w = GetSystemMetrics(SM_CXVIRTUALSCREEN), h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    HWND hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW, kCls, L"", WS_POPUP,
                                st.originX, st.originY, w, h, nullptr, nullptr, inst, &st);
    if (!hwnd) return false;
    SetLayeredWindowAttributes(hwnd, 0, 130, LWA_ALPHA);
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);

    MSG msg;
    while (!st.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    DestroyWindow(hwnd);
    if (!st.ok) return false;
    RECT r = st.Rect();
    out = RECT{ r.left + st.originX, r.top + st.originY, r.right + st.originX, r.bottom + st.originY };
    return true;
}

} // namespace nf
