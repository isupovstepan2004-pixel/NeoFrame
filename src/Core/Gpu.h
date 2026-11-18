#pragma once
#include <Windows.h>
#include <d3d11.h>
#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstdint>
#include <string>

namespace nf {

// Owns the single D3D11 device shared by capture, analysis and rendering.
// The immediate context is multithread-protected; multi-call sequences use Gpu::Lock.
class Gpu {
public:
    bool Init(std::wstring& error);

    ID3D11Device* Device() const { return dev_.Get(); }
    ID3D11DeviceContext* Context() const { return ctx_.Get(); }
    IDXGIFactory2* Factory() const { return factory_.Get(); }
    const std::wstring& AdapterName() const { return adapterName_; }
    bool TearingSupported() const { return tearing_; }
    bool QueryVram(uint64_t& used, uint64_t& budget) const;

    Microsoft::WRL::ComPtr<ID3DBlob> Compile(const char* src, size_t len, const char* entry,
                                             const char* profile, std::string& err) const;

    struct Lock {
        explicit Lock(Gpu& g) : mt(g.mt_.Get()) { if (mt) mt->Enter(); }
        ~Lock() { if (mt) mt->Leave(); }
        Lock(const Lock&) = delete;
        Lock& operator=(const Lock&) = delete;
        ID3D11Multithread* mt;
    };

private:
    Microsoft::WRL::ComPtr<ID3D11Device> dev_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> ctx_;
    Microsoft::WRL::ComPtr<ID3D11Multithread> mt_;
    Microsoft::WRL::ComPtr<IDXGIFactory2> factory_;
    Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter3_;
    std::wstring adapterName_;
    bool tearing_ = false;
};

} // namespace nf
