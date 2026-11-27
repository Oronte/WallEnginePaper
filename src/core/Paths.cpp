#include "core/Paths.h"

#include <windows.h>
#include <knownfolders.h>
#include <shlobj.h>

#include <string>

namespace lwe {
namespace {

std::filesystem::path RoamingAppData() {
    std::filesystem::path base;
    PWSTR raw = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_CREATE, nullptr, &raw)) && raw) {
        base = raw;
    }
    CoTaskMemFree(raw);
    return base;
}

}  // namespace

std::filesystem::path ExecutablePath() {
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return {};
        }
        if (length < buffer.size()) {
            buffer.resize(length);
            return std::filesystem::path(buffer);
        }
        buffer.resize(buffer.size() * 2);
    }
}

std::filesystem::path ExecutableDir() {
    const std::filesystem::path exe = ExecutablePath();
    if (exe.empty()) {
        std::error_code ec;
        return std::filesystem::current_path(ec);
    }
    return exe.parent_path();
}

std::filesystem::path AppDataDir() {
    std::filesystem::path base = RoamingAppData();
    if (base.empty()) {
        base = ExecutableDir();
    }
    const std::filesystem::path dir = base / kProductName;
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec)) {
        std::filesystem::create_directories(dir, ec);
        // Reprise de la configuration de l'ancien nom du projet.
        const std::filesystem::path legacy = base / L"LocalWallpaperEngine" / L"config.json";
        if (std::filesystem::exists(legacy, ec)) {
            std::filesystem::copy_file(legacy, dir / L"config.json", std::filesystem::copy_options::skip_existing, ec);
        }
    }
    return dir;
}

std::filesystem::path UserLibraryDir() {
    const std::filesystem::path dir = AppDataDir() / L"library";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

std::filesystem::path ResolveAppPath(const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute()) {
        return path;
    }
    return (ExecutableDir() / path).lexically_normal();
}

std::filesystem::path MakePortablePath(const std::filesystem::path& path) {
    if (!path.is_absolute()) {
        return path;
    }
    const std::filesystem::path relative = path.lexically_normal().lexically_relative(ExecutableDir());
    if (relative.empty() || relative.native().starts_with(L"..")) {
        return path;
    }
    return relative;
}

}  // namespace lwe
