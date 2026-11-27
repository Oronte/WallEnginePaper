#include "render/EffectsRenderer.h"

#include <algorithm>
#include <cstring>

#include "core/Log.h"
#include "core/StringUtil.h"
#include "core/Win32Util.h"
#include "render/RenderDevice.h"

namespace lwe {
namespace {

using Microsoft::WRL::ComPtr;

constexpr char kEffectsShader[] = R"hlsl(
Texture2D gSource : register(t0);
Texture2D gExtra : register(t1);
SamplerState gSampler : register(s0);

cbuffer FxConstants : register(b0)
{
    float2 resolution;
    float  time;
    float  strength;
    float4 params;          // propre a la passe (direction du flou en texels...)
    float4 mouse;           // xy pixels (haut gauche), inside, reaction a la souris
    float  bass;
    float  beat;
    float  overlayType;
    float  overlayIntensity;
    float  overlayAudio;
    float  rippleStrength;
    float  rippleCount;
    float  trailCount;
    float4 trailColor;
    float4 ripples[8];      // xy pixels, age (s)
    float4 trail[24];       // xy pixels, age (s)
};

static const float kPi = 3.14159265;

float Hash(float2 p)
{
    p = frac(p * float2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return frac(p.x * p.y);
}

float2 Hash2(float2 p)
{
    float n = Hash(p);
    return float2(n, Hash(p + n * 17.17));
}

float Noise(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    float2 u = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(Hash(i), Hash(i + float2(1, 0)), u.x), lerp(Hash(i + float2(0, 1)), Hash(i + float2(1, 1)), u.x), u.y);
}

float2 Uv(float4 position) { return position.xy / resolution; }
float Scale() { return resolution.y / 1080.0; }   // tailles definies pour 1080p

// --- Post-effets ----------------------------------------------------------------------------------

float4 PsChromatic(float4 position : SV_Position) : SV_Target
{
    float2 uv = Uv(position);
    float2 dir = (uv - 0.5) * strength * 0.018;
    float r = gSource.Sample(gSampler, uv + dir).r;
    float g = gSource.Sample(gSampler, uv).g;
    float b = gSource.Sample(gSampler, uv - dir).b;
    return float4(r, g, b, 1.0);
}

// Flou gaussien separable (9 echantillons via filtrage bilineaire) ; params.xy = pas en uv.
float4 PsBlur(float4 position : SV_Position) : SV_Target
{
    float2 uv = Uv(position);
    float2 step = params.xy;
    float3 color = gSource.Sample(gSampler, uv).rgb * 0.2270270270;
    color += gSource.Sample(gSampler, uv + step * 1.3846153846).rgb * 0.3162162162;
    color += gSource.Sample(gSampler, uv - step * 1.3846153846).rgb * 0.3162162162;
    color += gSource.Sample(gSampler, uv + step * 3.2307692308).rgb * 0.0702702703;
    color += gSource.Sample(gSampler, uv - step * 3.2307692308).rgb * 0.0702702703;
    return float4(color, 1.0);
}

// Bloom : zones lumineuses (seuil doux) a mi-resolution.
float4 PsBright(float4 position : SV_Position) : SV_Target
{
    float3 c = gSource.Sample(gSampler, Uv(position)).rgb;
    float luma = dot(c, float3(0.2126, 0.7152, 0.0722));
    float knee = smoothstep(0.55, 0.95, luma);
    return float4(c * knee, 1.0);
}

float4 PsBloomAdd(float4 position : SV_Position) : SV_Target
{
    float2 uv = Uv(position);
    float3 base = gSource.Sample(gSampler, uv).rgb;
    float3 glow = gExtra.Sample(gSampler, uv).rgb;
    return float4(base + glow * strength * 1.6, 1.0);
}

float4 PsVignette(float4 position : SV_Position) : SV_Target
{
    float2 uv = Uv(position);
    float3 c = gSource.Sample(gSampler, uv).rgb;
    float2 d = (uv - 0.5) * float2(resolution.x / resolution.y, 1.0);
    float v = smoothstep(1.05, 0.25, length(d));
    return float4(c * lerp(1.0, v, strength), 1.0);
}

float4 PsGrain(float4 position : SV_Position) : SV_Target
{
    float3 c = gSource.Sample(gSampler, Uv(position)).rgb;
    float2 cell = floor(position.xy / max(1.0, Scale()));
    float n = Hash(cell + frac(time * 7.13) * 917.0) - 0.5;
    float luma = dot(c, float3(0.299, 0.587, 0.114));
    // Grain plus visible dans les tons moyens, comme un film.
    float amount = strength * 0.16 * (0.4 + 0.6 * (1.0 - abs(luma * 2.0 - 1.0)));
    return float4(saturate(c + n * amount), 1.0);
}

float4 PsScanlines(float4 position : SV_Position) : SV_Target
{
    float3 c = gSource.Sample(gSampler, Uv(position)).rgb;
    float period = max(2.0, 3.0 * Scale());
    float scan = 0.5 + 0.5 * cos(position.y / period * 2.0 * kPi);
    float mask = 1.0 - strength * 0.45 * (1.0 - scan);
    // Leger masque RVB vertical (moniteur cathodique).
    float3 rgbMask = float3(1.0, 1.0, 1.0) - strength * 0.08 * float3(fmod(position.x, 3.0) < 1.0 ? 0 : 1,
                                                                    fmod(position.x + 1.0, 3.0) < 1.0 ? 0 : 1,
                                                                    fmod(position.x + 2.0, 3.0) < 1.0 ? 0 : 1);
    return float4(c * mask * rgbMask, 1.0);
}

// --- Passe finale : ondulations, particules, trainee -----------------------------------------------

// Pluie sur vitre. Cellules carrees ; chaque cellule porte au plus une goutte qui ne deborde jamais (pas de
// coutures). Renvoie (normale xy de la goutte, masque, bord) : la goutte agit comme une lentille.
float4 RainLayer(float2 uv, float aspect, float density, float t, float seed, float minRadius)
{
    float2 p = float2(uv.x * aspect, uv.y) * density;
    float column = floor(p.x);
    // Chaque colonne glisse a sa propre vitesse : les gouttes semblent couler sur la vitre.
    p.y -= t * (0.25 + 0.75 * Hash(float2(column, seed))) + Hash(float2(column, seed + 3.0)) * 9.0;
    float2 cell = floor(p);
    float2 f = frac(p) - 0.5;
    float rnd = Hash(cell + seed);
    float radius = minRadius + 0.12 * frac(rnd * 31.3);
    float2 center = (Hash2(cell + seed * 1.7) - 0.5) * (0.9 - 2.0 * radius);
    float2 d = (f - center) / float2(1.0, 1.25);          // goutte legerement allongee
    float r = length(d) / radius;
    float mask = smoothstep(1.0, 0.85, r) * step(0.45, rnd);
    return float4(d / radius * mask, mask, smoothstep(0.55, 1.0, r) * mask);
}

float4 Rain(float2 uv, float aspect, float audio, float wipe)
{
    float t = time * (0.08 + 0.12 * audio);
    float4 big = RainLayer(uv, aspect, 7.0, t, 1.0, 0.16);
    float4 medium = RainLayer(uv + 0.37, aspect, 13.0, t * 1.3, 7.0, 0.12);
    float4 fine = RainLayer(uv + 0.71, aspect, 34.0, 0.0, 13.0, 0.1);  // gouttelettes immobiles
    float4 drops = big;
    drops = medium.z > drops.z ? medium : drops;
    drops = fine.z > drops.z ? fine * 0.8 : drops;
    return drops * wipe;
}

float Snow(float2 uv, float aspect, float audio, float2 mouseUv, float mouseInfluence)
{
    float acc = 0.0;
    float2 q0 = float2(uv.x * aspect, uv.y);
    float2 m = float2(mouseUv.x * aspect, mouseUv.y);
    [unroll] for (int layer = 0; layer < 4; ++layer)
    {
        float depth = 1.0 + layer;
        float scale = 4.0 + depth * 4.0;
        float2 q = q0;
        q.y -= time * (0.03 + 0.025 * depth);
        q.x += sin(time * 0.4 + q.y * 2.0 + layer) * 0.02 * depth;
        // La souris ecarte les flocons.
        float2 away = q0 - m;
        q += normalize(away + 1e-5) * exp(-dot(away, away) / 0.015) * 0.06 * mouseInfluence;
        float2 g = q * scale;
        float2 cell = floor(g);
        float rnd = Hash(cell + layer * 17.0);
        float2 f = frac(g) - 0.5 - (Hash2(cell + 3.1) - 0.5) * 0.7;
        float size = (0.04 + 0.06 * frac(rnd * 7.3)) * (1.0 + audio * 0.6);
        acc += smoothstep(size, size * 0.2, length(f)) * step(0.45, rnd) * (0.35 + 0.65 * depth / 4.0);
    }
    return saturate(acc);
}

float3 Fireflies(float2 uv, float aspect, float audio, float2 mouseUv, float mouseInfluence)
{
    float3 glow = 0;
    float2 p = float2(uv.x * aspect, uv.y);
    float2 m = float2(mouseUv.x * aspect, mouseUv.y);
    [loop] for (int i = 0; i < 28; ++i)
    {
        float s = i * 7.31;
        float2 pos = float2(0.5 * aspect + 0.48 * aspect * sin(time * (0.05 + 0.03 * Hash(float2(s, 1))) + s),
                            0.55 + 0.4 * sin(time * (0.07 + 0.04 * Hash(float2(s, 2))) + s * 1.7));
        pos += 0.04 * float2(Noise(float2(time * 0.3, s)), Noise(float2(s, time * 0.3))) - 0.02;
        pos = lerp(pos, m, 0.18 * mouseInfluence * exp(-length(pos - m) * 3.0));   // attirees par le curseur
        float d = length(p - pos);
        float pulse = 0.5 + 0.5 * sin(time * (1.5 + Hash(float2(s, 3))) + s * 4.0);
        pulse = saturate(pulse + audio * 0.8);
        float size = 0.004 + 0.003 * Hash(float2(s, 4));
        glow += float3(1.0, 0.86, 0.42) * (exp(-d * d / (size * size)) * 1.2 + exp(-d / (size * 6.0)) * 0.18) * pulse;
    }
    return glow;
}

float3 Dust(float2 uv, float aspect, float audio, float2 mouseUv, float mouseInfluence)
{
    float2 p = float2(uv.x * aspect, uv.y);
    float2 m = float2(mouseUv.x * aspect, mouseUv.y);
    float2 away = p - m;
    float swirl = exp(-dot(away, away) / 0.02) * mouseInfluence;
    p += float2(-away.y, away.x) * swirl * 0.3;
    // Rayons de lumiere diagonaux qui revelent la poussiere.
    float beam = smoothstep(0.35, 0.95, Noise(float2((uv.x + uv.y * 0.6) * 2.5, time * 0.04)));
    float acc = 0.0;
    [unroll] for (int layer = 0; layer < 3; ++layer)
    {
        float scale = 10.0 + layer * 9.0;
        float2 q = p + float2(time * (0.006 + layer * 0.004), -time * (0.008 + layer * 0.005));
        float2 g = q * scale;
        float2 cell = floor(g);
        float rnd = Hash(cell + layer * 5.0);
        float2 f = frac(g) - 0.5 - (Hash2(cell) - 0.5) * 0.7;
        float size = 0.035 + 0.05 * frac(rnd * 17.0) * (1.0 - layer * 0.25);
        float twinkle = 0.55 + 0.45 * sin(time * (0.5 + rnd * 2.0) + rnd * 40.0);
        acc += exp(-dot(f, f) / (size * size)) * step(0.62, rnd) * twinkle * (1.0 - layer * 0.2);
    }
    float motes = acc * (0.25 + 1.4 * beam) * (1.0 + audio);
    return float3(1.0, 0.94, 0.82) * motes + float3(1.0, 0.9, 0.7) * beam * 0.035;
}

float SegmentDistance(float2 p, float2 a, float2 b)
{
    float2 ab = b - a;
    float t = saturate(dot(p - a, ab) / max(dot(ab, ab), 1e-4));
    return length(p - (a + ab * t));
}

float4 PsFinal(float4 position : SV_Position) : SV_Target
{
    float2 uv = Uv(position);
    float aspect = resolution.x / resolution.y;
    float scale = Scale();
    float audio = saturate(bass * 0.7 + beat * 0.5) * overlayAudio;
    float2 mouseUv = mouse.xy / resolution;
    float mouseInfluence = mouse.z * mouse.w;

    // Ondulations : deplacement radial de l'image autour de chaque clic.
    float2 offset = 0;
    float shade = 0;
    for (int i = 0; i < (int)rippleCount; ++i)
    {
        float2 d = position.xy - ripples[i].xy;
        float dist = length(d);
        float age = ripples[i].z;
        float radius = age * 600.0 * scale;
        float ring = dist - radius;
        float width = 70.0 * scale;
        float envelope = exp(-ring * ring / (width * width)) * pow(saturate(1.0 - age / 1.6), 1.5);
        float wave = sin(ring / (14.0 * scale));
        offset += (d / max(dist, 1.0)) * wave * envelope * 18.0 * scale * rippleStrength;
        shade += wave * envelope * 0.06 * rippleStrength;
    }

    float4 rain = 0;
    if (overlayType > 0.5 && overlayType < 1.5)
    {
        float wipe = 1.0 - mouseInfluence * smoothstep(0.12, 0.05, length((uv - mouseUv) * float2(aspect, 1.0)));
        rain = Rain(uv, aspect, audio, wipe) * overlayIntensity;
        offset += rain.xy * 26.0 * scale;   // la goutte agit comme une lentille
    }

    float3 color = gSource.Sample(gSampler, uv + offset / resolution).rgb + shade;
    if (overlayType > 0.5 && overlayType < 1.5)
    {
        float2 n = rain.xy / max(rain.z, 1e-3);
        float highlight = pow(saturate(dot(n, normalize(float2(0.45, 0.65)))), 6.0) * rain.z;  // reflet
        color = color * (1.0 - rain.w * 0.45) + highlight * 0.35;                              // bord sombre
    }
    else if (overlayType > 1.5 && overlayType < 2.5)
    {
        color = lerp(color, float3(0.96, 0.97, 1.0), Snow(uv, aspect, audio, mouseUv, mouseInfluence) * overlayIntensity);
    }
    else if (overlayType > 2.5 && overlayType < 3.5)
    {
        color += Fireflies(uv, aspect, audio, mouseUv, mouseInfluence) * overlayIntensity;
    }
    else if (overlayType > 3.5)
    {
        color += Dust(uv, aspect, audio, mouseUv, mouseInfluence) * overlayIntensity;
    }

    // Trainee : lueur le long des derniers points du curseur, plus fine et plus pale avec l'age.
    float glow = 0;
    for (int j = 0; j + 1 < (int)trailCount; ++j)
    {
        float age = trail[j].z;
        float life = saturate(1.0 - age / 0.45);
        float d = SegmentDistance(position.xy, trail[j].xy, trail[j + 1].xy);
        float w = (3.0 + 9.0 * life) * scale;
        glow = max(glow, exp(-d * d / (w * w)) * life);
    }
    color += trailColor.rgb * glow * 0.9 + glow * glow * 0.25;
    return float4(saturate(color), 1.0);
}
)hlsl";

constexpr const char* kEntryPoints[] = {"PsChromatic", "PsBlur",      "PsBright",    "PsBloomAdd",
                                        "PsVignette",  "PsGrain",     "PsScanlines", "PsFinal"};

struct FxConstants {
    float resolution[2];
    float time;
    float strength;
    float params[4];
    float mouse[4];
    float bass;
    float beat;
    float overlayType;
    float overlayIntensity;
    float overlayAudio;
    float rippleStrength;
    float rippleCount;
    float trailCount;
    float trailColor[4];
    float ripples[8][4];
    float trail[24][4];
};
static_assert(sizeof(FxConstants) % 16 == 0);
static_assert(RippleSet::kMaxRipples <= 8 && CursorTrail::kMaxPoints <= 24);

// Bytecode compile une seule fois pour toute l'application.
const std::array<ComPtr<ID3DBlob>, 8>& EffectBytecode(std::string& diagnostics) {
    static std::array<ComPtr<ID3DBlob>, 8> blobs;
    static bool compiled = false;
    if (!compiled) {
        compiled = true;
        for (size_t i = 0; i < blobs.size(); ++i) {
            std::string messages;
            blobs[i] = RenderDevice::CompileShader(kEffectsShader, "lwe_effects", kEntryPoints[i], "ps_5_0", messages);
            if (!blobs[i]) {
                diagnostics += messages;
            }
        }
    }
    return blobs;
}

}  // namespace

