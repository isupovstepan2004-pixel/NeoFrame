#include "Core/Engine.h"
#include "Core/Logger.h"
#include "FrameGeneration/FrameGenerator.h"
#include "Renderer/Overlay.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <timeapi.h>

namespace nf {

namespace {
double QpcFreq() {
    static const double f = [] { LARGE_INTEGER x; QueryPerformanceFrequency(&x); return static_cast<double>(x.QuadPart); }();
    return f;
}
double NowSec() {
    LARGE_INTEGER c; QueryPerformanceCounter(&c);
    return static_cast<double>(c.QuadPart) / QpcFreq();
}
D3D11_VIEWPORT FitViewport(UINT dw, UINT dh, uint32_t sw, uint32_t sh) {
    float s = std::min(static_cast<float>(dw) / sw, static_cast<float>(dh) / sh);
    float w = sw * s, h = sh * s;
    return D3D11_VIEWPORT{ std::floor((dw - w) / 2), std::floor((dh - h) / 2), std::floor(w), std::floor(h), 0.0f, 1.0f };
}
int ChooseMultiplier(const Settings& s, double srcFps) {
    int m = std::clamp(s.multiplier, 1, 3);
    if (s.targetFps > 0 && srcFps > 0) {
        int maxM = static_cast<int>(std::floor(s.targetFps / srcFps + 0.15));
        m = std::min(m, std::max(1, maxM));
    }
    return m;
}
}

bool Engine::Init(std::wstring& error) {
    if (!gpu_.Init(error)) return false;
    preview_.Init(gpu_);
    inited_ = true;
    return true;
}

void Engine::Shutdown() {
    Stop();
    cap_.Stop();
}

SourceInfo Engine::Source() const {
    SourceInfo s = source_;
    if (cap_.Active()) {
        uint32_t w = 0, h = 0;
        cap_.Latest(w, h);
        s.width = w; s.height = h;
    } else {
        s = SourceInfo{};
    }
    return s;
}

bool Engine::SelectMonitor(const MonitorInfo& mon, std::wstring& err) {
    if (!inited_) { err = L"GPU не готов."; return false; }
    if (running_) { err = L"Остановите генерацию, чтобы сменить источник."; return false; }
    if (!cap_.StartMonitor(gpu_, mon.handle, nullptr, Cfg().captureCursor, err)) { source_ = {}; return false; }
    source_ = { SourceKind::Monitor, mon.name, 0, 0 };
    srcMonitor_ = mon.handle; srcWindow_ = nullptr;
    return true;
}

bool Engine::SelectWindow(HWND wnd, const std::wstring& title, std::wstring& err) {
    if (!inited_) { err = L"GPU не готов."; return false; }
    if (running_) { err = L"Остановите генерацию, чтобы сменить источник."; return false; }
    if (!cap_.StartWindow(gpu_, wnd, Cfg().captureCursor, err)) { source_ = {}; return false; }
    source_ = { SourceKind::Window, title, 0, 0 };
    srcWindow_ = wnd; srcMonitor_ = nullptr;
    return true;
}

bool Engine::SelectRegion(const MonitorInfo& mon, const RECT& r, std::wstring& err) {
    if (!inited_) { err = L"GPU не готов."; return false; }
    if (running_) { err = L"Остановите генерацию, чтобы сменить источник."; return false; }
    RECT rel{ r.left - mon.rect.left, r.top - mon.rect.top, r.right - mon.rect.left, r.bottom - mon.rect.top };
    if (!cap_.StartMonitor(gpu_, mon.handle, &rel, Cfg().captureCursor, err)) { source_ = {}; return false; }
    source_ = { SourceKind::Region, L"Область экрана", 0, 0 };
    srcMonitor_ = mon.handle; srcWindow_ = nullptr;
    return true;
}

void Engine::SetSettings(const Settings& s) {
    { std::lock_guard lk(cfgMutex_); cfg_ = s; }
    cap_.SetCursor(s.captureCursor);
}

bool Engine::TakeAutoStopped(std::wstring& reason) {
    if (!autoStopped_.exchange(false)) return false;
    reason = autoStopReason_;
    return true;
}

bool Engine::Start(std::wstring& err) {
    if (running_) return true;
    if (!inited_) { err = L"GPU не готов."; return false; }
    if (!cap_.Active()) { err = L"Сначала выберите источник на странице «Захват»."; return false; }
    if (thread_.joinable()) thread_.join();
    stop_ = false;
    autoStopped_ = false;
    running_ = true;
    std::promise<std::wstring> p;
    auto fut = p.get_future();
    thread_ = std::thread(&Engine::Loop, this, std::move(p));
    if (fut.wait_for(std::chrono::seconds(8)) != std::future_status::ready) {
        err = L"Запуск вывода занял слишком много времени.";
        Stop();
        return false;
    }
    std::wstring result = fut.get();
    if (!result.empty()) {
        err = result;
        if (thread_.joinable()) thread_.join();
        running_ = false;
        return false;
    }
    return true;
}

void Engine::Stop() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
    running_ = false;
}

Metrics Engine::GetMetrics() {
    Metrics m;
    { std::lock_guard lk(metricsMutex_); m = metrics_; }
    m.running = running_;
    m.srcFps = cap_.Active() ? cap_.Fps() : 0.0;
    double now = NowSec();
    if (now - lastPerfTime_ >= 1.0) {
        lastPerfTime_ = now;
        perf_.Sample(cpuPct_, gpuPct_);
    }
    m.cpu = cpuPct_; m.gpu = gpuPct_;
    gpu_.QueryVram(m.vramUsed, m.vramBudget);
    return m;
}

