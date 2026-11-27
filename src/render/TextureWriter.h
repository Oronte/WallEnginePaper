#pragma once

#include <d3d11.h>
#include <objidl.h>

#include <cstdint>
#include <filesystem>
#include <string>

namespace lwe {

class RenderDevice;

// Enregistre une texture B8G8R8A8 en PNG (copie dans une texture de lecture, encodage WIC, alpha opaque),
// reduite a `maxWidth` pixels de large (0 = taille d'origine). Sert aux apercus (--capture) : jamais
// appele pendant le rendu normal.
bool SaveTextureToPng(RenderDevice& device, ID3D11Texture2D* texture, const std::filesystem::path& file,
                      uint32_t maxWidth, std::wstring& error);

// Reencode un PNG (capture WebView2 en memoire) en le reduisant a `maxWidth` pixels de large.
bool ResavePng(IStream* png, const std::filesystem::path& file, uint32_t maxWidth, std::wstring& error);

}  // namespace lwe
