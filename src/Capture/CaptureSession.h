#pragma once
#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <memory>
#include <string>
#include <vector>
#include "Core/Gpu.h"

namespace nf {

struct MonitorInfo { HMONITOR handle; RECT rect; std::wstring name; bool primary; };
struct WindowInfo  { HWND handle; std::wstring title; };

std::vector<MonitorInfo> EnumerateMonitors();
std::vector<WindowInfo> EnumerateWindows(DWORD excludePid);

// Windows Graphics Capture session. Each arriving frame is copied (optionally
// cropped) into a single GPU texture; consumers poll Sequence()/Latest().
class CaptureSession {
public:
    CaptureSession();
    ~CaptureSession();

    bool StartMonitor(Gpu& gpu, HMONITOR mon, const RECT* cropRelative, bool cursor, std::wstring& err);
    bool StartWindow(Gpu& gpu, HWND wnd, bool cursor, std::wstring& err);
    void Stop();
    bool Active() const;
    void SetCursor(bool enabled);

    Microsoft::WRL::ComPtr<ID3D11Texture2D> Latest(uint32_t& w, uint32_t& h) const;
    uint64_t Sequence() const;
    int64_t Timestamp() const;     // QPC ticks of the latest frame
    HANDLE Event() const;          // auto-reset, set on each new frame
    double Fps() const;
    bool Closed() const;

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

} // namespace nf
