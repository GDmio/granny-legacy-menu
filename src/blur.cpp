#include "blur.h"
#include "log.h"
#include "imgui_internal.h"
#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <cmath>
#include <cstring>
#include <initializer_list>

namespace blur {

namespace {

const char* kShader = R"(
cbuffer Params : register(b0) { float2 texel; float grain; float pad; };
Texture2D tex : register(t0);
SamplerState smp : register(s0);
struct V { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

V vs(uint id : SV_VertexID) {
    V o;
    o.uv = float2((id << 1) & 2, id & 2);
    o.pos = float4(o.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}
// 4 bilinear taps = 16-texel box: smooth 2x downsample
float4 ps_down(V i) : SV_Target {
    float2 o = texel * 0.5;
    return (tex.Sample(smp, i.uv + float2(-o.x, -o.y)) + tex.Sample(smp, i.uv + float2(o.x, -o.y)) +
            tex.Sample(smp, i.uv + float2(-o.x, o.y)) + tex.Sample(smp, i.uv + float2(o.x, o.y))) * 0.25;
}
// separable gaussian, `texel` = direction * step
float4 ps_blur(V i) : SV_Target {
    const float w[5] = { 0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216 };
    float4 c = tex.Sample(smp, i.uv) * w[0];
    [unroll] for (int k = 1; k < 5; ++k) {
        c += tex.Sample(smp, i.uv + texel * k) * w[k];
        c += tex.Sample(smp, i.uv - texel * k) * w[k];
    }
    return c;
}
float hash(float2 p) { return frac(sin(dot(p, float2(12.9898, 78.233))) * 43758.5453); }
// upsample to half resolution + grain (per output pixel so it stays fine)
float4 ps_final(V i) : SV_Target {
    float4 c = tex.Sample(smp, i.uv);
    c.rgb += (hash(floor(i.pos.xy)) - 0.5) * grain;
    c.a = 1;
    return c;
}
)";

struct Target {
    ID3D11Texture2D* tex = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
    UINT w = 0, h = 0;
    void Release() {
        if (srv) srv->Release();
        if (rtv) rtv->Release();
        if (tex) tex->Release();
        *this = {};
    }
};

ID3D11Device* g_dev = nullptr;
ID3D11VertexShader* g_vs = nullptr;
ID3D11PixelShader *g_psDown = nullptr, *g_psBlur = nullptr, *g_psFinal = nullptr;
ID3D11SamplerState* g_sampler = nullptr;
ID3D11Buffer* g_cb = nullptr;
ID3D11BlendState* g_blendOff = nullptr;
ID3D11RasterizerState* g_raster = nullptr;
ID3D11DepthStencilState* g_depthOff = nullptr;

ID3D11Texture2D* g_capture = nullptr;  // copy of the back buffer
ID3D11ShaderResourceView* g_captureSrv = nullptr;
Target g_half, g_quarterA, g_quarterB, g_final;
UINT g_w = 0, g_h = 0;
DXGI_FORMAT g_fmt = DXGI_FORMAT_UNKNOWN;
bool g_ready = false, g_requested = false, g_failed = false;

struct Params { float texel[2]; float grain; float pad; };

DXGI_FORMAT TypedFormat(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS: return DXGI_FORMAT_R10G10B10A2_UNORM;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    default: return f;
    }
}

bool MakeTarget(Target& t, UINT w, UINT h) {
    t.Release();
    D3D11_TEXTURE2D_DESC d{};
    d.Width = ImMax(1u, w);
    d.Height = ImMax(1u, h);
    d.MipLevels = 1;
    d.ArraySize = 1;
    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(g_dev->CreateTexture2D(&d, nullptr, &t.tex))) return false;
    if (FAILED(g_dev->CreateRenderTargetView(t.tex, nullptr, &t.rtv))) return false;
    if (FAILED(g_dev->CreateShaderResourceView(t.tex, nullptr, &t.srv))) return false;
    t.w = d.Width;
    t.h = d.Height;
    return true;
}

void ReleaseTargets() {
    if (g_captureSrv) g_captureSrv->Release();
    if (g_capture) g_capture->Release();
    g_captureSrv = nullptr;
    g_capture = nullptr;
    g_half.Release();
    g_quarterA.Release();
    g_quarterB.Release();
    g_final.Release();
    g_w = g_h = 0;
    g_ready = false;
}

bool EnsureTargets(const D3D11_TEXTURE2D_DESC& bb) {
    if (g_capture && bb.Width == g_w && bb.Height == g_h && bb.Format == g_fmt) return true;
    ReleaseTargets();
    D3D11_TEXTURE2D_DESC d = bb;
    d.SampleDesc.Count = 1;
    d.SampleDesc.Quality = 0;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    d.CPUAccessFlags = 0;
    d.MiscFlags = 0;
    d.MipLevels = 1;
    d.ArraySize = 1;
    if (FAILED(g_dev->CreateTexture2D(&d, nullptr, &g_capture))) return false;
    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = TypedFormat(bb.Format);
    sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    sd.Texture2D.MipLevels = 1;
    if (FAILED(g_dev->CreateShaderResourceView(g_capture, &sd, &g_captureSrv))) return false;
    if (!MakeTarget(g_half, bb.Width / 2, bb.Height / 2) || !MakeTarget(g_quarterA, bb.Width / 4, bb.Height / 4) ||
        !MakeTarget(g_quarterB, bb.Width / 4, bb.Height / 4) || !MakeTarget(g_final, bb.Width / 2, bb.Height / 2))
        return false;
    g_w = bb.Width;
    g_h = bb.Height;
    g_fmt = bb.Format;
    return true;
}

ID3DBlob* Compile(const char* entry, const char* profile) {
    ID3DBlob* blob = nullptr;
    ID3DBlob* err = nullptr;
    if (FAILED(D3DCompile(kShader, strlen(kShader), "blur", nullptr, nullptr, entry, profile, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blob, &err))) {
        LOG("blur shader %s failed: %s", entry, err ? static_cast<const char*>(err->GetBufferPointer()) : "?");
        if (err) err->Release();
        return nullptr;
    }
    if (err) err->Release();
    return blob;
}

// Minimal pipeline-state backup so the game's state is untouched after our passes
struct StateBackup {
    ID3D11RenderTargetView* rtv[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
    ID3D11DepthStencilView* dsv = nullptr;
    D3D11_VIEWPORT vp[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
    UINT vpCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    ID3D11VertexShader* vs = nullptr;
    ID3D11PixelShader* ps = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
    ID3D11SamplerState* smp = nullptr;
    ID3D11Buffer* cb = nullptr;
    ID3D11InputLayout* il = nullptr;
    D3D11_PRIMITIVE_TOPOLOGY topo{};
    ID3D11BlendState* blend = nullptr;
    float blendFactor[4]{};
    UINT sampleMask = 0;
    ID3D11RasterizerState* rs = nullptr;
    ID3D11DepthStencilState* ds = nullptr;
    UINT stencilRef = 0;

    void Save(ID3D11DeviceContext* c) {
        c->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtv, &dsv);
        c->RSGetViewports(&vpCount, vp);
        c->VSGetShader(&vs, nullptr, nullptr);
        c->PSGetShader(&ps, nullptr, nullptr);
        c->PSGetShaderResources(0, 1, &srv);
        c->PSGetSamplers(0, 1, &smp);
        c->PSGetConstantBuffers(0, 1, &cb);
        c->IAGetInputLayout(&il);
        c->IAGetPrimitiveTopology(&topo);
        c->OMGetBlendState(&blend, blendFactor, &sampleMask);
        c->RSGetState(&rs);
        c->OMGetDepthStencilState(&ds, &stencilRef);
    }
    void Restore(ID3D11DeviceContext* c) {
        c->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtv, dsv);
        c->RSSetViewports(vpCount, vp);
        c->VSSetShader(vs, nullptr, 0);
        c->PSSetShader(ps, nullptr, 0);
        c->PSSetShaderResources(0, 1, &srv);
        c->PSSetSamplers(0, 1, &smp);
        c->PSSetConstantBuffers(0, 1, &cb);
        c->IASetInputLayout(il);
        c->IASetPrimitiveTopology(topo);
        c->OMSetBlendState(blend, blendFactor, sampleMask);
        c->RSSetState(rs);
        c->OMSetDepthStencilState(ds, stencilRef);
        for (auto*& r : rtv) if (r) r->Release();
        if (dsv) dsv->Release();
        if (vs) vs->Release();
        if (ps) ps->Release();
        if (srv) srv->Release();
        if (smp) smp->Release();
        if (cb) cb->Release();
        if (il) il->Release();
        if (blend) blend->Release();
        if (rs) rs->Release();
        if (ds) ds->Release();
    }
};

void Pass(ID3D11DeviceContext* c, ID3D11PixelShader* ps, ID3D11ShaderResourceView* src, Target& dst, float tx, float ty, float grain) {
    ID3D11ShaderResourceView* nullSrv = nullptr;
    c->PSSetShaderResources(0, 1, &nullSrv);  // avoid read/write hazards between passes
    c->OMSetRenderTargets(1, &dst.rtv, nullptr);
    D3D11_VIEWPORT vp{ 0, 0, static_cast<float>(dst.w), static_cast<float>(dst.h), 0, 1 };
    c->RSSetViewports(1, &vp);
    D3D11_MAPPED_SUBRESOURCE m{};
    if (SUCCEEDED(c->Map(g_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        Params p{ { tx, ty }, grain, 0.f };
        memcpy(m.pData, &p, sizeof(p));
        c->Unmap(g_cb, 0);
    }
    c->PSSetShader(ps, nullptr, 0);
    c->PSSetShaderResources(0, 1, &src);
    c->Draw(3, 0);
}

} // namespace

bool Init(ID3D11Device* device) {
    g_dev = device;
    ID3DBlob* vs = Compile("vs", "vs_4_0");
    ID3DBlob* down = Compile("ps_down", "ps_4_0");
    ID3DBlob* bl = Compile("ps_blur", "ps_4_0");
    ID3DBlob* fin = Compile("ps_final", "ps_4_0");
    bool ok = vs && down && bl && fin;
    if (ok) {
        ok &= SUCCEEDED(device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &g_vs));
        ok &= SUCCEEDED(device->CreatePixelShader(down->GetBufferPointer(), down->GetBufferSize(), nullptr, &g_psDown));
        ok &= SUCCEEDED(device->CreatePixelShader(bl->GetBufferPointer(), bl->GetBufferSize(), nullptr, &g_psBlur));
        ok &= SUCCEEDED(device->CreatePixelShader(fin->GetBufferPointer(), fin->GetBufferSize(), nullptr, &g_psFinal));
    }
    for (ID3DBlob* b : { vs, down, bl, fin }) if (b) b->Release();

    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    ok &= SUCCEEDED(device->CreateSamplerState(&sd, &g_sampler));
    D3D11_BUFFER_DESC bd{ sizeof(Params), D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
    ok &= SUCCEEDED(device->CreateBuffer(&bd, nullptr, &g_cb));
    D3D11_BLEND_DESC blend{};
    blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    ok &= SUCCEEDED(device->CreateBlendState(&blend, &g_blendOff));
    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    ok &= SUCCEEDED(device->CreateRasterizerState(&rd, &g_raster));
    D3D11_DEPTH_STENCIL_DESC dd{};
    ok &= SUCCEEDED(device->CreateDepthStencilState(&dd, &g_depthOff));
    g_failed = !ok;
    LOG("blur::Init %s", ok ? "ok" : "failed");
    return ok;
}

void Shutdown() {
    ReleaseTargets();
    for (IUnknown* p : std::initializer_list<IUnknown*>{ g_vs, g_psDown, g_psBlur, g_psFinal, g_sampler, g_cb, g_blendOff, g_raster, g_depthOff })
        if (p) p->Release();
    g_vs = nullptr; g_psDown = g_psBlur = g_psFinal = nullptr; g_sampler = nullptr; g_cb = nullptr;
    g_blendOff = nullptr; g_raster = nullptr; g_depthOff = nullptr;
}

void Invalidate() { ReleaseTargets(); }

void Request() { g_requested = true; }
bool Requested() { return g_requested; }

void Process(ID3D11DeviceContext* c, IDXGISwapChain* sc, float strength, float grain) {
    const bool wanted = g_requested;
    g_requested = false;
    if (!wanted || g_failed || !g_vs) return;
    ID3D11Texture2D* back = nullptr;
    if (FAILED(sc->GetBuffer(0, IID_PPV_ARGS(&back))) || !back) return;
    D3D11_TEXTURE2D_DESC bd{};
    back->GetDesc(&bd);
    if (!EnsureTargets(bd)) {
        back->Release();
        g_failed = true;
        LOG("blur: could not create targets (%ux%u fmt %d)", bd.Width, bd.Height, bd.Format);
        return;
    }
    if (bd.SampleDesc.Count > 1) c->ResolveSubresource(g_capture, 0, back, 0, TypedFormat(bd.Format));
    else c->CopyResource(g_capture, back);
    back->Release();

    StateBackup st;
    st.Save(c);
    c->IASetInputLayout(nullptr);
    c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    c->VSSetShader(g_vs, nullptr, 0);
    c->PSSetSamplers(0, 1, &g_sampler);
    c->PSSetConstantBuffers(0, 1, &g_cb);
    const float bf[4]{};
    c->OMSetBlendState(g_blendOff, bf, 0xFFFFFFFF);
    c->RSSetState(g_raster);
    c->OMSetDepthStencilState(g_depthOff, 0);

    strength = ImClamp(strength, 0.f, 1.f);
    Pass(c, g_psDown, g_captureSrv, g_half, 1.f / g_w, 1.f / g_h, 0.f);
    Pass(c, g_psDown, g_half.srv, g_quarterA, 1.f / g_half.w, 1.f / g_half.h, 0.f);
    const int iterations = 1 + static_cast<int>(std::round(strength * 3.f));
    const float stepScale = 1.f + strength * 1.5f;
    for (int i = 0; i < iterations; ++i) {
        Pass(c, g_psBlur, g_quarterA.srv, g_quarterB, stepScale / g_quarterA.w, 0.f, 0.f);
        Pass(c, g_psBlur, g_quarterB.srv, g_quarterA, 0.f, stepScale / g_quarterA.h, 0.f);
    }
    Pass(c, g_psFinal, g_quarterA.srv, g_final, 0.f, 0.f, grain);
    ID3D11ShaderResourceView* nullSrv = nullptr;
    c->PSSetShaderResources(0, 1, &nullSrv);
    st.Restore(c);
    g_ready = true;
}

ImTextureID Texture() { return g_ready && g_final.srv ? (ImTextureID)(intptr_t)g_final.srv : (ImTextureID)0; }

} // namespace blur
