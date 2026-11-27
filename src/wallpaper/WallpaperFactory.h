#pragma once

#include <memory>
#include <string>

#include "wallpaper/IWallpaperBackend.h"

namespace lwe {

// Vrai si ce type de fond est pris en charge par cette build.
bool IsBackendAvailable(WallpaperType type) noexcept;

// Instancie le backend correspondant au type demande, ou retourne nullptr (avec un message)
// si ce type n'est pas compile dans cette build.
std::unique_ptr<IWallpaperBackend> CreateWallpaperBackend(WallpaperType type, std::wstring& error);

}  // namespace lwe
