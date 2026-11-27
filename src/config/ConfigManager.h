#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include "config/AppConfig.h"

namespace lwe {

// Charge / sauvegarde config.json. Les modifications sont regroupees (MarkDirty) puis ecrites
// de facon atomique (fichier temporaire + MoveFileEx) au plus une fois par seconde.
class ConfigManager {
public:
    void Load(const std::filesystem::path& file);
    bool Save();

    void MarkDirty() noexcept;
    void FlushIfDirty(bool force = false);

    AppConfig& Data() noexcept { return data_; }
    const AppConfig& Data() const noexcept { return data_; }
    const std::filesystem::path& FilePath() const noexcept { return file_; }

    // Reference persistante -> description complete (chemin absolu + reglages enregistres).
    WallpaperDesc Resolve(const WallpaperRef& ref) const;
    void SetWallpaperParam(const WallpaperRef& ref, const std::string& name, const ParamValue& value);
    void ResetWallpaperParams(const WallpaperRef& ref);

private:
    std::filesystem::path file_;
    AppConfig data_;
    bool dirty_ = false;
    uint64_t dirtySinceMs_ = 0;
};

}  // namespace lwe
