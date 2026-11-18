#include "Core/Gpu.h"
#include "Core/Logger.h"
#include <format>

using Microsoft::WRL::ComPtr;

namespace nf {

bool Gpu::Init(std::wstring& error) {
    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL got{};
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                   D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 1,
                                   D3D11_SDK_VERSION, dev_.GetAddressOf(), &got, ctx_.GetAddressOf());
    if (FAILED(hr)) {
        error = std::format(L"Не удалось создать устройство DirectX 11 (нужен GPU с Feature Level 11.0). Код 0x{:08X}",
                            static_cast<unsigned>(hr));
        Logger::Error("D3D11CreateDevice failed");
        return false;
    }
    if (SUCCEEDED(dev_.As(&mt_))) mt_->SetMultithreadProtected(TRUE);

    ComPtr<IDXGIDevice> dxgiDev;
    ComPtr<IDXGIAdapter> adapter;
    if (SUCCEEDED(dev_.As(&dxgiDev)) && SUCCEEDED(dxgiDev->GetAdapter(adapter.GetAddressOf()))) {
        DXGI_ADAPTER_DESC d{};
        adapter->GetDesc(&d);
        adapterName_ = d.Description;
        adapter->GetParent(IID_PPV_ARGS(factory_.GetAddressOf()));
        adapter.As(&adapter3_);
    }
    if (!factory_) {
        error = L"Не удалось получить DXGI Factory.";
        return false;
    }
    ComPtr<IDXGIFactory5> f5;
    if (SUCCEEDED(factory_.As(&f5))) {
        BOOL allow = FALSE;
        if (SUCCEEDED(f5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow, sizeof(allow))))
            tearing_ = allow != FALSE;
    }
    Logger::Info("GPU ready, tearing=" + std::string(tearing_ ? "yes" : "no"));
    return true;
}

bool Gpu::QueryVram(uint64_t& used, uint64_t& budget) const {
    if (!adapter3_) return false;
    DXGI_QUERY_VIDEO_MEMORY_INFO info{};
    if (FAILED(adapter3_->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info))) return false;
    used = info.CurrentUsage;
    budget = info.Budget;
    return true;
}

ComPtr<ID3DBlob> Gpu::Compile(const char* src, size_t len, const char* entry,
                              const char* profile, std::string& err) const {
    ComPtr<ID3DBlob> blob, errors;
    HRESULT hr = D3DCompile(src, len, nullptr, nullptr, nullptr, entry, profile,
                            D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, blob.GetAddressOf(), errors.GetAddressOf());
    if (FAILED(hr)) {
        err = std::string("Shader ") + entry + ": " +
              (errors ? std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize()) : "unknown error");
        return nullptr;
    }
    return blob;
}

} // namespace nf