bool Engine::GrabPreview(std::vector<uint8_t>& pixels) {
    if (!cap_.Active()) return false;
    uint32_t w = 0, h = 0;
    auto tex = cap_.Latest(w, h);
    if (!tex) return false;
    return preview_.Render(tex.Get(), w, h, pixels);
}

// ---------------------------------------------------------------- render loop
void Engine::Loop(std::promise<std::wstring> ready) {
    std::wstring err;
    Overlay overlay;
    FrameGenerator gen;

    RECT monRect{};
    HMONITOR hm = srcMonitor_;
    if (!hm && srcWindow_) hm = MonitorFromWindow(srcWindow_, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{ sizeof(mi) };
    if (!hm || !GetMonitorInfoW(hm, &mi)) {
        ready.set_value(L"Не удалось определить монитор для вывода.");
        return;
    }
    monRect = mi.rcMonitor;

    if (!gen.Init(gpu_, err) || !overlay.Create(gpu_, monRect, err)) {
        ready.set_value(err);
        return;
    }
    timeBeginPeriod(1);
    Logger::Info("Render loop started");
    ready.set_value(L"");

    uint64_t lastSeq = 0;
    double interval = 0, lastArrival = 0;
    uint64_t generated = 0, dropped = 0, outFrames = 0;
    double rateT0 = NowSec();
    uint64_t rateFrames0 = 0;
    double outFps = 0, latencyMs = 0;
    int effMult = 1;
    std::wstring stopReason;

    auto publish = [&] {
        std::lock_guard lk(metricsMutex_);
        metrics_.outFps = outFps;
        metrics_.flowMs = gen.FlowGpuMs();
        metrics_.latencyMs = latencyMs;
        metrics_.generated = generated;
        metrics_.dropped = dropped;
        metrics_.effectiveMult = effMult;
    };
    auto updateRate = [&] {
        double now = NowSec();
        if (now - rateT0 >= 0.5) {
            outFps = static_cast<double>(outFrames - rateFrames0) / (now - rateT0);
            rateT0 = now; rateFrames0 = outFrames;
            publish();
        }
    };
    // Sleep until `slot`, but give up if a newer source frame arrives or we are stopped.
    auto waitUntil = [&](double slot, uint64_t seq) {
        for (;;) {
            if (stop_ || cap_.Sequence() != seq) return false;
            double rem = slot - NowSec();
            if (rem <= 0) return true;
            if (rem > 0.002) Sleep(1); else YieldProcessor();
        }
    };

    while (!stop_) {
        overlay.Pump();
        DWORD w = WaitForSingleObject(cap_.Event(), 100);
        if (stop_) break;
        if (cap_.Closed() || (srcWindow_ && !IsWindow(srcWindow_))) {
            stopReason = L"Источник захвата закрыт.";
            break;
        }
        updateRate();
        if (w != WAIT_OBJECT_0) continue;

        uint32_t sw = 0, sh = 0;
        auto tex = cap_.Latest(sw, sh);
        uint64_t seq = cap_.Sequence();
        if (!tex || seq == lastSeq) continue;
        if (lastSeq > 0 && seq > lastSeq + 1) dropped += seq - lastSeq - 1;
        lastSeq = seq;
        const double tArrive = static_cast<double>(cap_.Timestamp()) / QpcFreq();

        if (sw != gen.Width() || sh != gen.Height()) {
            if (!gen.Resize(sw, sh)) { stopReason = L"Не удалось выделить видеопамять под кадры."; break; }
            lastArrival = 0; interval = 0;
        }
        const Settings s = Cfg();
        const bool useFlow = s.fgEnabled && s.algorithm == 0;
        gen.PushFrame(tex.Get(), useFlow);

        if (lastArrival > 0) {
            double dt = tArrive - lastArrival;
            if (dt > 0.002 && dt < 0.5) interval = interval == 0 ? dt : interval * 0.9 + dt * 0.1;
        }
        lastArrival = tArrive;

        const double srcFps = interval > 0 ? 1.0 / interval : 0.0;
        const int mult = s.fgEnabled ? ChooseMultiplier(s, srcFps) : 1;
        effMult = mult;
        const D3D11_VIEWPORT vp = FitViewport(overlay.Width(), overlay.Height(), sw, sh);

        if (mult <= 1 || !gen.HasPair() || interval <= 0) {
            overlay.Clear();
            gen.Draw(overlay.Rtv(), vp, 1.0f, false);
            overlay.Present(s.allowTearing);
            ++outFrames;
            latencyMs = (NowSec() - tArrive) * 1000.0;
            continue;
        }

        const bool motion = useFlow && gen.ComputeMotion(s.quality);
        const double t0 = NowSec();
        for (int j = 1; j <= mult; ++j) {
            if (j > 1 && !waitUntil(t0 + (j - 1) * interval / mult, seq)) break;   // new frame arrived: restart
            const float t = static_cast<float>(j) / mult;                          // j == mult -> current frame
            overlay.Clear();
            gen.Draw(overlay.Rtv(), vp, t, motion);
            overlay.Present(s.allowTearing);
            ++outFrames;
            if (j < mult) ++generated;
            if (j == 1) latencyMs = (NowSec() - tArrive) * 1000.0;
        }
    }

    timeEndPeriod(1);
    overlay.Destroy();
    publish();
    if (!stop_ && !stopReason.empty()) {
        autoStopReason_ = stopReason;
        autoStopped_ = true;
    }
    running_ = false;
    Logger::Info("Render loop stopped");
}

} // namespace nf
