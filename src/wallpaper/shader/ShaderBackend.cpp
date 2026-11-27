#include "wallpaper/shader/ShaderBackend.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstring>
#include <format>
#include <fstream>
#include <iterator>
#include <map>
#include <type_traits>

#include "core/Log.h"
#include "core/StringUtil.h"
#include "core/Win32Util.h"
#include "render/RenderDevice.h"

namespace lwe {
namespace {

using Microsoft::WRL::ComPtr;

// Miroir exact du cbuffer LweGlobals (regles d'empaquetage HLSL : registres de 16 octets).
struct ShaderConstants {
    float iResolution[3];
    float iTime;
    float iMouse[4];
    float iTimeDelta;
    float iFrame;
    float iFrameRate;
    float iAudioLevel;
    float iDate[4];
    float iMouseNorm[2];
    float iParallax[2];
    float iTint[4];
    float iHueShift;
    float iBrightness;
    float iAudioSensitivity;
    float iMouseInside;
    float iAudioSpectrum[kAudioBandCount];
    float iParams[ShaderBackend::kMaxShaderParams][4];
};
static_assert(sizeof(ShaderConstants) == 624, "ShaderConstants doit correspondre au cbuffer HLSL");
static_assert(sizeof(ShaderConstants) % 16 == 0, "Un constant buffer doit etre un multiple de 16 octets");

constexpr char kPreludeCore[] = R"hlsl(
// ---- WallEnginePaper : prelude genere ----
cbuffer LweGlobals : register(b0)
{
    float3 iResolution;
    float  iTime;
    float4 iMouse;
    float  iTimeDelta;
    float  iFrame;
    float  iFrameRate;
    float  iAudioLevel;
    float4 iDate;
    float2 iMouseNorm;
    float2 iParallax;
    float4 iTint;
    float  iHueShift;
    float  iBrightness;
    float  iAudioSensitivity;
    float  iMouseInside;
    float4 iAudioSpectrum[16];
    float4 iParams[16];
};

float AudioBand(int band)
{
    band = clamp(band, 0, 63);
    return iAudioSpectrum[band >> 2][band & 3];
}

float AudioSample(float x)
{
    float f = saturate(x) * 63.0;
    int i = (int)floor(f);
    return lerp(AudioBand(i), AudioBand(i + 1), f - (float)i);
}

float AudioRange(int first, int last)
{
    float sum = 0.0;
    for (int i = first; i <= last; ++i)
        sum += AudioBand(i);
    return sum / (float)(last - first + 1);
}

// ~20-260 Hz, ~260 Hz-3,9 kHz, ~3,9-16 kHz (a 48 kHz).
float AudioBass()   { return AudioRange(0, 9); }
float AudioMid()    { return AudioRange(10, 48); }
float AudioTreble() { return AudioRange(49, 63); }

float3 LweHueShift(float3 color, float degrees)
{
    if (abs(degrees) < 0.01)
        return color;
    const float3 k = float3(0.57735, 0.57735, 0.57735);
    float a = radians(degrees);
    float c = cos(a);
    return color * c + cross(k, color) * sin(a) + k * dot(k, color) * (1.0 - c);
}
)hlsl";

// Facilite le portage depuis Shadertoy (GLSL). Desactivable en ecrivant LWE_NO_GLSL_COMPAT
// n'importe ou dans le shader (par exemple dans un commentaire).
constexpr char kPreludeGlslCompat[] = R"hlsl(
typedef float2 vec2;
typedef float3 vec3;
typedef float4 vec4;
typedef int2 ivec2;
typedef int3 ivec3;
typedef int4 ivec4;
typedef float2x2 mat2;
typedef float3x3 mat3;
typedef float4x4 mat4;
#define fract frac
#define mix lerp
#define inversesqrt rsqrt
#define mod(x, y) ((x) - (y) * floor((x) / (y)))
)hlsl";

