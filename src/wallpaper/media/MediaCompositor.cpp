#include "wallpaper/media/MediaCompositor.h"

#include <algorithm>
#include <cstring>

#include "core/Win32Util.h"
#include "render/RenderDevice.h"

namespace lwe {
namespace {

using Microsoft::WRL::ComPtr;

constexpr char kCompositorShader[] = R"hlsl(
Texture2D gSource : register(t0);
SamplerState gSampler : register(s0);

cbuffer CompositorConstants : register(b0)
{
    float2 targetSize;
    float2 sourceSize;
    float2 parallax;
    float  zoom;
    float  fitMode;
    float4 tint;
    float  hue;
    float  brightness;
    float2 padding;
};

float3 HueShift(float3 color, float degrees)
{
    if (abs(degrees) < 0.01)
        return color;
    const float3 k = float3(0.57735, 0.57735, 0.57735);
    float a = radians(degrees);
    float c = cos(a);
    return color * c + cross(k, color) * sin(a) + k * dot(k, color) * (1.0 - c);
}

float4 main(float4 position : SV_Position) : SV_Target
{
    float2 ratio = targetSize / sourceSize;
    float scale = fitMode < 0.5 ? max(ratio.x, ratio.y) : min(ratio.x, ratio.y);
    float2 displayed = (fitMode > 1.5 ? targetSize : sourceSize * scale) * zoom;
    // Marge disponible de chaque cote : la parallaxe ne l'excede jamais (aucun bord visible).
    float2 slack = max(displayed - targetSize, 0.0) * 0.5;
    float2 origin = (targetSize - displayed) * 0.5 + slack * parallax * float2(-1.0, 1.0);
    float2 uv = (position.xy - origin) / displayed;
    if (any(uv < 0.0) || any(uv > 1.0))
        return float4(0.0, 0.0, 0.0, 1.0);
    float4 texel = gSource.Sample(gSampler, uv);
    float3 rgb = HueShift(texel.rgb * texel.a, hue) * tint.rgb * brightness;
    return float4(saturate(rgb), 1.0);
}
)hlsl";

struct CompositorConstants {
    float targetSize[2];
    float sourceSize[2];
    float parallax[2];
    float zoom;
    float fitMode;
    float tint[4];
    float hue;
    float brightness;
    float padding[2];
};
static_assert(sizeof(CompositorConstants) % 16 == 0);

bool AssignFloat(const ParamValue& value, float& out) {
    if (const auto* f = std::get_if<float>(&value)) {
        out = *f;
        return true;
    }
    if (const auto* b = std::get_if<bool>(&value)) {
        out = *b ? 1.0f : 0.0f;
        return true;
    }
    return false;
}

}  // namespace

std::vector<ParamDescriptor> MediaAppearance::Descriptors(float defaultParallax) {
    ParamDescriptor fit{"fit", "Ajustement", ParamKind::Choice, 0.0f, 0.0f, 2.0f};
    fit.options = {"Remplir l'écran (rogner)", "Adapter (bandes noires)", "Étirer"};
    return {
        fit,
        {"parallax", "Parallaxe souris", ParamKind::Float, defaultParallax, 0.0f, 2.0f},
        {"hue", "Teinte (degrés)", ParamKind::Float, 0.0f, -180.0f, 180.0f},
        {"tint", "Filtre couleur", ParamKind::Color, Color{1.0f, 1.0f, 1.0f, 1.0f}, 0.0f, 1.0f},
        {"brightness", "Luminosité", ParamKind::Float, 1.0f, 0.0f, 2.0f},
    };
}

bool MediaAppearance::Apply(std::string_view name, const ParamValue& value) {
    if (name == "fit") {
        float f = 0.0f;
        if (AssignFloat(value, f)) {
            fit = std::clamp(static_cast<int>(f + 0.5f), 0, 2);
        }
        return true;
    }
    if (name == "parallax") {
        AssignFloat(value, parallax);
        return true;
    }
    if (name == "hue") {
        AssignFloat(value, hue);
        return true;
    }
    if (name == "brightness") {
        AssignFloat(value, brightness);
        return true;
    }
    if (name == "tint") {
        if (const auto* color = std::get_if<Color>(&value)) {
            tint = *color;
        }
        return true;
    }
    return false;
}

void MediaAppearance::Store(ParamMap& values) const {
    values["fit"] = static_cast<float>(fit);
    values["parallax"] = parallax;
    values["hue"] = hue;
    values["tint"] = tint;
    values["brightness"] = brightness;
}

bool MediaCompositor::Initialize(RenderDevice& device, std::wstring& error) {
    device_ = &device;
    static ComPtr<ID3DBlob> code;  // bytecode independant du device : compile une seule fois
    if (!code) {
        std::string diagnostics;
        code = RenderDevice::CompileShader(kCompositorShader, "lwe_media_compositor", "main", "ps_5_0", diagnostics);
        if (!code) {
            error = L"compilation du compositeur media impossible";
            return false;
        }
    }
    HRESULT hr = device.Device()->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr,
                                                    &pixelShader_);
    if (SUCCEEDED(hr)) {
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = sizeof(CompositorConstants);
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
        error = L"compositeur media : " + HResultToString(hr);
        return false;
    }
    return true;
}

void MediaCompositor::Draw(ID3D11RenderTargetView* target, uint32_t targetWidth, uint32_t targetHeight,
                           ID3D11ShaderResourceView* source, uint32_t sourceWidth, uint32_t sourceHeight,
                           const MediaAppearance& appearance, float parallaxX, float parallaxY) {
    ID3D11DeviceContext* context = device_->Context();
    const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    if (!source || sourceWidth == 0 || sourceHeight == 0) {
        context->ClearRenderTargetView(target, black);
        return;
    }

    CompositorConstants c{};
    c.targetSize[0] = static_cast<float>(targetWidth);
    c.targetSize[1] = static_cast<float>(targetHeight);
    c.sourceSize[0] = static_cast<float>(sourceWidth);
    c.sourceSize[1] = static_cast<float>(sourceHeight);
    const float strength = std::max(0.0f, appearance.parallax);
    c.parallax[0] = std::clamp(parallaxX, -1.0f, 1.0f) * std::min(strength, 1.0f);
    c.parallax[1] = std::clamp(parallaxY, -1.0f, 1.0f) * std::min(strength, 1.0f);
    c.zoom = 1.0f + 0.06f * strength;
    c.fitMode = static_cast<float>(appearance.fit);
    std::copy(appearance.tint.begin(), appearance.tint.end(), c.tint);
    c.hue = appearance.hue;
    c.brightness = appearance.brightness;

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(context->Map(constants_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        std::memcpy(mapped.pData, &c, sizeof(c));
        context->Unmap(constants_.Get(), 0);
    }

    const D3D11_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(targetWidth), static_cast<float>(targetHeight),
                                  0.0f, 1.0f};
    ID3D11Buffer* constantBuffer = constants_.Get();
    ID3D11SamplerState* sampler = sampler_.Get();
    context->OMSetRenderTargets(1, &target, nullptr);
    context->RSSetViewports(1, &viewport);
    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(device_->FullscreenVertexShader(), nullptr, 0);
    context->PSSetShader(pixelShader_.Get(), nullptr, 0);
    context->PSSetConstantBuffers(0, 1, &constantBuffer);
    context->PSSetShaderResources(0, 1, &source);
    context->PSSetSamplers(0, 1, &sampler);
    context->Draw(3, 0);
    ID3D11ShaderResourceView* none = nullptr;
    context->PSSetShaderResources(0, 1, &none);  // la texture video redevient cible de TransferVideoFrame
}

}  // namespace lwe
