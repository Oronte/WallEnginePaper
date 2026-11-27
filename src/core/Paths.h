#pragma once

#include <filesystem>

namespace lwe {

inline constexpr wchar_t kProductName[] = L"WallEnginePaper";

// Chemin complet de WallEnginePaper.exe.
std::filesystem::path ExecutablePath();

// Dossier contenant l'executable (et le dossier assets/ livre avec).
std::filesystem::path ExecutableDir();

// %APPDATA%\WallEnginePaper (cree si absent). Au premier lancement, la configuration de l'ancien
// dossier %APPDATA%\LocalWallpaperEngine est reprise automatiquement.
std::filesystem::path AppDataDir();

// %APPDATA%\WallEnginePaper\library : fonds importes par l'utilisateur.
std::filesystem::path UserLibraryDir();

// Chemin relatif -> resolu depuis le dossier de l'executable ; chemin absolu -> inchange.
std::filesystem::path ResolveAppPath(const std::filesystem::path& path);

// Inverse de ResolveAppPath : rend relatif au dossier de l'executable ce qui s'y trouve,
// pour que config.json reste valide si l'application est deplacee.
std::filesystem::path MakePortablePath(const std::filesystem::path& path);

}  // namespace lwe