constexpr char kEpilogue[] = R"hlsl(
float4 LwePixelMain(float4 lwePosition : SV_Position) : SV_Target
{
    // SV_Position a son origine en haut a gauche ; Shadertoy en bas a gauche.
    float2 lweFragCoord = float2(lwePosition.x, iResolution.y - lwePosition.y);
    float4 lweColor = float4(0.0, 0.0, 0.0, 1.0);
    mainImage(lweColor, lweFragCoord);
    float3 lweRgb = LweHueShift(lweColor.rgb, iHueShift) * iTint.rgb * iBrightness;
    return float4(saturate(lweRgb), 1.0);
}
)hlsl";

// Utilise si le fichier configure est introuvable ou ne compile pas : un degrade sobre et anime.
constexpr char kBuiltinFallback[] = R"hlsl(
void mainImage(out float4 fragColor, in float2 fragCoord)
{
    float2 uv = fragCoord / iResolution.xy;
    float wave = 0.5 + 0.5 * sin(iTime * 0.25 + uv.x * 3.0 + iParallax.x * 0.5);
    float3 top = float3(0.06, 0.08, 0.16);
    float3 bottom = float3(0.14, 0.06, 0.20);
    float3 color = lerp(bottom, top, uv.y) + 0.04 * wave;
    float glow = exp(-length(fragCoord - iMouse.xy) / (0.25 * iResolution.y)) * iMouseInside;
    fragColor = float4(color + glow * float3(0.05, 0.07, 0.12), 1.0);
}
)hlsl";

const std::vector<ParamDescriptor>& EngineParameters() {
    static const std::vector<ParamDescriptor> params = {
        {"speed", "Vitesse", ParamKind::Float, 1.0f, 0.0f, 4.0f},
        {"hue", "Teinte (degrés)", ParamKind::Float, 0.0f, -180.0f, 180.0f},
        {"tint", "Filtre couleur", ParamKind::Color, Color{1.0f, 1.0f, 1.0f, 1.0f}, 0.0f, 1.0f},
        {"brightness", "Luminosité", ParamKind::Float, 1.0f, 0.0f, 2.0f},
        {"audioSensitivity", "Sensibilité audio", ParamKind::Float, 1.0f, 0.0f, 4.0f},
        {"parallax", "Parallaxe souris", ParamKind::Float, 1.0f, 0.0f, 2.0f},
    };
    return params;
}

bool IsEngineParameter(std::string_view name) {
    const auto& params = EngineParameters();
    return std::any_of(params.begin(), params.end(), [&](const ParamDescriptor& p) { return p.name == name; });
}

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

struct Token {
    std::string text;
    bool quoted = false;
};

std::vector<Token> Tokenize(std::string_view line) {
    std::vector<Token> tokens;
    size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) {
            ++i;
        }
        if (i >= line.size()) {
            break;
        }
        Token token;
        if (line[i] == '"') {
            const size_t end = line.find('"', i + 1);
            token.text = std::string(line.substr(i + 1, end == std::string_view::npos ? std::string_view::npos
                                                                                       : end - i - 1));
            token.quoted = true;
            i = end == std::string_view::npos ? line.size() : end + 1;
        } else {
            const size_t start = i;
            while (i < line.size() && !std::isspace(static_cast<unsigned char>(line[i]))) {
                ++i;
            }
            token.text = std::string(line.substr(start, i - start));
        }
        tokens.push_back(std::move(token));
    }
    return tokens;
}

bool ParseFloat(std::string_view text, float& out) {
    if (!text.empty() && text.front() == '+') {
        text.remove_prefix(1);
    }
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), out);
    return ec == std::errc{} && end == text.data() + text.size();
}

bool ParseBool(std::string_view text, bool& out) {
    if (text == "true" || text == "1" || text == "on") {
        out = true;
        return true;
    }
    if (text == "false" || text == "0" || text == "off") {
        out = false;
        return true;
    }
    return false;
}

