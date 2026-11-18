#pragma once
#include <Windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <deque>
#include <functional>
#include <string>
#include <vector>
#include "Capture/CaptureSession.h"
#include "Core/Engine.h"
#include "Core/Types.h"
#include "Renderer/Preview.h"

namespace nf {

// Main window: Direct2D-drawn Windows 11 dark UI. Widgets are immediate-mode:
// each frame registers hit regions, and clicks run the callback of the last drawn layout.
class MainWindow {
public:
    bool Create(HINSTANCE inst);
    void Show(int cmd);

private:
    enum Page { kHome, kFrameGen, kCapture, kPerf, kSettings, kPageCount };
    struct Hit { D2D1_RECT_F r; std::function<void()> fn; };

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT Handle(UINT, WPARAM, LPARAM);

    // framework
    bool CreateDeviceIndependent();
    void CreateDeviceResources();
    void Render();
    void Resize();
    void ApplyDwmStyle();
    void DrawSidebar(float h);
    D2D1_RECT_F NavRect(int i) const;
    void SelectPage(int i);
    void OnMetricsTimer();

    // widgets (Widgets in MainWindow.cpp)
    void AddHit(const D2D1_RECT_F& r, std::function<void()> fn);
    bool Hover(const D2D1_RECT_F& r) const;
    void Fill(const D2D1_RECT_F& r, float radius, const D2D1_COLOR_F& c);
    void Stroke(const D2D1_RECT_F& r, float radius, const D2D1_COLOR_F& c);
    void CardBox(const D2D1_RECT_F& r);
    void Text(const std::wstring& s, IDWriteTextFormat* f, const D2D1_RECT_F& r, const D2D1_COLOR_F& c);
    void TextC(const std::wstring& s, IDWriteTextFormat* f, const D2D1_RECT_F& r, const D2D1_COLOR_F& c);
    void Toggle(const D2D1_RECT_F& r, bool on, bool enabled, std::function<void()> fn);
    void Segment(const D2D1_RECT_F& r, const std::vector<std::wstring>& opts, int sel, std::function<void(int)> fn);
    void Button(const D2D1_RECT_F& r, const std::wstring& label, int style, bool enabled, std::function<void()> fn);
    void StatCard(float x, float y, float w, float h, const std::wstring& label, const std::wstring& value, const std::wstring& note);
    void SettingRow(float x, float y, float w, float h, const std::wstring& title, const std::wstring& desc);

    // pages (Pages.cpp)
    void DrawHome();
    void DrawFrameGen();
    void DrawCapture();
    void DrawPerf();
    void DrawSettings();

    // actions
    void ToggleRun();
    void ApplySettings();
    void RefreshSources();
    void PickRegion();
    void SetNotice(const std::wstring& s) { notice_ = s; }

    HWND hwnd_ = nullptr;
    HINSTANCE inst_ = nullptr;
    float dpi_ = 96.0f;
    int page_ = kHome;
    float indicatorY_ = 0.0f;
    float mx_ = -1, my_ = -1;
    bool trackingMouse_ = false;
    std::vector<Hit> hits_;

    // layout of the current frame (DIPs)
    float cx_ = 0, cw_ = 0, ch_ = 0;

    // state
    Engine engine_;
    Settings settings_;
    Metrics metrics_;
    std::wstring initError_, notice_;
    std::vector<MonitorInfo> monitors_;
    std::vector<WindowInfo> windows_;
    HMONITOR selMonitor_ = nullptr;
    HWND selWindow_ = nullptr;
    bool selRegion_ = false;
    int windowScroll_ = 0;
    D2D1_RECT_F windowListRect_{ 0, 0, 0, 0 };
    std::deque<float> histSrc_, histOut_;
    std::vector<uint8_t> previewPixels_;
    bool previewDirty_ = false;
    HICON icon_ = nullptr;

    Microsoft::WRL::ComPtr<ID2D1Factory> d2d_;
    Microsoft::WRL::ComPtr<IDWriteFactory> dw_;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> rt_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> previewBmp_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> fLogo_, fTitle_, fBody_, fSmall_, fValue_, fBodyB_;
};

} // namespace nf
