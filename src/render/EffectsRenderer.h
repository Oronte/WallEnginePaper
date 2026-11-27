#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <string>

#include "config/AppConfig.h"
#include "effects/Interaction.h"
#include "render/RenderTexture.h"

namespace lwe {

class RenderDevice;

// Etat d'une image pour les effets (temps, son, souris en pixels avec origine en haut a gauche).
struct EffectFrame {
    float time = 0.0f;
    float bass = 0.0f;
    float beat = 0.0f;
    float mouseX = 0.0f;
    float mouseY = 0.0f;
    bool mouseInside = false;
    const RippleSet* ripples = nullptr;
    const CursorTrail* trail = nullptr;
};

// Chaine de post-effets d'une surface : le fond dessine dans une texture "scene", puis chaque effet actif est
// applique dans l'ordre choisi (textures ping-pong ; flou et bloom en deux passes separables, bloom a
// mi-resolution), et une passe finale ajoute ondulations, particules et trainee avant d'ecrire dans la swap
// chain. Un effet a 0 ne coute aucune passe ; sans aucun effet, la surface n'utilise pas ce chemin du tout.
class EffectsRenderer {
public:
    bool Prepare(RenderDevice& device, uint32_t width, uint32_t height, std::wstring& error);
    void Release() noexcept;  // textures rendues (les shaders restent)
    bool IsPrepared() const noexcept { return scene_[0].rtv != nullptr; }

    // Cible ou le fond (ou la transition) dessine l'image de base.
    ID3D11RenderTargetView* SceneTarget() const noexcept { return scene_[0].rtv.Get(); }

    void Apply(ID3D11RenderTargetView* output, const EffectSettings& settings, const EffectFrame& frame);

private:
    enum Pass : size_t { kChromatic, kBlur, kBright, kBloomAdd, kVignette, kGrain, kScanlines, kFinal, kPassCount };

    void Draw(Pass pass, ID3D11RenderTargetView* target, uint32_t width, uint32_t height,
              ID3D11ShaderResourceView* source, ID3D11ShaderResourceView* extra, float strength,
              const std::array<float, 4>& params);

    RenderDevice* device_ = nullptr;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    // scene_[0] : image de base (jamais ecrasee : le fond ne la redessine pas forcement a chaque image) ;
    // scene_[1] et scene_[2] : ping-pong des passes, pleine resolution.
    std::array<RenderTexture, 3> scene_;
    std::array<RenderTexture, 2> half_;   // bloom a mi-resolution
    std::array<Microsoft::WRL::ComPtr<ID3D11PixelShader>, kPassCount> shaders_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> constants_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;

    std::array<float, 4> trailColor_{};
    const EffectSettings* settings_ = nullptr;
    const EffectFrame* frame_ = nullptr;
};

}  // namespace lwe
