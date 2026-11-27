#pragma once

#include <wrl/client.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "wallpaper/IWallpaperBackend.h"

namespace lwe {

// Fond d'ecran HLSL temps reel, compatible avec la convention Shadertoy :
//
//   void mainImage(out float4 fragColor, in float2 fragCoord) { ... }
//
// Uniformes disponibles : iResolution, iTime, iTimeDelta, iFrame, iFrameRate, iMouse, iDate,
// iAudioSpectrum[16] (64 bandes empaquetees, lire via AudioBand(i) / AudioSample(x)), iAudioLevel,
// iMouseNorm, iParallax, iMouseInside.
//
// Parametres exposes a l'interface, declares dans un commentaire du shader :
//   // @param float intensity 1.0 0.0 2.0 "Intensite"
//   // @param color baseColor 0.2 0.5 1.0 "Couleur"
//   // @param bool  showBars  true "Afficher le spectre"
// Chaque parametre devient une macro utilisable directement dans le code (intensity, baseColor...).
//
// Le fichier est surveille : toute sauvegarde recompile le shader a chaud ; en cas d'erreur, la
// version precedente reste affichee et l'erreur est journalisee.
class ShaderBackend final : public IWallpaperBackend {
public:
    WallpaperType Type() const noexcept override { return WallpaperType::Shader; }

    bool Initialize(const BackendInitContext& context, std::wstring& error) override;
    void Resize(uint32_t width, uint32_t height) override;
    bool Render(ID3D11RenderTargetView* target, const FrameContext& frame) override;

    std::vector<ParamDescriptor> Parameters() const override;
    ParamMap ParameterValues() const override;
    void SetParameter(std::string_view name, const ParamValue& value) override;

    void MaintenanceTick() override;

    static constexpr size_t kMaxShaderParams = 16;

private:
    struct ShaderParam {
        ParamDescriptor desc;
        ParamValue value;
        uint32_t slot = 0;
    };

    bool LoadAndCompile(std::wstring& error);
    bool Compile(const std::string& userSource, const std::string& displayName, std::wstring& error);
    bool ApplyParameter(std::string_view name, const ParamValue& value);
    static std::vector<ShaderParam> ParseAnnotations(const std::string& source, std::vector<std::string>& warnings);
    static std::string BuildPrelude(const std::vector<ShaderParam>& params, bool glslCompatibility);

    RenderDevice* device_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> pixelShader_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> constants_;
    uint32_t width_ = 1;
    uint32_t height_ = 1;

    std::filesystem::path sourcePath_;
    std::filesystem::file_time_type lastWriteTime_{};
    std::vector<ShaderParam> params_;
    ParamMap overrides_;

    // Reglages communs a tous les shaders (appliques par le moteur).
    float speed_ = 1.0f;
    float hueShift_ = 0.0f;
    Color tint_{1.0f, 1.0f, 1.0f, 1.0f};
    float brightness_ = 1.0f;
    float audioSensitivity_ = 1.0f;
    float parallax_ = 1.0f;

    double shaderTime_ = 0.0;
    uint64_t localFrame_ = 0;
};

}  // namespace lwe