bool IsIdentifier(std::string_view name) {
    if (name.empty() || !(std::isalpha(static_cast<unsigned char>(name[0])) || name[0] == '_')) {
        return false;
    }
    return std::all_of(name.begin(), name.end(),
                       [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; });
}

// Cache de bytecode partage par toutes les surfaces : le meme fond affiche sur N ecrans n'est
// compile qu'une fois (~0,4 s de CPU economisees par ecran supplementaire). La cle est la source
// complete generee ; les shaders utilisant #include ne sont pas mis en cache (dependances non suivies).
ComPtr<ID3DBlob> CompilePixelShaderCached(const std::string& fullSource, const std::string& name,
                                          std::string& diagnostics) {
    constexpr size_t kCapacity = 16;
    static std::map<std::string, ComPtr<ID3DBlob>, std::less<>> cache;
    const bool cacheable = fullSource.find("#include") == std::string::npos;
    if (cacheable) {
        if (const auto it = cache.find(fullSource); it != cache.end()) {
            diagnostics.clear();
            return it->second;
        }
    }
    ComPtr<ID3DBlob> code = RenderDevice::CompileShader(fullSource, name, "LwePixelMain", "ps_5_0", diagnostics);
    if (code && cacheable) {
        if (cache.size() >= kCapacity) {
            cache.clear();
        }
        cache.emplace(fullSource, code);
    }
    return code;
}

bool ReadTextFile(const std::filesystem::path& file, std::string& out) {
    std::ifstream stream(file, std::ios::binary);
    if (!stream) {
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    if (out.size() >= 3 && static_cast<unsigned char>(out[0]) == 0xEF &&
        static_cast<unsigned char>(out[1]) == 0xBB && static_cast<unsigned char>(out[2]) == 0xBF) {
        out.erase(0, 3);  // BOM UTF-8
    }
    return !stream.bad();
}

}  // namespace

bool ShaderBackend::Initialize(const BackendInitContext& context, std::wstring& error) {
    device_ = &context.device;
    width_ = std::max<uint32_t>(1, context.width);
    height_ = std::max<uint32_t>(1, context.height);
    sourcePath_ = context.desc.source;
    overrides_ = context.desc.params;

    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = sizeof(ShaderConstants);
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    const HRESULT hr = device_->Device()->CreateBuffer(&desc, nullptr, &constants_);
    if (FAILED(hr)) {
        error = L"CreateBuffer (constantes) : " + HResultToString(hr);
        return false;
    }

    if (sourcePath_.empty()) {
        return Compile(kBuiltinFallback, "lwe_builtin_fallback", error);
    }
    return LoadAndCompile(error);
}

void ShaderBackend::Resize(uint32_t width, uint32_t height) {
    width_ = std::max<uint32_t>(1, width);
    height_ = std::max<uint32_t>(1, height);
}

bool ShaderBackend::LoadAndCompile(std::wstring& error) {
    std::error_code ec;
    lastWriteTime_ = std::filesystem::last_write_time(sourcePath_, ec);
    std::string source;
    if (ec || !ReadTextFile(sourcePath_, source)) {
        error = std::format(L"lecture impossible de {}", sourcePath_.wstring());
        return false;
    }
    return Compile(source, PathToUtf8(sourcePath_), error);
}

