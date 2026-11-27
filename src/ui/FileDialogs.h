#pragma once

#include <windows.h>

#include <filesystem>
#include <optional>

namespace lwe {

// Boites de dialogue Windows natives (IFileOpenDialog), modales pour `owner`.
std::optional<std::filesystem::path> PickWallpaperFile(HWND owner);
std::optional<std::filesystem::path> PickFolder(HWND owner);

// Ouvre un dossier ou un fichier avec l'application associee (Explorateur, Bloc-notes...).
void OpenInShell(const std::filesystem::path& path);

}  // namespace lwe
