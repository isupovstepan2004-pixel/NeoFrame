#pragma once
#include <Windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <string>
#include <vector>

namespace nf {

// Main window: Direct2D-drawn Windows 11 dark UI with sidebar navigation.
// Pages show real status only; features not implemented yet are labeled unavailable.
class MainWindow {
public:
    bool Create(HINSTANCE inst);
    void Show(int cmd);

private:
    struct Card { const wchar_t* label; const wchar_t* value; const wchar_t* note; };
    struct Page { const wchar_t* nav; const wchar_t* title; const wchar_t* subtitle; std::vector<Card> cards; };

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT Handle(UINT, WPARAM, LPARAM);

    bool CreateDeviceIndependent();
    void CreateDeviceResources();
    void Render();
    void Resize();
    void ApplyDwmStyle();
    D2D1_RECT_F NavRect(int i) const;
    int HitNav(float x, float y) const;
    void SelectPage(int i);
    void DrawLabel(const std::wstring& s, IDWriteTextFormat* f, const D2D1_RECT_F& r, const D2D1_COLOR_F& c);

    HWND hwnd_ = nullptr;
    float dpi_ = 96.0f;
    int selected_ = 0;
    int hover_ = -1;
    float indicatorY_ = 0.0f;
    bool trackingMouse_ = false;
    std::vector<Page> pages_;

    Microsoft::WRL::ComPtr<ID2D1Factory> d2d_;
    Microsoft::WRL::ComPtr<IDWriteFactory> dw_;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> rt_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> fLogo_, fTitle_, fBody_, fSmall_, fValue_;
};

} // namespace nf
