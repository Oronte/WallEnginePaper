#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace lwe {

struct LoadedTexture {
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
    uint32_t width = 0;
    uint32_t height = 0;
    explicit operator bool() const noexcept { return view != nullptr; }
};

// Decodage WIC (PNG, JPEG, BMP, GIF, WebP si le codec Windows est present) vers une texture RGBA
// immuable, reduite si necessaire pour que la plus grande dimension ne depasse pas `maxDimension`.
LoadedTexture LoadTextureFromFile(ID3D11Device* device, const std::filesystem::path& file, uint32_t maxDimension);
LoadedTexture LoadTextureFromMemory(ID3D11Device* device, const void* data, size_t size, uint32_t maxDimension);

// Ressource RCDATA embarquee dans l'executable (ex. le logo).
LoadedTexture LoadTextureFromResource(ID3D11Device* device, int resourceId, uint32_t maxDimension);

}  // namespace lwe
