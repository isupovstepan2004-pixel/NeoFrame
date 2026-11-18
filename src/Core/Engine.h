#pragma once
#include <atomic>
#include <future>
#include <mutex>
#include <thread>
#include <vector>
#include "Capture/CaptureSession.h"
#include "Core/Gpu.h"
#include "Core/Types.h"
#include "Performance/PerfMonitor.h"
#include "Renderer/Preview.h"

namespace nf {

// Orchestrates capture -> motion analysis -> interpolation -> overlay output.
// Selecting a source starts a capture session (used for the preview);
// Start() adds the render thread and the overlay.
class Engine {
public:
    ~Engine() { Shutdown(); }
    bool Init(std::wstring& error);
    void Shutdown();

    bool SelectMonitor(const MonitorInfo& mon, std::wstring& err);
    bool SelectWindow(HWND wnd, const std::wstring& title, std::wstring& err);
    bool SelectRegion(const MonitorInfo& mon, const RECT& screenRect, std::wstring& err);
    bool HasSource() const { return cap_.Active(); }
    SourceInfo Source() const;
    double CaptureFps() const { return cap_.Fps(); }

    bool Start(std::wstring& err);
    void Stop();
    bool Running() const { return running_; }
    bool TakeAutoStopped(std::wstring& reason);   // true once if the engine stopped by itself

    void SetSettings(const Settings& s);
    Metrics GetMetrics();
    bool GrabPreview(std::vector<uint8_t>& pixels);

    const std::wstring& GpuName() const { return gpu_.AdapterName(); }
    bool TearingSupported() const { return gpu_.TearingSupported(); }

private:
    void Loop(std::promise<std::wstring> ready);
    Settings Cfg() const { std::lock_guard lk(cfgMutex_); return cfg_; }

    Gpu gpu_;
    CaptureSession cap_;
    Preview preview_;
    PerfMonitor perf_;
    bool inited_ = false;

    SourceInfo source_;
    HMONITOR srcMonitor_ = nullptr;
    HWND srcWindow_ = nullptr;

    std::thread thread_;
    std::atomic<bool> stop_{ false };
    std::atomic<bool> running_{ false };
    std::atomic<bool> autoStopped_{ false };
    std::wstring autoStopReason_;

    mutable std::mutex cfgMutex_;
    Settings cfg_;
    std::mutex metricsMutex_;
    Metrics metrics_;
    double lastPerfTime_ = 0;
    double cpuPct_ = 0, gpuPct_ = 0;
};

} // namespace nf