std::vector<ShaderBackend::ShaderParam> ShaderBackend::ParseAnnotations(const std::string& source,
                                                                        std::vector<std::string>& warnings) {
    std::vector<ShaderParam> params;
    size_t lineStart = 0;
    while (lineStart < source.size()) {
        size_t lineEnd = source.find('\n', lineStart);
        if (lineEnd == std::string::npos) {
            lineEnd = source.size();
        }
        const std::string_view line(source.data() + lineStart, lineEnd - lineStart);
        lineStart = lineEnd + 1;

        const size_t tag = line.find("@param");
        if (tag == std::string_view::npos) {
            continue;
        }
        const std::vector<Token> tokens = Tokenize(line.substr(tag + 6));
        if (tokens.size() < 2) {
            warnings.push_back(std::format("@param incomplet : {}", line));
            continue;
        }
        const std::string& kind = tokens[0].text;
        const std::string& name = tokens[1].text;
        if (!IsIdentifier(name) || IsEngineParameter(name)) {
            warnings.push_back(std::format("@param : nom invalide ou reserve \"{}\"", name));
            continue;
        }
        if (std::any_of(params.begin(), params.end(), [&](const ShaderParam& p) { return p.desc.name == name; })) {
            warnings.push_back(std::format("@param : \"{}\" declare deux fois", name));
            continue;
        }
        if (params.size() >= kMaxShaderParams) {
            warnings.push_back(std::format("@param : limite de {} parametres atteinte", kMaxShaderParams));
            break;
        }

        // Valeurs numeriques jusqu'au libelle optionnel (entre guillemets).
        std::vector<std::string> values;
        std::string label = name;
        for (size_t i = 2; i < tokens.size(); ++i) {
            if (tokens[i].quoted) {
                label = tokens[i].text;
                break;
            }
            values.push_back(tokens[i].text);
        }

        ShaderParam param;
        param.desc.name = name;
        param.desc.label = label;
        param.slot = static_cast<uint32_t>(params.size());
        bool ok = true;
        if (kind == "float") {
            float def = 0.0f;
            float lo = 0.0f;
            float hi = 1.0f;
            ok = !values.empty() && ParseFloat(values[0], def);
            if (ok && values.size() >= 3) {
                ok = ParseFloat(values[1], lo) && ParseFloat(values[2], hi);
            } else if (ok) {
                lo = std::min(0.0f, def);
                hi = std::max(1.0f, def * 2.0f);
            }
            param.desc.kind = ParamKind::Float;
            param.desc.defaultValue = def;
            param.desc.minValue = std::min(lo, hi);
            param.desc.maxValue = std::max(lo, hi);
        } else if (kind == "bool") {
            bool def = false;
            ok = !values.empty() && ParseBool(values[0], def);
            param.desc.kind = ParamKind::Bool;
            param.desc.defaultValue = def;
        } else if (kind == "color") {
            Color def{0.0f, 0.0f, 0.0f, 1.0f};
            ok = values.size() == 3 || values.size() == 4;
            for (size_t i = 0; ok && i < values.size(); ++i) {
                ok = ParseFloat(values[i], def[i]);
            }
            param.desc.kind = ParamKind::Color;
            param.desc.defaultValue = def;
        } else {
            ok = false;
        }
        if (!ok) {
            warnings.push_back(std::format("@param \"{}\" : syntaxe invalide (type float|bool|color)", name));
            continue;
        }
        param.value = param.desc.defaultValue;
        params.push_back(std::move(param));
    }
    return params;
}

std::string ShaderBackend::BuildPrelude(const std::vector<ShaderParam>& params, bool glslCompatibility) {
    std::string prelude = kPreludeCore;
    if (glslCompatibility) {
        prelude += kPreludeGlslCompat;
    }
    for (const auto& param : params) {
        switch (param.desc.kind) {
        case ParamKind::Float:
            prelude += std::format("#define {} (iParams[{}].x)\n", param.desc.name, param.slot);
            break;
        case ParamKind::Bool:
            prelude += std::format("#define {} (iParams[{}].x > 0.5)\n", param.desc.name, param.slot);
            break;
        case ParamKind::Color:
            prelude += std::format("#define {} (iParams[{}])\n", param.desc.name, param.slot);
            break;
        }
    }
    return prelude;
}

