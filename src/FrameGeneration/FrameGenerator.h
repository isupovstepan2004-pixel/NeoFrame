#pragma once
#include "Core/Gpu.h"
#include "Core/Types.h"
#include "OpticalFlow/MotionEstimator.h"
#include <array>

namespace nf {

// Keeps the last two source frames on the GPU and renders interpolated frames:
// motion-compensated warp of both frames toward time t, blended, with an
// occlusion fallback. t == 1 shows the current frame unchanged.
class FrameGenerator {
public:
    bool Init(Gpu& gpu, std::wstring& err);
    bool Resize(uint32_t w, uint32_t h);
    uint32_t Width() const { return w_; }
    uint32_t Height() const { return h_; }

    void PushFrame(ID3D11Texture2D* src, bool analyzeMotion);
    bool HasPair() const { return frames_ >= 2; }
    bool ComputeMotion(int quality);
    void Draw(ID3D11RenderTargetView* rtv, const D3D11_VIEWPORT& vp, float t, bool useMotion);
    double FlowGpuMs() const { return flow_.GpuMs(); }

private:
    Gpu* gpu_ = nullptr;
    MotionEstimator flow_;
    uint32_t w_ = 0, h_ = 0;
    int cur_ = 0;
    uint32_t frames_ = 0;
    bool motionValid_ = false;
    struct Frame {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
    };
    std::array<Frame, 2> color_;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vs_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> psCopy_, psInterp_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> raster_;
};

} // namespace nf
