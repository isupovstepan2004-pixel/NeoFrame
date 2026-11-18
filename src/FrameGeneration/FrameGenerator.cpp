#include "FrameGeneration/FrameGenerator.h"
#include "OpticalFlow/Shaders.h"
#include "Core/Logger.h"
#include <cstring>

using Microsoft::WRL::ComPtr;

namespace nf {

bool FrameGenerator::Init(Gpu& gpu, std::wstring& err) {
    gpu_ = &gpu;
    if (!flow_.Init(gpu, err)) return false;

    std::string e;
    auto vsb = gpu.Compile(kShaderSource, std::strlen(kShaderSource), "VSMain", "vs_5_0", e);
    auto pc = gpu.Compile(kShaderSource, std::strlen(kShaderSource), "PSCopy", "ps_5_0", e);
    auto pi = gpu.Compile(kShaderSource, std::strlen(kShaderSource), "PSInterp", "ps_5_0", e);
    if (!vsb || !pc || !pi) {
        Logger::Error(e);
        err = L"Ошибка компиляции шейдеров вывода (см. neoframe.log).";
        return false;
    }
    auto* dev = gpu.Device();
    if (FAILED(dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, vs_.GetAddressOf())) ||
        FAILED(dev->CreatePixelShader(pc->GetBufferPointer(), pc->GetBufferSize(), nullptr, psCopy_.GetAddressOf())) ||
        FAILED(dev->CreatePixelShader(pi->GetBufferPointer(), pi->GetBufferSize(), nullptr, psInterp_.GetAddressOf()))) {
        err = L"GPU не смог создать шейдеры вывода.";
        return false;
    }
    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    dev->CreateSamplerState(&sd, sampler_.GetAddressOf());
    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    dev->CreateRasterizerState(&rd, raster_.GetAddressOf());
    return true;
}

bool FrameGenerator::Resize(uint32_t w, uint32_t h) {
    Gpu::Lock lock(*gpu_);
    w_ = w; h_ = h; frames_ = 0; cur_ = 0; motionValid_ = false;
    D3D11_TEXTURE2D_DESC d{};
    d.Width = w; d.Height = h; d.MipLevels = 1; d.ArraySize = 1;
    d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    for (auto& f : color_) {
        if (FAILED(gpu_->Device()->CreateTexture2D(&d, nullptr, f.tex.ReleaseAndGetAddressOf())) ||
            FAILED(gpu_->Device()->CreateShaderResourceView(f.tex.Get(), nullptr, f.srv.ReleaseAndGetAddressOf())))
            return false;
    }
    return flow_.Resize(w, h);
}

void FrameGenerator::PushFrame(ID3D11Texture2D* src, bool analyzeMotion) {
    Gpu::Lock lock(*gpu_);
    if (frames_ > 0) cur_ = 1 - cur_;
    if (frames_ < 2) ++frames_;
    gpu_->Context()->CopyResource(color_[cur_].tex.Get(), src);
    flow_.Invalidate(cur_);
    motionValid_ = false;
    if (analyzeMotion) flow_.Analyze(color_[cur_].srv.Get(), cur_);
}

bool FrameGenerator::ComputeMotion(int quality) {
    if (frames_ < 2) return false;
    motionValid_ = flow_.Estimate(cur_, quality);
    return motionValid_;
}

void FrameGenerator::Draw(ID3D11RenderTargetView* rtv, const D3D11_VIEWPORT& vp, float t, bool useMotion) {
    Gpu::Lock lock(*gpu_);
    auto* ctx = gpu_->Context();
    const bool motion = useMotion && motionValid_ && flow_.Motion() != nullptr;
    flow_.UpdateConstants(t, motion);

    ctx->OMSetRenderTargets(1, &rtv, nullptr);
    ctx->RSSetViewports(1, &vp);
    ctx->RSSetState(raster_.Get());
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(vs_.Get(), nullptr, 0);
    ctx->PSSetShader(t >= 0.999f ? psCopy_.Get() : psInterp_.Get(), nullptr, 0);
    ID3D11Buffer* cb = flow_.Constants();
    ctx->PSSetConstantBuffers(0, 1, &cb);
    ID3D11SamplerState* smp = sampler_.Get();
    ctx->PSSetSamplers(0, 1, &smp);

    const int prev = frames_ >= 2 ? 1 - cur_ : cur_;
    ID3D11ShaderResourceView* srvs[3] = { color_[prev].srv.Get(), color_[cur_].srv.Get(),
                                          motion ? flow_.Motion() : nullptr };
    ctx->PSSetShaderResources(8, 3, srvs);
    ctx->Draw(3, 0);
    ID3D11ShaderResourceView* none[3] = {};
    ctx->PSSetShaderResources(8, 3, none);
}

} // namespace nf
