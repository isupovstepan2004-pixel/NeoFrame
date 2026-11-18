#pragma once
#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <string>
#include "Core/Gpu.h"

namespace nf {

// Borderless, topmost, click-through window covering one monitor with a flip-model
// swap chain. Excluded from screen capture so monitor capture does not feed back.
class Overlay {
public:
    ~Overlay() { Destroy(); }
    bool Create(Gpu& gpu, const RECT& monitor, std::wstring& err);
    void Destroy();
    void Pump();
    void Clear();
    void Present(bool allowTearing);
    ID3D11RenderTargetView* Rtv() const { return rtv_.Get(); }
    UINT Width() const { return w_; }
    UINT Height() const { return h_; }

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    Gpu* gpu_ = nullptr;
    HWND hwnd_ = nullptr;
    UINT w_ = 0, h_ = 0;
    bool tearingCap_ = false;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> sc_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv_;
};

} // namespace nf
