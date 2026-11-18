#include "UI/Logo.h"
#include "UI/Theme.h"
#include <wincodec.h>
#include <wrl/client.h>
#include <cstring>

using Microsoft::WRL::ComPtr;
using namespace nf::theme;

namespace nf {

void DrawLogo(ID2D1RenderTarget* rt, float x, float y, float s) {
    ComPtr<ID2D1Factory> factory;
    rt->GetFactory(factory.GetAddressOf());

    D2D1_GRADIENT_STOP stops[2] = { { 0.0f, kAccent }, { 1.0f, kAccent2 } };
    ComPtr<ID2D1GradientStopCollection> coll;
    ComPtr<ID2D1LinearGradientBrush> grad;
    if (FAILED(rt->CreateGradientStopCollection(stops, 2, coll.GetAddressOf()))) return;
    if (FAILED(rt->CreateLinearGradientBrush(
            D2D1::LinearGradientBrushProperties(D2D1::Point2F(x, y), D2D1::Point2F(x + s, y + s)),
            coll.Get(), grad.GetAddressOf()))) return;
    rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x, y, x + s, y + s), s * 0.28f, s * 0.28f), grad.Get());

    ComPtr<ID2D1SolidColorBrush> dark;
    rt->CreateSolidColorBrush(D2D1::ColorF(0x0F172A), dark.GetAddressOf());
    const float fw = s * 0.34f, fh = s * 0.46f, fy = y + (s - fh) / 2.0f, r = s * 0.07f;
    const float xs[3] = { x + s * 0.20f, x + s * 0.33f, x + s * 0.46f };
    const float alpha[3] = { 0.22f, 0.5f, 1.0f };
    for (int i = 0; i < 3; ++i) {
        dark->SetOpacity(alpha[i]);
        rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(xs[i], fy, xs[i] + fw, fy + fh), r, r), dark.Get());
    }

    // Play triangle on the front (real) frame.
    const float cx = xs[2] + fw * 0.5f, cy = fy + fh * 0.5f, t = fw * 0.27f;
    ComPtr<ID2D1PathGeometry> pg;
    ComPtr<ID2D1GeometrySink> sink;
    if (SUCCEEDED(factory->CreatePathGeometry(pg.GetAddressOf())) && SUCCEEDED(pg->Open(sink.GetAddressOf()))) {
        sink->BeginFigure(D2D1::Point2F(cx - t * 0.6f, cy - t), D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLine(D2D1::Point2F(cx - t * 0.6f, cy + t));
        sink->AddLine(D2D1::Point2F(cx + t * 0.95f, cy));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->Close();
        rt->FillGeometry(pg.Get(), grad.Get());
    }
}

HICON CreateLogoIcon(ID2D1Factory* factory, int size) {
    ComPtr<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wic.GetAddressOf()))))
        return nullptr;
    ComPtr<IWICBitmap> bmp;
    if (FAILED(wic->CreateBitmap(size, size, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, bmp.GetAddressOf())))
        return nullptr;
    ComPtr<ID2D1RenderTarget> rt;
    auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    if (FAILED(factory->CreateWicBitmapRenderTarget(bmp.Get(), props, rt.GetAddressOf()))) return nullptr;
    rt->BeginDraw();
    rt->Clear(D2D1::ColorF(0, 0.0f));
    DrawLogo(rt.Get(), 0, 0, static_cast<float>(size));
    if (FAILED(rt->EndDraw())) return nullptr;

    WICRect rc{ 0, 0, size, size };
    ComPtr<IWICBitmapLock> lock;
    if (FAILED(bmp->Lock(&rc, WICBitmapLockRead, lock.GetAddressOf()))) return nullptr;
    UINT stride = 0, bytes = 0;
    BYTE* data = nullptr;
    lock->GetStride(&stride);
    lock->GetDataPointer(&bytes, &data);

    BITMAPV5HEADER bi{};
    bi.bV5Size = sizeof(bi);
    bi.bV5Width = size; bi.bV5Height = -size; bi.bV5Planes = 1; bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00FF0000; bi.bV5GreenMask = 0x0000FF00; bi.bV5BlueMask = 0x000000FF; bi.bV5AlphaMask = 0xFF000000;
    void* dst = nullptr;
    HDC dc = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(dc, reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS, &dst, nullptr, 0);
    ReleaseDC(nullptr, dc);
    if (!color || !dst) return nullptr;
    for (int y = 0; y < size; ++y)
        std::memcpy(static_cast<BYTE*>(dst) + static_cast<size_t>(y) * size * 4, data + static_cast<size_t>(y) * stride, static_cast<size_t>(size) * 4);
    HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
    ICONINFO ii{ TRUE, 0, 0, mask, color };
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(color);
    DeleteObject(mask);
    return icon;
}

} // namespace nf
