#include "render/TransitionRenderer.h"

#include <algorithm>
#include <cstring>

#include "core/StringUtil.h"
#include "core/Win32Util.h"
#include "render/RenderDevice.h"

namespace lwe {
namespace {

using Microsoft::WRL::ComPtr;

constexpr char kTransitionShader[] = R"hlsl(
Texture2D gFrom : register(t0);
Texture2D gTo : register(t1);
SamplerState gSampler : register(s0);

cbuffer TransitionConstants : register(b0)
{
    float2 resolution;
    float  progress;
    float  type;
    float  seed;
    float3 padding;
};

float Hash(float2 p)
{
    p = frac(p * float2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return frac(p.x * p.y);
}

float Noise(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    float2 u = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(Hash(i), Hash(i + float2(1, 0)), u.x), lerp(Hash(i + float2(0, 1)), Hash(i + float2(1, 1)), u.x), u.y);
}

float Fbm(float2 p)
{
    float value = 0.0;
    float amplitude = 0.5;
    for (int i = 0; i < 4; ++i)
    {
        value += amplitude * Noise(p);
        p = p * 2.03 + 17.1;
        amplitude *= 0.5;
    }
    return value / 0.9375;
}

float4 SampleZoomed(Texture2D tex, float2 uv, float zoom)
{
    return tex.Sample(gSampler, (uv - 0.5) / zoom + 0.5);
}

float4 main(float4 position : SV_Position) : SV_Target
{
    float2 uv = position.xy / resolution;
    float p = saturate(progress);
    float eased = p * p * (3.0 - 2.0 * p);
    float aspect = resolution.x / max(resolution.y, 1.0);
    float4 from = gFrom.Sample(gSampler, uv);
    float4 to = gTo.Sample(gSampler, uv);

    if (type < 0.5)  // Fondu
        return lerp(from, to, eased);

    if (type < 1.5)  // Dissolve : bruit fractal, liseré lumineux sur le front
    {
        float n = Fbm(float2(uv.x * aspect, uv.y) * 5.0 + seed * 91.7);
        float edge = 0.08;
        float t = p * (1.0 + 2.0 * edge) - edge;
        float mask = smoothstep(n - edge, n + edge, t);
        float glow = (1.0 - abs(mask * 2.0 - 1.0)) * 0.35;
        return lerp(from, to, mask) + glow * float4(1.0, 0.9, 0.75, 0.0);
    }

    if (type < 2.5)  // Balayage : direction tiree au hasard, bord adouci
    {
        float angle = seed * 6.2831853;
        float2 dir = float2(cos(angle), sin(angle));
        float2 centered = (uv - 0.5) * float2(aspect, 1.0);
        float extent = 0.5 * (abs(dir.x) * aspect + abs(dir.y));
        float d = dot(centered, dir) / extent * 0.5 + 0.5;  // 0 -> 1 le long du balayage
        float softness = 0.06;
        float mask = smoothstep(d - softness, d + softness, eased * (1.0 + 2.0 * softness) - softness);
        return lerp(from, to, mask);
    }

    // Zoom-fondu : l'ancien fond avance vers l'ecran, le nouveau arrive de plus loin
    float4 zoomedFrom = SampleZoomed(gFrom, uv, 1.0 + 0.25 * eased);
    float4 zoomedTo = SampleZoomed(gTo, uv, 0.85 + 0.15 * eased);
    return lerp(zoomedFrom, zoomedTo, eased);
}
)hlsl";

struct TransitionConstants {
    float resolution[2];
    float progress;
    float type;
    float seed;
    float padding[3];
};
static_assert(sizeof(TransitionConstants) % 16 == 0);

// Bytecode partage par toutes les surfaces (et tous les devices successifs).
ComPtr<ID3DBlob> TransitionBytecode(std::string& diagnostics) {
    static ComPtr<ID3DBlob> blob;
    if (!blob) {
        blob = RenderDevice::CompileShader(kTransitionShader, "lwe_transition", "main", "ps_5_0", diagnostics);
    }
    return blob;
}

}  // namespace

bool TransitionRenderer::Initialize(RenderDevice& device, std::wstring& error) {
    device_ = &device;
    std::string diagnostics;
    const ComPtr<ID3DBlob> code = TransitionBytecode(diagnostics);
    if (!code) {
        error = L"shader de transition : " + Utf8ToWide(diagnostics);
        return false;
    }
    HRESULT hr = device.Device()->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader_);
    if (SUCCEEDED(hr)) {
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = sizeof(TransitionConstants);
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        hr = device.Device()->CreateBuffer(&desc, nullptr, &constants_);
    }
    if (SUCCEEDED(hr)) {
        D3D11_SAMPLER_DESC sampler{};
        sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.MaxLOD = D3D11_FLOAT32_MAX;
        hr = device.Device()->CreateSamplerState(&sampler, &sampler_);
    }
    if (FAILED(hr)) {
        error = L"ressources de transition : " + HResultToString(hr);
        return false;
    }
    return true;
}

void TransitionRenderer::Draw(ID3D11RenderTargetView* target, ID3D11ShaderResourceView* from,
                              ID3D11ShaderResourceView* to, uint32_t width, uint32_t height, float progress,
                              TransitionType type, float seed) {
    if (!shader_ || !target) {
        return;
    }
    ID3D11DeviceContext* context = device_->Context();
    TransitionConstants c{};
    c.resolution[0] = static_cast<float>(std::max<uint32_t>(1, width));
    c.resolution[1] = static_cast<float>(std::max<uint32_t>(1, height));
    c.progress = std::clamp(progress, 0.0f, 1.0f);
    c.type = static_cast<float>(type);
    c.seed = seed;
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(context->Map(constants_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        std::memcpy(mapped.pData, &c, sizeof(c));
        context->Unmap(constants_.Get(), 0);
    }

    const D3D11_VIEWPORT viewport{0.0f, 0.0f, c.resolution[0], c.resolution[1], 0.0f, 1.0f};
    ID3D11ShaderResourceView* views[2] = {from, to};
    ID3D11Buffer* buffer = constants_.Get();
    ID3D11SamplerState* sampler = sampler_.Get();
    context->OMSetRenderTargets(1, &target, nullptr);
    context->RSSetViewports(1, &viewport);
    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(device_->FullscreenVertexShader(), nullptr, 0);
    context->PSSetShader(shader_.Get(), nullptr, 0);
    context->PSSetShaderResources(0, 2, views);
    context->PSSetSamplers(0, 1, &sampler);
    context->PSSetConstantBuffers(0, 1, &buffer);
    context->Draw(3, 0);
    // Les textures redeviennent des cibles de rendu a l'image suivante : on les delie tout de suite.
    ID3D11ShaderResourceView* none[2] = {nullptr, nullptr};
    context->PSSetShaderResources(0, 2, none);
}

}  // namespace lwe
