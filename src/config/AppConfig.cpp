#include "config/AppConfig.h"

#include <algorithm>
#include <cmath>
#include <type_traits>

#include <nlohmann/json.hpp>

#include "core/StringUtil.h"

namespace lwe {
namespace {

using json = nlohmann::ordered_json;

struct TypeName {
    WallpaperType type;
    const char* name;
};

constexpr TypeName kTypeNames[] = {
    {WallpaperType::Shader, "shader"},
    {WallpaperType::Scene, "scene"},
    {WallpaperType::Video, "video"},
    {WallpaperType::Web, "web"},
};

template <class T>
void ReadValue(const json& object, const char* key, T& out) {
    if (!object.is_object()) {
        return;
    }
    const auto it = object.find(key);
    if (it == object.end() || it->is_null()) {
        return;
    }
    try {
        out = it->template get<T>();
    } catch (const json::exception&) {
        // Type inattendu : on conserve la valeur par defaut plutot que de rejeter tout le fichier.
    }
}

void ReadPath(const json& object, const char* key, std::filesystem::path& out) {
    std::string text;
    ReadValue(object, key, text);
    if (!text.empty()) {
        out = Utf8ToPath(text);
    }
}

json PathToJson(const std::filesystem::path& path) {
    return PathToUtf8(path);
}

json ParamToJson(const ParamValue& value) {
    return std::visit(
        [](const auto& v) -> json {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, Color>) {
                return json::array({v[0], v[1], v[2], v[3]});
            } else {
                return json(v);
            }
        },
        value);
}

bool ParamFromJson(const json& value, ParamValue& out) {
    if (value.is_boolean()) {
        out = value.get<bool>();
        return true;
    }
    if (value.is_number()) {
        const float f = value.get<float>();
        if (!std::isfinite(f)) {
            return false;
        }
        out = f;
        return true;
    }
    if (value.is_array() && (value.size() == 3 || value.size() == 4)) {
        Color color{0.0f, 0.0f, 0.0f, 1.0f};
        for (size_t i = 0; i < value.size(); ++i) {
            if (!value[i].is_number()) {
                return false;
            }
            color[i] = value[i].get<float>();
        }
        out = color;
        return true;
    }
    return false;
}

json RefToJson(const WallpaperRef& ref) {
    return json{{"type", ToString(ref.type)}, {"source", PathToJson(ref.source)}};
}

bool RefFromJson(const json& value, WallpaperRef& out) {
    if (!value.is_object()) {
        return false;
    }
    WallpaperRef ref;
    std::string type;
    ReadValue(value, "type", type);
    if (const auto parsed = ParseWallpaperType(type)) {
        ref.type = *parsed;
    } else if (!type.empty()) {
        return false;
    }
    ReadPath(value, "source", ref.source);
    if (ref.source.empty()) {
        return false;
    }
    out = std::move(ref);
    return true;
}

json PathListToJson(const std::vector<std::filesystem::path>& paths) {
    json list = json::array();
    for (const auto& path : paths) {
        list.push_back(PathToJson(path));
    }
    return list;
}

std::vector<std::filesystem::path> PathListFromJson(const json& object, const char* key) {
    std::vector<std::filesystem::path> paths;
    const auto it = object.find(key);
    if (it == object.end() || !it->is_array()) {
        return paths;
    }
    for (const auto& item : *it) {
        if (item.is_string()) {
            paths.push_back(Utf8ToPath(item.get<std::string>()));
        }
    }
    return paths;
}

}  // namespace

const char* ToString(WallpaperType type) noexcept {
    for (const auto& entry : kTypeNames) {
        if (entry.type == type) {
            return entry.name;
        }
    }
    return "shader";
}

std::optional<WallpaperType> ParseWallpaperType(std::string_view text) noexcept {
    for (const auto& entry : kTypeNames) {
        if (text == entry.name) {
            return entry.type;
        }
    }
    return std::nullopt;
}

std::optional<WallpaperType> GuessWallpaperType(const std::filesystem::path& file) {
    const std::wstring ext = ToLower(file.extension().wstring());
    if (ext == L".hlsl" || ext == L".fx") {
        return WallpaperType::Shader;
    }
    if (ext == L".mp4" || ext == L".webm" || ext == L".mkv" || ext == L".mov" || ext == L".avi" || ext == L".wmv" ||
        ext == L".m4v") {
        return WallpaperType::Video;
    }
    if (ext == L".html" || ext == L".htm") {
        return WallpaperType::Web;
    }
    if (ext == L".png" || ext == L".jpg" || ext == L".jpeg" || ext == L".bmp" || ext == L".gif" ||
        ext == L".webp") {
        return WallpaperType::Scene;
    }
    return std::nullopt;
}

void AppConfig::Sanitize() {
    version = kCurrentVersion;
    fpsLimit = std::clamp<uint32_t>(fpsLimit, 1, 360);
    if (!std::isfinite(renderScale)) {
        renderScale = 1.0f;
    }
    renderScale = std::clamp(renderScale, 0.25f, 1.0f);
    if (!std::isfinite(audio.volume)) {
        audio.volume = 0.5f;
    }
    audio.volume = std::clamp(audio.volume, 0.0f, 1.0f);
    if (!std::isfinite(audio.sensitivity)) {
        audio.sensitivity = 1.0f;
    }
    audio.sensitivity = std::clamp(audio.sensitivity, 0.0f, 8.0f);
    playlist.intervalMinutes = std::clamp<uint32_t>(playlist.intervalMinutes, 1, 24 * 60);
    if (defaultWallpaper.Empty()) {
        defaultWallpaper = AppConfig{}.defaultWallpaper;
    }
}

std::string WallpaperSettingsKey(const std::filesystem::path& source) {
    return WideToUtf8(ToLower(source.lexically_normal().generic_wstring()));
}

