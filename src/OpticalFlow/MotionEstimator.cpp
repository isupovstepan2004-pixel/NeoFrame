#include "OpticalFlow/MotionEstimator.h"
#include "OpticalFlow/Shaders.h"
#include "Core/Logger.h"
#include <algorithm>
#include <cstring>
#include <string>

using Microsoft::WRL::ComPtr;

namespace nf {

namespace {
struct CBData {
    uint32_t sizes[4];
    uint32_t sizes2[4];
    uint32_t sizes3[4];
    float params[4];
};
static_assert(sizeof(CBData) == 64);
}

bool MotionEstimator::MakeCS(const char* entry, ComPtr<ID3D11ComputeShader>& cs, std::wstring& err) {
    std::string e;
    auto blob = gpu_->Compile(kShaderSource, std::strlen(kShaderSource), entry, "cs_5_0", e);
    if (!blob) {
        Logger::Error(e);
        err = L"Ошибка компиляции шейдера анализа движения (см. neoframe.log).";
        return false;
    }
    if (FAILED(gpu_->Device()->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(),
                                                   nullptr, cs.GetAddressOf()))) {
        err = L"GPU не смог создать compute-шейдер.";
        return false;
    }
    return true;
}

bool MotionEstimator::Init(Gpu& gpu, std::wstring& err) {
    gpu_ = &gpu;
    if (!MakeCS("CSLumaL1", csL1_, err) || !MakeCS("CSLumaL2", csL2_, err) ||
        !MakeCS("CSMotionCoarse", csCoarse_, err) || !MakeCS("CSMotionRefine", csRefine_, err) ||
        !MakeCS("CSMedian", csMedian_, err))
        return false;

    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = sizeof(CBData);
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(gpu.Device()->CreateBuffer(&bd, nullptr, cb_.GetAddressOf()))) {
        err = L"Не удалось создать константный буфер.";
        return false;
    }
    D3D11_QUERY_DESC qd{};
    for (auto& q : tq_) {
        qd.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
        gpu.Device()->CreateQuery(&qd, q.disjoint.GetAddressOf());
        qd.Query = D3D11_QUERY_TIMESTAMP;
        gpu.Device()->CreateQuery(&qd, q.t0.GetAddressOf());
        gpu.Device()->CreateQuery(&qd, q.t1.GetAddressOf());
    }
    return true;
}

bool MotionEstimator::MakeTex(Tex& t, uint32_t w, uint32_t h) {
    D3D11_TEXTURE2D_DESC d{};
    d.Width = w; d.Height = h; d.MipLevels = 1; d.ArraySize = 1;
    d.Format = DXGI_FORMAT_R32_FLOAT;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    auto* dev = gpu_->Device();
    return SUCCEEDED(dev->CreateTexture2D(&d, nullptr, t.tex.ReleaseAndGetAddressOf())) &&
           SUCCEEDED(dev->CreateShaderResourceView(t.tex.Get(), nullptr, t.srv.ReleaseAndGetAddressOf())) &&
           SUCCEEDED(dev->CreateUnorderedAccessView(t.tex.Get(), nullptr, t.uav.ReleaseAndGetAddressOf()));
}

bool MotionEstimator::MakeBuf(Buf& b, uint32_t count) {
    D3D11_BUFFER_DESC d{};
    d.ByteWidth = count * 16;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    d.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    d.StructureByteStride = 16;
    auto* dev = gpu_->Device();
    if (FAILED(dev->CreateBuffer(&d, nullptr, b.buf.ReleaseAndGetAddressOf()))) return false;
    D3D11_SHADER_RESOURCE_VIEW_DESC sv{};
    sv.Format = DXGI_FORMAT_UNKNOWN;
    sv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    sv.Buffer.FirstElement = 0;
    sv.Buffer.NumElements = count;
    D3D11_UNORDERED_ACCESS_VIEW_DESC uv{};
    uv.Format = DXGI_FORMAT_UNKNOWN;
    uv.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    uv.Buffer.FirstElement = 0;
    uv.Buffer.NumElements = count;
    return SUCCEEDED(dev->CreateShaderResourceView(b.buf.Get(), &sv, b.srv.ReleaseAndGetAddressOf())) &&
           SUCCEEDED(dev->CreateUnorderedAccessView(b.buf.Get(), &uv, b.uav.ReleaseAndGetAddressOf()));
}

