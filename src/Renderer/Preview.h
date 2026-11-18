#pragma once
#include "Core/Gpu.h"
#include <cstdint>
#include <vector>

namespace nf {

// Renders the latest captured frame into a small BGRA image for the UI preview.
class Preview {
public:
    static constexpr int kW = 384, kH = 216;
    bool Init(Gpu& gpu);
    bool Render(ID3D11Texture2D* src, uint32_t sw, uint32_t sh, std::vector<uint8_t>& pixels);

private:
    Gpu* gpu_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vs_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> ps_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> smp_;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> rs_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> rt_, staging_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv_;
};

} // namespace nf
