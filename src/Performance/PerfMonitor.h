#pragma once
#include <Windows.h>
#include <pdh.h>
#include <pdhmsg.h>

namespace nf {

// System CPU load (GetSystemTimes) and GPU engine load (PDH "GPU Engine" counters,
// the same source Task Manager uses; the busiest engine is reported).
class PerfMonitor {
public:
    PerfMonitor();
    ~PerfMonitor();
    void Sample(double& cpuPercent, double& gpuPercent);

private:
    PDH_HQUERY query_ = nullptr;
    PDH_HCOUNTER gpuCounter_ = nullptr;
    bool gpuOk_ = false;
    ULONGLONG lastIdle_ = 0, lastKernel_ = 0, lastUser_ = 0;
};

} // namespace nf
