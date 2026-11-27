#pragma once

#include <d3d11.h>

#include <cstdint>
#include <filesystem>

#include "render/TextureLoader.h"

namespace lwe {

// Extrait une trame (vers 1,5 s) d'une video avec IMFSourceReader et la reduit pour servir de miniature.
LoadedTexture LoadVideoThumbnail(ID3D11Device* device, const std::filesystem::path& file, uint32_t maxDimension);

}  // namespace lwe
