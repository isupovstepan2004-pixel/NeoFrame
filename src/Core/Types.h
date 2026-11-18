#pragma once
#include <cstdint>
#include <string>

namespace nf {

// User-editable settings (persisted by Settings module).
struct Settings {
    bool fgEnabled = true;
    int multiplier = 2;        // 2 or 3
    int algorithm = 0;         // 0 = optical flow (GPU), 1 = plain blend (no motion)
    int quality = 1;           // 0 fast, 1 balanced, 2 quality
    int targetFps = 0;         // 0 = auto (no limit)
    bool captureCursor = true;
    bool allowTearing = true;
    bool runWithWindows = false;
};

// Live numbers published by the engine.
struct Metrics {
    double srcFps = 0, outFps = 0;
    double flowMs = 0;          // GPU time of motion analysis
    double latencyMs = 0;       // capture -> first presented frame
    uint64_t generated = 0, dropped = 0;
    double cpu = 0, gpu = 0;    // percent
    uint64_t vramUsed = 0, vramBudget = 0;
    int effectiveMult = 1;
    bool running = false;
};

enum class SourceKind { None, Monitor, Window, Region };

struct SourceInfo {
    SourceKind kind = SourceKind::None;
    std::wstring name;
    uint32_t width = 0, height = 0;
};

} // namespace nf