bool EffectsRenderer::Prepare(RenderDevice& device, uint32_t width, uint32_t height, std::wstring& error) {
    device_ = &device;
    if (!shaders_[0]) {
        std::string diagnostics;
        const auto& blobs = EffectBytecode(diagnostics);
        for (size_t i = 0; i < kPassCount; ++i) {
            if (!blobs[i] || FAILED(device.Device()->CreatePixelShader(blobs[i]->GetBufferPointer(),
                                                                       blobs[i]->GetBufferSize(), nullptr, &shaders_[i]))) {
                error = L"shaders d'effets : " + Utf8ToWide(diagnostics);
                return false;
            }
        }
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = sizeof(FxConstants);
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        D3D11_SAMPLER_DESC sampler{};
        sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.MaxLOD = D3D11_FLOAT32_MAX;
        if (FAILED(device.Device()->CreateBuffer(&desc, nullptr, &constants_)) ||
            FAILED(device.Device()->CreateSamplerState(&sampler, &sampler_))) {
            error = L"ressources des effets indisponibles";
            return false;
        }
    }
    if (width_ != width || height_ != height || !IsPrepared()) {
        Release();
        width_ = width;
        height_ = height;
        const uint32_t halfWidth = std::max<uint32_t>(1, width / 2);
        const uint32_t halfHeight = std::max<uint32_t>(1, height / 2);
        if (!scene_[0].Create(device.Device(), width, height) || !scene_[1].Create(device.Device(), width, height) ||
            !scene_[2].Create(device.Device(), width, height) ||
            !half_[0].Create(device.Device(), halfWidth, halfHeight) ||
            !half_[1].Create(device.Device(), halfWidth, halfHeight)) {
            Release();
            error = L"textures des effets : memoire video insuffisante";
            return false;
        }
        const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        device.Context()->ClearRenderTargetView(scene_[0].rtv.Get(), black);
    }
    return true;
}