bool ShaderBackend::Compile(const std::string& userSource, const std::string& displayName, std::wstring& error) {
    std::vector<std::string> warnings;
    std::vector<ShaderParam> parsed = ParseAnnotations(userSource, warnings);
    for (const auto& warning : warnings) {
        log::Warn(L"{} : {}", Utf8ToWide(displayName), Utf8ToWide(warning));
    }
    // Rechargement a chaud : les valeurs reglees par l'utilisateur survivent a la recompilation.
    for (auto& param : parsed) {
        for (const auto& previous : params_) {
            if (previous.desc.name == param.desc.name && previous.desc.kind == param.desc.kind) {
                param.value = previous.value;
            }
        }
    }

    const bool glslCompat = userSource.find("LWE_NO_GLSL_COMPAT") == std::string::npos;
    std::string fullSource = BuildPrelude(parsed, glslCompat);
    fullSource += std::format("#line 1 \"{}\"\n", displayName);
    fullSource += userSource;
    fullSource += "\n#line 1 \"lwe_epilogue\"\n";
    fullSource += kEpilogue;

    std::string diagnostics;
    ComPtr<ID3DBlob> code = CompilePixelShaderCached(fullSource, displayName, diagnostics);
    if (!code) {
        error = Utf8ToWide(diagnostics);
        return false;
    }
    if (!diagnostics.empty()) {
        log::Warn(L"Avertissements de compilation :\n{}", Utf8ToWide(diagnostics));
    }

    ComPtr<ID3D11PixelShader> shader;
    const HRESULT hr = device_->Device()->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr,
                                                            &shader);
    if (FAILED(hr)) {
        error = L"CreatePixelShader : " + HResultToString(hr);
        return false;
    }

    pixelShader_ = std::move(shader);
    params_ = std::move(parsed);
    for (const auto& [name, value] : overrides_) {
        ApplyParameter(name, value);
    }
    return true;
}

bool ShaderBackend::ApplyParameter(std::string_view name, const ParamValue& value) {
    if (name == "speed") {
        return AssignFloat(value, speed_);
    }
    if (name == "hue") {
        return AssignFloat(value, hueShift_);
    }
    if (name == "brightness") {
        return AssignFloat(value, brightness_);
    }
    if (name == "audioSensitivity") {
        return AssignFloat(value, audioSensitivity_);
    }
    if (name == "parallax") {
        return AssignFloat(value, parallax_);
    }
    if (name == "tint") {
        if (const auto* color = std::get_if<Color>(&value)) {
            tint_ = *color;
            return true;
        }
        return false;
    }
    for (auto& param : params_) {
        if (param.desc.name != name) {
            continue;
        }
        switch (param.desc.kind) {
        case ParamKind::Float: {
            float f = 0.0f;
            if (!AssignFloat(value, f)) {
                return false;
            }
            param.value = std::clamp(f, param.desc.minValue, param.desc.maxValue);
            return true;
        }
        case ParamKind::Bool: {
            float f = 0.0f;
            if (!AssignFloat(value, f)) {
                return false;
            }
            param.value = f > 0.5f;
            return true;
        }
        case ParamKind::Color:
            if (const auto* color = std::get_if<Color>(&value)) {
                param.value = *color;
                return true;
            }
            return false;
        }
    }
    return false;
}

std::vector<ParamDescriptor> ShaderBackend::Parameters() const {
    std::vector<ParamDescriptor> result = EngineParameters();
    for (const auto& param : params_) {
        result.push_back(param.desc);
    }
    return result;
}

ParamMap ShaderBackend::ParameterValues() const {
    ParamMap values;
    values["speed"] = speed_;
    values["hue"] = hueShift_;
    values["tint"] = tint_;
    values["brightness"] = brightness_;
    values["audioSensitivity"] = audioSensitivity_;
    values["parallax"] = parallax_;
    for (const auto& param : params_) {
        values[param.desc.name] = param.value;
    }
    return values;
}

void ShaderBackend::SetParameter(std::string_view name, const ParamValue& value) {
    if (ApplyParameter(name, value)) {
        overrides_.insert_or_assign(std::string(name), value);
    }
}