bool MotionEstimator::Resize(uint32_t w, uint32_t h) {
    if (w < 32 || h < 32) return false;
    w_ = w; h_ = h;
    l1w_ = w / 2; l1h_ = h / 2;
    l2w_ = std::max(1u, l1w_ / 2); l2h_ = std::max(1u, l1h_ / 2);
    coarseW_ = (l2w_ + 7) / 8; coarseH_ = (l2h_ + 7) / 8;
    fineW_ = (l1w_ + 7) / 8;   fineH_ = (l1h_ + 7) / 8;
    final_ = nullptr;
    lumaOk_ = { false, false };
    for (int i = 0; i < 2; ++i)
        if (!MakeTex(l1_[i], l1w_, l1h_) || !MakeTex(l2_[i], l2w_, l2h_)) return false;
    return MakeBuf(coarse_, coarseW_ * coarseH_) && MakeBuf(fineRaw_, fineW_ * fineH_) &&
           MakeBuf(fineMed_, fineW_ * fineH_);
}

void MotionEstimator::UpdateConstants(float t, bool useMotion) {
    CBData c{};
    c.sizes[0] = w_;  c.sizes[1] = h_;  c.sizes[2] = l1w_; c.sizes[3] = l1h_;
    c.sizes2[0] = l2w_; c.sizes2[1] = l2h_; c.sizes2[2] = coarseW_; c.sizes2[3] = coarseH_;
    c.sizes3[0] = fineW_; c.sizes3[1] = fineH_; c.sizes3[2] = refineR_; c.sizes3[3] = useMotion ? 1 : 0;
    c.params[0] = t; c.params[1] = 0.12f;
    D3D11_MAPPED_SUBRESOURCE m{};
    if (SUCCEEDED(gpu_->Context()->Map(cb_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        std::memcpy(m.pData, &c, sizeof(c));
        gpu_->Context()->Unmap(cb_.Get(), 0);
    }
}

void MotionEstimator::UnbindCS() {
    auto* ctx = gpu_->Context();
    ID3D11ShaderResourceView* nullSrv[11] = {};
    ID3D11UnorderedAccessView* nullUav[2] = {};
    ctx->CSSetShaderResources(0, 11, nullSrv);
    ctx->CSSetUnorderedAccessViews(0, 2, nullUav, nullptr);
}

void MotionEstimator::Analyze(ID3D11ShaderResourceView* colorSrv, int slot) {
    Gpu::Lock lock(*gpu_);
    auto* ctx = gpu_->Context();
    UpdateConstants(0.0f, false);
    ID3D11Buffer* cb = cb_.Get();
    ctx->CSSetConstantBuffers(0, 1, &cb);

    ctx->CSSetShader(csL1_.Get(), nullptr, 0);
    ctx->CSSetShaderResources(0, 1, &colorSrv);
    ID3D11UnorderedAccessView* u = l1_[slot].uav.Get();
    ctx->CSSetUnorderedAccessViews(0, 1, &u, nullptr);
    ctx->Dispatch((l1w_ + 7) / 8, (l1h_ + 7) / 8, 1);
    UnbindCS();

    ctx->CSSetShader(csL2_.Get(), nullptr, 0);
    ID3D11ShaderResourceView* s = l1_[slot].srv.Get();
    ctx->CSSetShaderResources(1, 1, &s);
    u = l2_[slot].uav.Get();
    ctx->CSSetUnorderedAccessViews(0, 1, &u, nullptr);
    ctx->Dispatch((l2w_ + 7) / 8, (l2h_ + 7) / 8, 1);
    UnbindCS();
    lumaOk_[slot] = true;
}

void MotionEstimator::PollTimers() {
    auto* ctx = gpu_->Context();
    for (auto& q : tq_) {
        if (!q.inFlight) continue;
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj{};
        if (ctx->GetData(q.disjoint.Get(), &dj, sizeof(dj), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) continue;
        UINT64 a = 0, b = 0;
        if (!dj.Disjoint && ctx->GetData(q.t0.Get(), &a, sizeof(a), 0) == S_OK &&
            ctx->GetData(q.t1.Get(), &b, sizeof(b), 0) == S_OK && dj.Frequency > 0) {
            double ms = static_cast<double>(b - a) * 1000.0 / static_cast<double>(dj.Frequency);
            gpuMs_ = gpuMs_ == 0 ? ms : gpuMs_ * 0.8 + ms * 0.2;
        }
        q.inFlight = false;
    }
}

bool MotionEstimator::Estimate(int cur, int quality) {
    const int prev = 1 - cur;
    if (!lumaOk_[cur] || !lumaOk_[prev]) return false;
    Gpu::Lock lock(*gpu_);
    auto* ctx = gpu_->Context();
    PollTimers();
    TimeQ& tq = tq_[tqW_];
    const bool timing = !tq.inFlight && tq.disjoint && tq.t0 && tq.t1;
    if (timing) { ctx->Begin(tq.disjoint.Get()); ctx->End(tq.t0.Get()); }

    refineR_ = quality == 0 ? 0 : (quality == 1 ? 2 : 3);
    UpdateConstants(0.0f, false);
    ID3D11Buffer* cb = cb_.Get();
    ctx->CSSetConstantBuffers(0, 1, &cb);

    // 1) coarse search
    ctx->CSSetShader(csCoarse_.Get(), nullptr, 0);
    ID3D11ShaderResourceView* s2[2] = { l2_[cur].srv.Get(), l2_[prev].srv.Get() };
    ctx->CSSetShaderResources(2, 2, s2);
    ID3D11UnorderedAccessView* u = coarse_.uav.Get();
    ctx->CSSetUnorderedAccessViews(1, 1, &u, nullptr);
    ctx->Dispatch(coarseW_, coarseH_, 1);
    UnbindCS();

    // 2) refinement
    ctx->CSSetShader(csRefine_.Get(), nullptr, 0);
    ID3D11ShaderResourceView* s3[3] = { l1_[cur].srv.Get(), l1_[prev].srv.Get(), coarse_.srv.Get() };
    ctx->CSSetShaderResources(4, 3, s3);
    u = fineRaw_.uav.Get();
    ctx->CSSetUnorderedAccessViews(1, 1, &u, nullptr);
    ctx->Dispatch(fineW_, fineH_, 1);
    UnbindCS();
    final_ = fineRaw_.srv.Get();

    // 3) median filter (balanced / quality)
    if (quality >= 1) {
        ctx->CSSetShader(csMedian_.Get(), nullptr, 0);
        ID3D11ShaderResourceView* s = fineRaw_.srv.Get();
        ctx->CSSetShaderResources(7, 1, &s);
        u = fineMed_.uav.Get();
        ctx->CSSetUnorderedAccessViews(1, 1, &u, nullptr);
        ctx->Dispatch((fineW_ + 7) / 8, (fineH_ + 7) / 8, 1);
        UnbindCS();
        final_ = fineMed_.srv.Get();
    }
    if (timing) {
        ctx->End(tq.t1.Get());
        ctx->End(tq.disjoint.Get());
        tq.inFlight = true;
        tqW_ = (tqW_ + 1) % static_cast<int>(tq_.size());
    }
    return true;
}

} // namespace nf