void EffectsRenderer::Release() noexcept {
    for (auto& texture : scene_) {
        texture.Reset();
    }
    for (auto& texture : half_) {
        texture.Reset();
    }
    width_ = height_ = 0;
}

void EffectsRenderer::Draw(Pass pass, ID3D11RenderTargetView* target, uint32_t width, uint32_t height,
                           ID3D11ShaderResourceView* source, ID3D11ShaderResourceView* extra, float strength,
                           const std::array<float, 4>& params) {
    ID3D11DeviceContext* context = device_->Context();
    FxConstants c{};
    c.resolution[0] = static_cast<float>(width);
    c.resolution[1] = static_cast<float>(height);
    c.time = frame_->time;
    c.strength = strength;
    std::copy(params.begin(), params.end(), c.params);
    c.mouse[0] = frame_->mouseX * static_cast<float>(width) / static_cast<float>(std::max<uint32_t>(1, width_));
    c.mouse[1] = frame_->mouseY * static_cast<float>(height) / static_cast<float>(std::max<uint32_t>(1, height_));
    c.mouse[2] = frame_->mouseInside ? 1.0f : 0.0f;
    c.mouse[3] = settings_->overlayMouse;
    c.bass = frame_->bass;
    c.beat = frame_->beat;
    c.overlayType = static_cast<float>(settings_->overlay);
    c.overlayIntensity = settings_->overlayIntensity;
    c.overlayAudio = settings_->overlayAudio;
    c.rippleStrength = settings_->rippleStrength;
    if (pass == kFinal) {
        if (frame_->ripples && settings_->ripples) {
            const auto& ripples = frame_->ripples->Items();
            c.rippleCount = static_cast<float>(ripples.size());
            for (size_t i = 0; i < ripples.size(); ++i) {
                c.ripples[i][0] = ripples[i].x;
                c.ripples[i][1] = ripples[i].y;
                c.ripples[i][2] = ripples[i].age;
            }
        }
        if (frame_->trail && settings_->trail) {
            const auto& points = frame_->trail->Points();
            c.trailCount = static_cast<float>(points.size());
            for (size_t i = 0; i < points.size(); ++i) {
                c.trail[i][0] = points[i].x;
                c.trail[i][1] = points[i].y;
                c.trail[i][2] = points[i].age;
            }
        }
        std::copy(trailColor_.begin(), trailColor_.end(), c.trailColor);
    }
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(context->Map(constants_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        std::memcpy(mapped.pData, &c, sizeof(c));
        context->Unmap(constants_.Get(), 0);
    }
    const D3D11_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f};
    ID3D11ShaderResourceView* views[2] = {source, extra};
    ID3D11Buffer* buffer = constants_.Get();
    ID3D11SamplerState* sampler = sampler_.Get();
    // Delier d'abord les entrees : la cible de cette passe peut etre l'entree de la precedente.
    ID3D11ShaderResourceView* none[2] = {nullptr, nullptr};
    context->PSSetShaderResources(0, 2, none);
    context->OMSetRenderTargets(1, &target, nullptr);
    context->RSSetViewports(1, &viewport);
    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(device_->FullscreenVertexShader(), nullptr, 0);
    context->PSSetShader(shaders_[pass].Get(), nullptr, 0);
    context->PSSetShaderResources(0, 2, views);
    context->PSSetSamplers(0, 1, &sampler);
    context->PSSetConstantBuffers(0, 1, &buffer);
    context->Draw(3, 0);
    context->PSSetShaderResources(0, 2, none);
}