void ShaderBackend::MaintenanceTick() {
    if (sourcePath_.empty()) {
        return;
    }
    std::error_code ec;
    const auto writeTime = std::filesystem::last_write_time(sourcePath_, ec);
    if (ec || writeTime == lastWriteTime_) {
        return;
    }
    std::wstring error;
    if (LoadAndCompile(error)) {
        log::Info(L"Shader recharge : {}", sourcePath_.wstring());
    } else {
        log::Error(L"Rechargement de {} : la version precedente reste active.\n{}", sourcePath_.wstring(), error);
    }
}

bool ShaderBackend::Render(ID3D11RenderTargetView* target, const FrameContext& frame) {
    if (!pixelShader_ || !target) {
        return false;
    }
    shaderTime_ += static_cast<double>(frame.deltaTime) * speed_;

    ShaderConstants c{};
    c.iResolution[0] = static_cast<float>(width_);
    c.iResolution[1] = static_cast<float>(height_);
    c.iResolution[2] = 1.0f;
    c.iTime = static_cast<float>(shaderTime_);

    const MouseState& mouse = frame.mouse;
    c.iMouse[0] = mouse.x;
    c.iMouse[1] = mouse.y;
    c.iMouse[2] = mouse.pressed ? mouse.clickX : -mouse.clickX;  // convention Shadertoy : signe = bouton
    c.iMouse[3] = mouse.pressed ? mouse.clickY : -mouse.clickY;

    c.iTimeDelta = frame.deltaTime * speed_;
    c.iFrame = static_cast<float>(localFrame_++);
    c.iFrameRate = frame.frameRate;

    SYSTEMTIME now{};
    GetLocalTime(&now);
    c.iDate[0] = static_cast<float>(now.wYear);
    c.iDate[1] = static_cast<float>(now.wMonth - 1);
    c.iDate[2] = static_cast<float>(now.wDay);
    c.iDate[3] = static_cast<float>(now.wHour * 3600 + now.wMinute * 60 + now.wSecond) + now.wMilliseconds / 1000.0f;

    c.iMouseNorm[0] = mouse.normX;
    c.iMouseNorm[1] = mouse.normY;
    c.iParallax[0] = mouse.parallaxX * parallax_;
    c.iParallax[1] = mouse.parallaxY * parallax_;
    std::copy(tint_.begin(), tint_.end(), c.iTint);
    c.iHueShift = hueShift_;
    c.iBrightness = brightness_;
    c.iAudioSensitivity = audioSensitivity_;
    c.iMouseInside = mouse.inside ? 1.0f : 0.0f;

    if (frame.audio) {
        c.iAudioLevel = std::min(1.0f, frame.audio->level * audioSensitivity_);
        for (size_t i = 0; i < kAudioBandCount; ++i) {
            c.iAudioSpectrum[i] = std::min(1.0f, frame.audio->bands[i] * audioSensitivity_);
        }
    }

    for (const auto& param : params_) {
        float* slot = c.iParams[param.slot];
        std::visit(
            [slot](const auto& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, Color>) {
                    std::copy(v.begin(), v.end(), slot);
                } else if constexpr (std::is_same_v<T, bool>) {
                    slot[0] = v ? 1.0f : 0.0f;
                } else {
                    slot[0] = v;
                }
            },
            param.value);
    }

    ID3D11DeviceContext* context = device_->Context();
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(context->Map(constants_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        std::memcpy(mapped.pData, &c, sizeof(c));
        context->Unmap(constants_.Get(), 0);
    }

    const D3D11_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_), 0.0f, 1.0f};
    ID3D11Buffer* constantBuffer = constants_.Get();
    context->OMSetRenderTargets(1, &target, nullptr);
    context->RSSetViewports(1, &viewport);
    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(device_->FullscreenVertexShader(), nullptr, 0);
    context->PSSetShader(pixelShader_.Get(), nullptr, 0);
    context->PSSetConstantBuffers(0, 1, &constantBuffer);
    context->Draw(3, 0);
    return true;
}

}  // namespace lwe
