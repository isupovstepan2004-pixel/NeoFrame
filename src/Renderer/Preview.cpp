#include "Renderer/Preview.h"
#include "OpticalFlow/Shaders.h"
#include <algorithm>
#include <cstring>

using Microsoft::WRL::ComPtr;

namespace nf {

bool Preview::Init(Gpu& gpu) {
    gpu_ = &gpu;
    std::string e;
    auto vb = gpu.Compile(kShaderSource, std::strlen(kShaderSource), "VSMain", "vs_5_0", e);
    auto pb = gpu.Compile(kShaderSource, std::strlen(kShaderSource), "PSCopy", "ps_5_0", e);
    if (!vb || !pb) return false;
    auto* dev = gpu.Device();
    if (FAILED(dev->CreateVertexShader(vb->GetBufferPointer(), vb->GetBufferSize(), nullptr, vs_.GetAddressOf())) ||
        FAILED(dev->CreatePixelShader(pb->GetBufferPointer(), pb->GetBufferSize(), nullptr, ps_.GetAddressOf())))
        return false;
    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    dev->CreateSamplerState(&sd, smp_.GetAddressOf());
    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE;
    dev->CreateRasterizerState(&rd, rs_.GetAddressOf());

    D3D11_TEXTURE2D_DESC d{};
    d.Width = kW; d.Height = kH; d.MipLevels = 1; d.ArraySize = 1;
    d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_RENDER_TARGET;
    if (FAILED(dev->CreateTexture2D(&d, nullptr, rt_.GetAddressOf())) ||
        FAILED(dev->CreateRenderTargetView(rt_.Get(), nullptr, rtv_.GetAddressOf())))
        return false;
    d.Usage = D3D11_USAGE_STAGING;
    d.BindFlags = 0;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    return SUCCEEDED(dev->CreateTexture2D(&d, nullptr, staging_.GetAddressOf()));
}

bool Preview::Render(ID3D11Texture2D* src, uint32_t sw, uint32_t sh, std::vector<uint8_t>& px) {
    if (!src || sw == 0 || sh == 0 || !rtv_) return false;
    Gpu::Lock lock(*gpu_);
    auto* ctx = gpu_->Context();
    ComPtr<ID3D11ShaderResourceView> srv;
    if (FAILED(gpu_->Device()->CreateShaderResourceView(src, nullptr, srv.GetAddressOf()))) return false;

    const float black[4] = { 0.09f, 0.09f, 0.09f, 1 };
    ctx->ClearRenderTargetView(rtv_.Get(), black);
    float scale = std::min(static_cast<float>(kW) / sw, static_cast<float>(kH) / sh);
    float vw = sw * scale, vh = sh * scale;
    D3D11_VIEWPORT vp{ (kW - vw) / 2, (kH - vh) / 2, vw, vh, 0, 1 };
    ID3D11RenderTargetView* rtv = rtv_.Get();
    ctx->OMSetRenderTargets(1, &rtv, nullptr);
    ctx->RSSetViewports(1, &vp);
    ctx->RSSetState(rs_.Get());
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(vs_.Get(), nullptr, 0);
    ctx->PSSetShader(ps_.Get(), nullptr, 0);
    ID3D11SamplerState* s = smp_.Get();
    ctx->PSSetSamplers(0, 1, &s);
    ID3D11ShaderResourceView* v = srv.Get();
    ctx->PSSetShaderResources(9, 1, &v);
    ctx->Draw(3, 0);
    ID3D11ShaderResourceView* none = nullptr;
    ctx->PSSetShaderResources(9, 1, &none);
    ctx->OMSetRenderTargets(0, nullptr, nullptr);

    ctx->CopyResource(staging_.Get(), rt_.Get());
    D3D11_MAPPED_SUBRESOURCE m{};
    if (FAILED(ctx->Map(staging_.Get(), 0, D3D11_MAP_READ, 0, &m))) return false;
    px.resize(static_cast<size_t>(kW) * kH * 4);
    for (int y = 0; y < kH; ++y)
        std::memcpy(px.data() + static_cast<size_t>(y) * kW * 4,
                    static_cast<const uint8_t*>(m.pData) + static_cast<size_t>(y) * m.RowPitch, kW * 4);
    ctx->Unmap(staging_.Get(), 0);
    return true;
}

} // namespace nf