void EffectsRenderer::Apply(ID3D11RenderTargetView* output, const EffectSettings& settings, const EffectFrame& frame) {
    if (!IsPrepared() || !output) {
        return;
    }
    settings_ = &settings;
    frame_ = &frame;
    trailColor_ = {settings.trailColor[0], settings.trailColor[1], settings.trailColor[2], 1.0f};
    const std::array<float, 4> noParams{};

    // `current` contient l'image a traiter : la base (0) puis les textures 1 et 2 en alternance.
    size_t current = 0;
    const auto next = [&]() -> size_t { return current == 1 ? 2 : 1; };
    for (const EffectId effect : settings.order) {
        const float strength = settings.Strength(effect);
        if (strength <= 0.0f) {
            continue;  // effet desactive : aucune passe
        }
        switch (effect) {
        case EffectId::Blur: {
            // Rayon proportionnel a la hauteur (meme rendu en 1080p et en 4K).
            const float radius = strength * 4.0f * static_cast<float>(height_) / 1080.0f;
            Draw(kBlur, scene_[next()].rtv.Get(), width_, height_, scene_[current].srv.Get(), nullptr, strength,
                 {radius / static_cast<float>(width_), 0.0f, 0.0f, 0.0f});
            current = next();
            Draw(kBlur, scene_[next()].rtv.Get(), width_, height_, scene_[current].srv.Get(), nullptr, strength,
                 {0.0f, radius / static_cast<float>(height_), 0.0f, 0.0f});
            current = next();
            break;
        }
        case EffectId::Bloom: {
            const uint32_t hw = half_[0].width;
            const uint32_t hh = half_[0].height;
            const float radius = 2.5f * static_cast<float>(hh) / 540.0f;
            Draw(kBright, half_[0].rtv.Get(), hw, hh, scene_[current].srv.Get(), nullptr, strength, noParams);
            Draw(kBlur, half_[1].rtv.Get(), hw, hh, half_[0].srv.Get(), nullptr, strength,
                 {radius / static_cast<float>(hw), 0.0f, 0.0f, 0.0f});
            Draw(kBlur, half_[0].rtv.Get(), hw, hh, half_[1].srv.Get(), nullptr, strength,
                 {0.0f, radius / static_cast<float>(hh), 0.0f, 0.0f});
            Draw(kBloomAdd, scene_[next()].rtv.Get(), width_, height_, scene_[current].srv.Get(), half_[0].srv.Get(),
                 strength, noParams);
            current = next();
            break;
        }
        case EffectId::Chromatic:
        case EffectId::Vignette:
        case EffectId::Grain:
        case EffectId::Scanlines: {
            const Pass pass = effect == EffectId::Chromatic ? kChromatic
                              : effect == EffectId::Vignette ? kVignette
                              : effect == EffectId::Grain    ? kGrain
                                                             : kScanlines;
            Draw(pass, scene_[next()].rtv.Get(), width_, height_, scene_[current].srv.Get(), nullptr, strength, noParams);
            current = next();
            break;
        }
        }
    }
    // Passe finale (ondulations, particules, trainee ; simple copie si rien de tout cela) vers la swap chain.
    Draw(kFinal, output, width_, height_, scene_[current].srv.Get(), nullptr, 1.0f, noParams);
    settings_ = nullptr;
    frame_ = nullptr;
}

}  // namespace lwe
