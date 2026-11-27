#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>

namespace lwe {

// Texture de rendu hors ecran (miniatures et apercus de l'interface) : RTV pour dessiner, SRV pour l'afficher.
struct RenderTexture {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
    uint32_t width = 0;
    uint32_t height = 0;

    bool Create(ID3D11Device* device, uint32_t w, uint32_t h);
    void Reset() noexcept;
    explicit operator bool() const noexcept { return srv != nullptr; }
};

}  // namespace lwe
