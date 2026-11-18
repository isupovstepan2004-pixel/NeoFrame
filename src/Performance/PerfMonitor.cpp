#include "Performance/PerfMonitor.h"
#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace nf {

namespace {
ULONGLONG ToU64(const FILETIME& f) { return (static_cast<ULONGLONG>(f.dwHighDateTime) << 32) | f.dwLowDateTime; }
}

PerfMonitor::PerfMonitor() {
    if (PdhOpenQueryW(nullptr, 0, &query_) == ERROR_SUCCESS) {
        gpuOk_ = PdhAddEnglishCounterW(query_, L"\\GPU Engine(*)\\Utilization Percentage", 0, &gpuCounter_) == ERROR_SUCCESS;
        if (gpuOk_) PdhCollectQueryData(query_);
    }
    FILETIME i, k, u;
    if (GetSystemTimes(&i, &k, &u)) { lastIdle_ = ToU64(i); lastKernel_ = ToU64(k); lastUser_ = ToU64(u); }
}

PerfMonitor::~PerfMonitor() { if (query_) PdhCloseQuery(query_); }

void PerfMonitor::Sample(double& cpu, double& gpu) {
    cpu = 0; gpu = 0;
    FILETIME i, k, u;
    if (GetSystemTimes(&i, &k, &u)) {
        ULONGLONG idle = ToU64(i), kern = ToU64(k), user = ToU64(u);
        ULONGLONG dIdle = idle - lastIdle_, dTotal = (kern - lastKernel_) + (user - lastUser_);  // kernel includes idle
        if (dTotal > 0) cpu = std::clamp(100.0 * (1.0 - static_cast<double>(dIdle) / static_cast<double>(dTotal)), 0.0, 100.0);
        lastIdle_ = idle; lastKernel_ = kern; lastUser_ = user;
    }
    if (!gpuOk_) return;
    if (PdhCollectQueryData(query_) != ERROR_SUCCESS) return;
    DWORD size = 0, count = 0;
    if (PdhGetFormattedCounterArrayW(gpuCounter_, PDH_FMT_DOUBLE, &size, &count, nullptr) != PDH_MORE_DATA) return;
    std::vector<BYTE> buf(size);
    auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buf.data());
    if (PdhGetFormattedCounterArrayW(gpuCounter_, PDH_FMT_DOUBLE, &size, &count, items) != ERROR_SUCCESS) return;

    std::map<std::wstring, double> engines;   // sum over processes per physical engine
    for (DWORD n = 0; n < count; ++n) {
        if (items[n].FmtValue.CStatus != PDH_CSTATUS_VALID_DATA && items[n].FmtValue.CStatus != PDH_CSTATUS_NEW_DATA) continue;
        std::wstring name = items[n].szName;
        auto p = name.find(L"luid_");
        if (p == std::wstring::npos) continue;
        engines[name.substr(p)] += items[n].FmtValue.doubleValue;
    }
    double best = 0;
    for (auto& [key, v] : engines) best = std::max(best, v);
    gpu = std::clamp(best, 0.0, 100.0);
}

} // namespace nf