std::string SerializeConfig(const AppConfig& config) {
    json root;
    root["version"] = config.version;
    root["fpsLimit"] = config.fpsLimit;
    root["renderScale"] = config.renderScale;
    root["layout"] = config.layout == LayoutMode::Span ? "span" : "perMonitor";
    root["defaultWallpaper"] = RefToJson(config.defaultWallpaper);
    root["spanWallpaper"] = config.spanWallpaper.Empty() ? json(nullptr) : RefToJson(config.spanWallpaper);

    json monitors = json::object();
    for (const auto& [id, ref] : config.monitorWallpapers) {
        monitors[id] = RefToJson(ref);
    }
    root["monitors"] = std::move(monitors);

    json settings = json::object();
    for (const auto& [key, params] : config.wallpaperSettings) {
        json values = json::object();
        for (const auto& [name, value] : params) {
            values[name] = ParamToJson(value);
        }
        settings[key] = std::move(values);
    }
    root["wallpaperSettings"] = std::move(settings);

    root["pause"] = {
        {"onFullscreenApp", config.pause.onFullscreenApp},
        {"onMaximizedApp", config.pause.onMaximizedApp},
        {"onBattery", config.pause.onBattery},
        {"onDisplayOff", config.pause.onDisplayOff},
        {"onSessionLocked", config.pause.onSessionLocked},
        {"releaseGpuResources", config.pause.releaseGpuResources},
    };
    root["audio"] = {
        {"volume", config.audio.volume},
        {"muted", config.audio.muted},
        {"reactive", config.audio.reactive},
        {"sensitivity", config.audio.sensitivity},
        {"autoGain", config.audio.autoGain},
    };
    root["playlist"] = {
        {"enabled", config.playlist.enabled},
        {"intervalMinutes", config.playlist.intervalMinutes},
        {"shuffle", config.playlist.shuffle},
        {"advanceOnStartup", config.playlist.advanceOnStartup},
        {"items", PathListToJson(config.playlist.items)},
    };
    root["startWithWindows"] = config.startWithWindows;
    root["libraryFolders"] = PathListToJson(config.libraryFolders);
    return root.dump(2);
}

bool DeserializeConfig(std::string_view text, AppConfig& out, std::string& error) {
    const json root = json::parse(text.begin(), text.end(), nullptr, /*allow_exceptions=*/false,
                                  /*ignore_comments=*/true);
    if (root.is_discarded() || !root.is_object()) {
        error = "JSON invalide";
        return false;
    }

    AppConfig config;
    ReadValue(root, "version", config.version);
    ReadValue(root, "fpsLimit", config.fpsLimit);
    ReadValue(root, "renderScale", config.renderScale);

    std::string layout;
    ReadValue(root, "layout", layout);
    if (layout == "span") {
        config.layout = LayoutMode::Span;
    } else if (layout == "perMonitor") {
        config.layout = LayoutMode::PerMonitor;
    }

    if (const auto it = root.find("defaultWallpaper"); it != root.end()) {
        RefFromJson(*it, config.defaultWallpaper);
    }
    if (const auto it = root.find("spanWallpaper"); it != root.end()) {
        RefFromJson(*it, config.spanWallpaper);
    }
    if (const auto it = root.find("monitors"); it != root.end() && it->is_object()) {
        for (const auto& [id, value] : it->items()) {
            WallpaperRef ref;
            if (RefFromJson(value, ref)) {
                config.monitorWallpapers[id] = std::move(ref);
            }
        }
    }
    if (const auto it = root.find("wallpaperSettings"); it != root.end() && it->is_object()) {
        for (const auto& [key, values] : it->items()) {
            if (!values.is_object()) {
                continue;
            }
            ParamMap params;
            for (const auto& [name, raw] : values.items()) {
                ParamValue value;
                if (ParamFromJson(raw, value)) {
                    params[name] = value;
                }
            }
            config.wallpaperSettings[key] = std::move(params);
        }
    }
    if (const auto it = root.find("pause"); it != root.end()) {
        ReadValue(*it, "onFullscreenApp", config.pause.onFullscreenApp);
        ReadValue(*it, "onMaximizedApp", config.pause.onMaximizedApp);
        ReadValue(*it, "onBattery", config.pause.onBattery);
        ReadValue(*it, "onDisplayOff", config.pause.onDisplayOff);
        ReadValue(*it, "onSessionLocked", config.pause.onSessionLocked);
        ReadValue(*it, "releaseGpuResources", config.pause.releaseGpuResources);
    }
    if (const auto it = root.find("audio"); it != root.end()) {
        ReadValue(*it, "volume", config.audio.volume);
        ReadValue(*it, "muted", config.audio.muted);
        ReadValue(*it, "reactive", config.audio.reactive);
        ReadValue(*it, "sensitivity", config.audio.sensitivity);
        ReadValue(*it, "autoGain", config.audio.autoGain);
    }
    if (const auto it = root.find("playlist"); it != root.end() && it->is_object()) {
        ReadValue(*it, "enabled", config.playlist.enabled);
        ReadValue(*it, "intervalMinutes", config.playlist.intervalMinutes);
        ReadValue(*it, "shuffle", config.playlist.shuffle);
        ReadValue(*it, "advanceOnStartup", config.playlist.advanceOnStartup);
        config.playlist.items = PathListFromJson(*it, "items");
    }
    ReadValue(root, "startWithWindows", config.startWithWindows);
    config.libraryFolders = PathListFromJson(root, "libraryFolders");

    // Migration v1 -> v2 : le lancement avec Windows devient le comportement par defaut.
    if (config.version < 2) {
        config.startWithWindows = true;
    }

    config.Sanitize();
    out = std::move(config);
    return true;
}

}  // namespace lwe
