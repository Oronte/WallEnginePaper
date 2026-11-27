#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>

#include "config/AppConfig.h"

namespace lwe {

class RenderDevice;

// Compose deux images (ancien et nouveau fond, rendus dans des RenderTexture) par un shader de transition :
// fondu, dissolve (bruit), balayage, zoom-fondu. Le bytecode est compile une seule fois pour toute
// l'application ; chaque surface cree ses propres objets D3D (quelques octets).
class TransitionRenderer {
public:
    bool Initialize(RenderDevice& device, std::wstring& error);

    // `progress` dans [0, 1] ; `seed` dans [0, 1] (direction du balayage, motif du dissolve).
    // `type` ne doit pas valoir Random (resolu par l'appelant).
    void Draw(ID3D11RenderTargetView* target, ID3D11ShaderResourceView* from, ID3D11ShaderResourceView* to,
              uint32_t width, uint32_t height, float progress, TransitionType type, float seed);

private:
    RenderDevice* device_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> shader_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> constants_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;
};

}  // namespace lwe
