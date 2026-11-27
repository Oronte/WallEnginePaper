#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "wallpaper/IWallpaperBackend.h"

namespace lwe {

class RenderDevice;

// Reglages d'affichage communs aux fonds image et video.
struct MediaAppearance {
    enum Fit : int { kCover = 0, kContain = 1, kStretch = 2 };

    int fit = kCover;
    float parallax = 0.0f;  // amplitude de la parallaxe souris (0 = desactivee)
    float hue = 0.0f;       // degres
    Color tint{1.0f, 1.0f, 1.0f, 1.0f};
    float brightness = 1.0f;

    static std::vector<ParamDescriptor> Descriptors(float defaultParallax);
    bool Apply(std::string_view name, const ParamValue& value);  // true si `name` est un reglage d'affichage
    void Store(ParamMap& values) const;
};

// Dessine une texture (trame video, image) dans une cible : ajustement (remplir / adapter / etirer),
// parallaxe souris (leger zoom + decalage, sans jamais devoiler les bords) et correction de couleur.
class MediaCompositor {
public:
    bool Initialize(RenderDevice& device, std::wstring& error);

    void Draw(ID3D11RenderTargetView* target, uint32_t targetWidth, uint32_t targetHeight,
              ID3D11ShaderResourceView* source, uint32_t sourceWidth, uint32_t sourceHeight,
              const MediaAppearance& appearance, float parallaxX, float parallaxY);

private:
    RenderDevice* device_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> pixelShader_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> constants_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;
};

}  // namespace lwe
