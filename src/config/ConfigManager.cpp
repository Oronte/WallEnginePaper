#include "config/ConfigManager.h"

#include <windows.h>

#include <fstream>
#include <iterator>
#include <sstream>

#include "core/Log.h"
#include "core/Paths.h"
#include "core/StringUtil.h"

namespace lwe {
namespace {

constexpr uint64_t kSaveDebounceMs = 1000;

bool ReadWholeFile(const std::filesystem::path& file, std::string& out) {
    std::ifstream stream(file, std::ios::binary);
    if (!stream) {
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    return !stream.bad();
}

}  // namespace

void ConfigManager::Load(const std::filesystem::path& file) {
    file_ = file;
    data_ = AppConfig{};
    dirty_ = false;

    std::error_code ec;
    if (!std::filesystem::exists(file_, ec)) {
        log::Info(L"Aucune configuration trouvee : creation de {}", file_.wstring());
        Save();
        return;
    }

    std::string text;
    std::string error;
    if (!ReadWholeFile(file_, text) || !DeserializeConfig(text, data_, error)) {
        std::filesystem::path backup = file_;
        backup += L".corrupt";
        std::filesystem::copy_file(file_, backup, std::filesystem::copy_options::overwrite_existing, ec);
        log::Error(L"Configuration illisible ({}) : sauvegardee dans {} et remplacee par les valeurs par defaut",
                   Utf8ToWide(error), backup.wstring());
        data_ = AppConfig{};
        Save();
        return;
    }
    log::Info(L"Configuration chargee : {}", file_.wstring());
}

bool ConfigManager::Save() {
    if (file_.empty()) {
        return false;
    }
    data_.Sanitize();
    const std::string text = SerializeConfig(data_);

    std::filesystem::path temp = file_;
    temp += L".tmp";
    {
        std::ofstream stream(temp, std::ios::binary | std::ios::trunc);
        if (!stream) {
            log::Error(L"Ecriture impossible : {}", temp.wstring());
            return false;
        }
        stream.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!stream) {
            log::Error(L"Ecriture incomplete : {}", temp.wstring());
            return false;
        }
    }
    if (!MoveFileExW(temp.c_str(), file_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        log::Error(L"Remplacement de {} impossible (erreur {})", file_.wstring(), GetLastError());
        return false;
    }
    dirty_ = false;
    return true;
}

void ConfigManager::MarkDirty() noexcept {
    if (!dirty_) {
        dirty_ = true;
        dirtySinceMs_ = GetTickCount64();
    }
}

void ConfigManager::FlushIfDirty(bool force) {
    if (dirty_ && (force || GetTickCount64() - dirtySinceMs_ >= kSaveDebounceMs)) {
        Save();
    }
}

WallpaperDesc ConfigManager::Resolve(const WallpaperRef& ref) const {
    WallpaperDesc desc;
    desc.type = ref.type;
    desc.source = ResolveAppPath(ref.source);
    if (const auto it = data_.wallpaperSettings.find(WallpaperSettingsKey(ref.source));
        it != data_.wallpaperSettings.end()) {
        desc.params = it->second;
    }
    return desc;
}

void ConfigManager::SetWallpaperParam(const WallpaperRef& ref, const std::string& name, const ParamValue& value) {
    data_.wallpaperSettings[WallpaperSettingsKey(ref.source)][name] = value;
    MarkDirty();
}

void ConfigManager::ResetWallpaperParams(const WallpaperRef& ref) {
    if (data_.wallpaperSettings.erase(WallpaperSettingsKey(ref.source)) > 0) {
        MarkDirty();
    }
}

}  // namespace lwe
