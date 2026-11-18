#pragma once
#include "Core/Gpu.h"
#include <array>
#include <string>

namespace nf {

// GPU optical-flow stage: luma pyramid -> coarse block matching (1/4 res)
// -> refinement (1/2 res) -> optional vector median. Output is a structured
// buffer of float4 {dx, dy, error, 0} on a 16x16 source-pixel grid.
class MotionEstimator {
public:
    bool Init(Gpu& gpu, std::wstring& err);
    bool Resize(uint32_t w, uint32_t h);

    // Computes the luma pyramid of the frame stored in `slot` (0/1).
    void Analyze(ID3D11ShaderResourceView* colorSrv, int slot);
    bool LumaReady(int slot) const { return lumaOk_[slot]; }
    void Invalidate(int slot) { lumaOk_[slot] = false; }

    // Estimates motion between slot `cur` and the other slot. quality: 0 fast, 1 balanced, 2 quality.
    bool Estimate(int cur, int quality);

    void UpdateConstants(float t, bool useMotion);
    ID3D11Buffer* Constants() const { return cb_.Get(); }
    ID3D11ShaderResourceView* Motion() const { return final_; }
    double GpuMs() const { return gpuMs_; }

private:
    struct Tex {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> uav;
    };
    struct Buf {
        Microsoft::WRL::ComPtr<ID3D11Buffer> buf;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> uav;
    };
    struct TimeQ {
        Microsoft::WRL::ComPtr<ID3D11Query> disjoint, t0, t1;
        bool inFlight = false;
    };

    bool MakeTex(Tex& t, uint32_t w, uint32_t h);
    bool MakeBuf(Buf& b, uint32_t count);
    bool MakeCS(const char* entry, Microsoft::WRL::ComPtr<ID3D11ComputeShader>& cs, std::wstring& err);
    void UnbindCS();
    void PollTimers();

    Gpu* gpu_ = nullptr;
    uint32_t w_ = 0, h_ = 0, l1w_ = 0, l1h_ = 0, l2w_ = 0, l2h_ = 0;
    uint32_t coarseW_ = 0, coarseH_ = 0, fineW_ = 0, fineH_ = 0, refineR_ = 0;
    std::array<Tex, 2> l1_, l2_;
    std::array<bool, 2> lumaOk_{ false, false };
    Buf coarse_, fineRaw_, fineMed_;
    ID3D11ShaderResourceView* final_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D11Buffer> cb_;
    Microsoft::WRL::ComPtr<ID3D11ComputeShader> csL1_, csL2_, csCoarse_, csRefine_, csMedian_;
    std::array<TimeQ, 4> tq_;
    int tqW_ = 0;
    double gpuMs_ = 0;
};

} // namespace nf
