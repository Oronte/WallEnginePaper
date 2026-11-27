#include "config/AppConfig.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iterator>
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

constexpr const char* kTransitionNames[] = {"fade", "dissolve", "wipe", "zoomFade", "random"};

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

json TransitionToJson(const TransitionSettings& transition) {
    return json{{"type", ToString(transition.type)}, {"seconds", transition.seconds}};
}

void TransitionFromJson(const json& object, TransitionSettings& out) {
    if (!object.is_object()) {
        return;
    }
    std::string type;
    ReadValue(object, "type", type);
    if (const auto parsed = ParseTransitionType(type)) {
        out.type = *parsed;
    }
    ReadValue(object, "seconds", out.seconds);
}

void SanitizeTransition(TransitionSettings& transition) {
    if (!std::isfinite(transition.seconds)) {
        transition.seconds = 1.0f;
    }
    transition.seconds = std::clamp(transition.seconds, 0.0f, 3.0f);
}

// --- Raccourcis ---------------------------------------------------------------------------------
constexpr uint32_t kModAlt = 0x1;
constexpr uint32_t kModControl = 0x2;
constexpr uint32_t kModShift = 0x4;
constexpr uint32_t kModWin = 0x8;
constexpr const char* kHotkeyActionNames[kHotkeyActionCount] = {"next", "previous", "pause", "mute", "togglePanel"};

struct KeyName {
    uint32_t key;
    const char* name;     // JSON
    const char* display;  // interface
};

constexpr KeyName kNamedKeys[] = {
    {0x25, "Left", "Flèche gauche"},  {0x26, "Up", "Flèche haut"},     {0x27, "Right", "Flèche droite"},
    {0x28, "Down", "Flèche bas"},     {0x24, "Home", "Origine"},       {0x23, "End", "Fin"},
    {0x21, "PageUp", "Page préc."},   {0x22, "PageDown", "Page suiv."}, {0x2D, "Insert", "Inser"},
    {0x2E, "Delete", "Suppr"},        {0x20, "Space", "Espace"},        {0x13, "Pause", "Pause"},
    {0x6A, "NumMultiply", "Num *"},   {0x6B, "NumAdd", "Num +"},        {0x6D, "NumSubtract", "Num -"},
    {0x6E, "NumDecimal", "Num ."},    {0x6F, "NumDivide", "Num /"},     {0xAD, "VolumeMute", "Muet (clavier)"},
    {0xAE, "VolumeDown", "Volume -"}, {0xAF, "VolumeUp", "Volume +"},   {0xB0, "MediaNext", "Piste suivante"},
    {0xB1, "MediaPrev", "Piste précédente"}, {0xB2, "MediaStop", "Stop"}, {0xB3, "MediaPlayPause", "Lecture/Pause"},
};

std::string KeyToString(uint32_t key, bool display) {
    if ((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9')) {
        return std::string(1, static_cast<char>(key));
    }
    if (key >= 0x70 && key <= 0x87) {
        return "F" + std::to_string(key - 0x70 + 1);
    }
    if (key >= 0x60 && key <= 0x69) {
        return (display ? "Num " : "Num") + std::to_string(key - 0x60);
    }
    for (const auto& named : kNamedKeys) {
        if (named.key == key) {
            return display ? named.display : named.name;
        }
    }
    return "VK" + std::to_string(key);
}

std::optional<uint32_t> KeyFromString(std::string_view text) {
    if (text.size() == 1) {
        const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])));
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
            return static_cast<uint32_t>(c);
        }
    }
    const auto number = [](std::string_view digits) -> std::optional<uint32_t> {
        uint32_t value = 0;
        if (digits.empty() || digits.size() > 3) {
            return std::nullopt;
        }
        for (const char c : digits) {
            if (c < '0' || c > '9') {
                return std::nullopt;
            }
            value = value * 10 + static_cast<uint32_t>(c - '0');
        }
        return value;
    };
    if (text.size() >= 2 && (text[0] == 'F' || text[0] == 'f')) {
        if (const auto n = number(text.substr(1)); n && *n >= 1 && *n <= 24) {
            return 0x70 + *n - 1;
        }
    }
    if (text.starts_with("Num")) {
        if (const auto n = number(text.substr(3)); n && *n <= 9) {
            return 0x60 + *n;
        }
    }
    if (text.starts_with("VK")) {
        if (const auto n = number(text.substr(2)); n && *n > 0 && *n < 256) {
            return *n;
        }
    }
    for (const auto& named : kNamedKeys) {
        if (text == named.name) {
            return named.key;
        }
    }
    return std::nullopt;
}

// --- Serialisation des nouveaux reglages ----------------------------------------------------------
constexpr const char* kScheduleModeNames[] = {"timeSlots", "sun", "theme"};
constexpr const char* kReducedMotionNames[] = {"auto", "on", "off"};

json OptionalRefToJson(const WallpaperRef& ref) {
    return ref.Empty() ? json(nullptr) : RefToJson(ref);
}

void OptionalRefFromJson(const json& object, const char* key, WallpaperRef& out) {
    if (const auto it = object.find(key); it != object.end()) {
        RefFromJson(*it, out);
    }
}

json HotkeysToJson(const HotkeySettings& hotkeys) {
    json keys = json::object();
    for (size_t i = 0; i < kHotkeyActionCount; ++i) {
        keys[kHotkeyActionNames[i]] = FormatHotkey(hotkeys.keys[i]);
    }
    return json{{"enabled", hotkeys.enabled}, {"keys", std::move(keys)}};
}

void HotkeysFromJson(const json& object, HotkeySettings& out) {
    if (!object.is_object()) {
        return;
    }
    ReadValue(object, "enabled", out.enabled);
    const auto keys = object.find("keys");
    if (keys == object.end() || !keys->is_object()) {
        return;
    }
    for (size_t i = 0; i < kHotkeyActionCount; ++i) {
        std::string text;
        ReadValue(*keys, kHotkeyActionNames[i], text);
        if (keys->contains(kHotkeyActionNames[i])) {
            out.keys[i] = text.empty() ? Hotkey{} : ParseHotkey(text).value_or(out.keys[i]);
        }
    }
}

json ScheduleToJson(const ScheduleSettings& schedule) {
    json slots = json::array();
    for (const auto& slot : schedule.slots) {
        slots.push_back(json{{"start", slot.startMinutes}, {"end", slot.endMinutes}, {"wallpaper", OptionalRefToJson(slot.wallpaper)}});
    }
    return json{
        {"enabled", schedule.enabled},
        {"mode", kScheduleModeNames[static_cast<size_t>(schedule.mode)]},
        {"slots", std::move(slots)},
        {"latitude", schedule.latitude},
        {"longitude", schedule.longitude},
        {"sunriseOffsetMinutes", schedule.sunriseOffsetMinutes},
        {"sunsetOffsetMinutes", schedule.sunsetOffsetMinutes},
        {"dayWallpaper", OptionalRefToJson(schedule.dayWallpaper)},
        {"nightWallpaper", OptionalRefToJson(schedule.nightWallpaper)},
        {"lightWallpaper", OptionalRefToJson(schedule.lightWallpaper)},
        {"darkWallpaper", OptionalRefToJson(schedule.darkWallpaper)},
    };
}

void ScheduleFromJson(const json& object, ScheduleSettings& out) {
    if (!object.is_object()) {
        return;
    }
    ReadValue(object, "enabled", out.enabled);
    std::string mode;
    ReadValue(object, "mode", mode);
    for (size_t i = 0; i < std::size(kScheduleModeNames); ++i) {
        if (mode == kScheduleModeNames[i]) {
            out.mode = static_cast<ScheduleMode>(i);
        }
    }
    if (const auto it = object.find("slots"); it != object.end() && it->is_array()) {
        out.slots.clear();
        for (const auto& item : *it) {
            TimeSlot slot;
            ReadValue(item, "start", slot.startMinutes);
            ReadValue(item, "end", slot.endMinutes);
            OptionalRefFromJson(item, "wallpaper", slot.wallpaper);
            out.slots.push_back(std::move(slot));
        }
    }
    ReadValue(object, "latitude", out.latitude);
    ReadValue(object, "longitude", out.longitude);
    ReadValue(object, "sunriseOffsetMinutes", out.sunriseOffsetMinutes);
    ReadValue(object, "sunsetOffsetMinutes", out.sunsetOffsetMinutes);
    OptionalRefFromJson(object, "dayWallpaper", out.dayWallpaper);
    OptionalRefFromJson(object, "nightWallpaper", out.nightWallpaper);
    OptionalRefFromJson(object, "lightWallpaper", out.lightWallpaper);
    OptionalRefFromJson(object, "darkWallpaper", out.darkWallpaper);
}

json ProfilesToJson(const ProfileSettings& settings) {
    json profiles = json::array();
    for (const auto& p : settings.profiles) {
        profiles.push_back(json{{"name", p.name},
                                {"fpsLimit", p.fpsLimit},
                                {"renderScale", p.renderScale},
                                {"pauseOnFullscreen", p.pauseOnFullscreen},
                                {"pauseOnMaximized", p.pauseOnMaximized},
                                {"pauseOnBattery", p.pauseOnBattery},
                                {"volume", p.volume},
                                {"muted", p.muted}});
    }
    json rules = json::array();
    for (const auto& r : settings.rules) {
        rules.push_back(json{{"executable", r.executable}, {"profile", r.profile}});
    }
    return json{{"active", settings.active},
                {"autoSwitch", settings.autoSwitch},
                {"batteryProfile", settings.batteryProfile},
                {"rules", std::move(rules)},
                {"profiles", std::move(profiles)}};
}

void ProfilesFromJson(const json& object, ProfileSettings& out) {
    if (!object.is_object()) {
        return;
    }
    ReadValue(object, "active", out.active);
    ReadValue(object, "autoSwitch", out.autoSwitch);
    ReadValue(object, "batteryProfile", out.batteryProfile);
    if (const auto it = object.find("rules"); it != object.end() && it->is_array()) {
        out.rules.clear();
        for (const auto& item : *it) {
            ProfileRule rule;
            ReadValue(item, "executable", rule.executable);
            ReadValue(item, "profile", rule.profile);
            out.rules.push_back(std::move(rule));
        }
    }
    if (const auto it = object.find("profiles"); it != object.end() && it->is_array()) {
        out.profiles.clear();
        for (const auto& item : *it) {
            Profile p;
            ReadValue(item, "name", p.name);
            ReadValue(item, "fpsLimit", p.fpsLimit);
            ReadValue(item, "renderScale", p.renderScale);
            ReadValue(item, "pauseOnFullscreen", p.pauseOnFullscreen);
            ReadValue(item, "pauseOnMaximized", p.pauseOnMaximized);
            ReadValue(item, "pauseOnBattery", p.pauseOnBattery);
            ReadValue(item, "volume", p.volume);
            ReadValue(item, "muted", p.muted);
            out.profiles.push_back(std::move(p));
        }
    }
}

}  // namespace

const char* ToString(HotkeyAction action) noexcept {
    const auto index = static_cast<size_t>(action);
    return index < kHotkeyActionCount ? kHotkeyActionNames[index] : "next";
}

std::string FormatHotkey(const Hotkey& hotkey, bool display) {
    if (hotkey.Empty()) {
        return display ? "Aucun" : "";
    }
    const char* separator = display ? " + " : "+";
    std::string text;
    const auto add = [&](const char* part) {
        if (!text.empty()) {
            text += separator;
        }
        text += part;
    };
    if (hotkey.modifiers & kModControl) {
        add("Ctrl");
    }
    if (hotkey.modifiers & kModAlt) {
        add("Alt");
    }
    if (hotkey.modifiers & kModShift) {
        add(display ? "Maj" : "Shift");
    }
    if (hotkey.modifiers & kModWin) {
        add("Win");
    }
    add(KeyToString(hotkey.key, display).c_str());
    return text;
}

std::optional<Hotkey> ParseHotkey(std::string_view text) {
    Hotkey hotkey;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('+', start);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        std::string_view part = text.substr(start, end - start);
        while (!part.empty() && part.front() == ' ') {
            part.remove_prefix(1);
        }
        while (!part.empty() && part.back() == ' ') {
            part.remove_suffix(1);
        }
        const bool last = end == text.size();
        if (part == "Ctrl") {
            hotkey.modifiers |= kModControl;
        } else if (part == "Alt") {
            hotkey.modifiers |= kModAlt;
        } else if (part == "Shift" || part == "Maj") {
            hotkey.modifiers |= kModShift;
        } else if (part == "Win") {
            hotkey.modifiers |= kModWin;
        } else if (last) {
            const auto key = KeyFromString(part);
            if (!key) {
                return std::nullopt;
            }
            hotkey.key = *key;
        } else {
            return std::nullopt;
        }
        start = end + 1;
        if (last) {
            break;
        }
    }
    if (hotkey.key == 0) {
        return std::nullopt;
    }
    return hotkey;
}

std::array<Hotkey, kHotkeyActionCount> HotkeySettings::DefaultHotkeys() noexcept {
    constexpr uint32_t kCtrlAlt = kModControl | kModAlt;
    return {Hotkey{kCtrlAlt, 0x27}, Hotkey{kCtrlAlt, 0x25}, Hotkey{kCtrlAlt, 'P'}, Hotkey{kCtrlAlt, 'M'},
            Hotkey{kCtrlAlt, 'W'}};
}

std::vector<Profile> ProfileSettings::DefaultProfiles() {
    Profile gaming;
    gaming.name = "Gaming";
    gaming.fpsLimit = 30;
    gaming.renderScale = 0.75f;
    gaming.pauseOnMaximized = true;
    gaming.volume = 0.0f;
    gaming.muted = true;

    Profile work;
    work.name = "Travail";
    work.fpsLimit = 30;
    work.pauseOnMaximized = true;
    work.volume = 0.3f;

    Profile battery;
    battery.name = "Batterie";
    battery.fpsLimit = 15;
    battery.renderScale = 0.5f;
    battery.pauseOnMaximized = true;
    battery.pauseOnBattery = false;  // fond ralenti plutot qu'arrete
    battery.volume = 0.3f;
    return {gaming, work, battery};
}

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

const char* ToString(TransitionType type) noexcept {
    const auto index = static_cast<size_t>(type);
    return index < std::size(kTransitionNames) ? kTransitionNames[index] : "fade";
}

std::optional<TransitionType> ParseTransitionType(std::string_view text) noexcept {
    for (size_t i = 0; i < std::size(kTransitionNames); ++i) {
        if (text == kTransitionNames[i]) {
            return static_cast<TransitionType>(i);
        }
    }
    return std::nullopt;
}

TransitionType ResolveTransitionType(TransitionType type, float random01) noexcept {
    if (type != TransitionType::Random) {
        return type;
    }
    constexpr int kConcrete = kTransitionTypeCount - 1;
    const int index = std::clamp(static_cast<int>(random01 * kConcrete), 0, kConcrete - 1);
    return static_cast<TransitionType>(index);
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
    const auto finiteOr = [](float value, float fallback) { return std::isfinite(value) ? value : fallback; };
    audio.beatSensitivity = std::clamp(finiteOr(audio.beatSensitivity, 1.0f), 0.25f, 2.0f);
    audio.attackMs = std::clamp(finiteOr(audio.attackMs, 10.0f), 0.0f, 500.0f);
    audio.releaseMs = std::clamp(finiteOr(audio.releaseMs, 200.0f), 10.0f, 3000.0f);
    playlist.intervalMinutes = std::clamp<uint32_t>(playlist.intervalMinutes, 1, 24 * 60);
    SanitizeTransition(transition);
    SanitizeTransition(playlist.transition);

    for (auto& slot : schedule.slots) {
        slot.startMinutes = std::min<uint32_t>(slot.startMinutes, 1439);
        slot.endMinutes = std::min<uint32_t>(slot.endMinutes, 1440);
    }
    if (!std::isfinite(schedule.latitude)) {
        schedule.latitude = 0.0;
    }
    if (!std::isfinite(schedule.longitude)) {
        schedule.longitude = 0.0;
    }
    schedule.latitude = std::clamp(schedule.latitude, -90.0, 90.0);
    schedule.longitude = std::clamp(schedule.longitude, -180.0, 180.0);
    schedule.sunriseOffsetMinutes = std::clamp(schedule.sunriseOffsetMinutes, -240, 240);
    schedule.sunsetOffsetMinutes = std::clamp(schedule.sunsetOffsetMinutes, -240, 240);

    // Profils : noms non vides et uniques, valeurs bornees ; regles orphelines conservees (profil recree plus tard).
    std::vector<Profile> uniqueProfiles;
    for (auto& p : profiles.profiles) {
        if (p.name.empty() || std::any_of(uniqueProfiles.begin(), uniqueProfiles.end(),
                                          [&](const Profile& other) { return other.name == p.name; })) {
            continue;
        }
        p.fpsLimit = std::clamp<uint32_t>(p.fpsLimit, 1, 360);
        p.renderScale = std::clamp(std::isfinite(p.renderScale) ? p.renderScale : 1.0f, 0.25f, 1.0f);
        p.volume = std::clamp(std::isfinite(p.volume) ? p.volume : 0.5f, 0.0f, 1.0f);
        uniqueProfiles.push_back(p);
    }
    profiles.profiles = std::move(uniqueProfiles);
    std::erase_if(profiles.rules, [](const ProfileRule& rule) { return rule.executable.empty(); });
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
        {"beatSensitivity", config.audio.beatSensitivity},
        {"attackMs", config.audio.attackMs},
        {"releaseMs", config.audio.releaseMs},
    };
    root["playlist"] = {
        {"enabled", config.playlist.enabled},
        {"intervalMinutes", config.playlist.intervalMinutes},
        {"shuffle", config.playlist.shuffle},
        {"advanceOnStartup", config.playlist.advanceOnStartup},
        {"items", PathListToJson(config.playlist.items)},
        {"customTransition", config.playlist.customTransition},
        {"transition", TransitionToJson(config.playlist.transition)},
    };
    root["transition"] = TransitionToJson(config.transition);
    root["hotkeys"] = HotkeysToJson(config.hotkeys);
    root["schedule"] = ScheduleToJson(config.schedule);
    root["profiles"] = ProfilesToJson(config.profiles);
    root["reducedMotion"] = kReducedMotionNames[static_cast<size_t>(config.reducedMotion)];
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
        ReadValue(*it, "beatSensitivity", config.audio.beatSensitivity);
        ReadValue(*it, "attackMs", config.audio.attackMs);
        ReadValue(*it, "releaseMs", config.audio.releaseMs);
    }
    if (const auto it = root.find("playlist"); it != root.end() && it->is_object()) {
        ReadValue(*it, "enabled", config.playlist.enabled);
        ReadValue(*it, "intervalMinutes", config.playlist.intervalMinutes);
        ReadValue(*it, "shuffle", config.playlist.shuffle);
        ReadValue(*it, "advanceOnStartup", config.playlist.advanceOnStartup);
        config.playlist.items = PathListFromJson(*it, "items");
        ReadValue(*it, "customTransition", config.playlist.customTransition);
        if (const auto t = it->find("transition"); t != it->end()) {
            TransitionFromJson(*t, config.playlist.transition);
        }
    }
    if (const auto it = root.find("transition"); it != root.end()) {
        TransitionFromJson(*it, config.transition);
    }
    if (const auto it = root.find("hotkeys"); it != root.end()) {
        HotkeysFromJson(*it, config.hotkeys);
    }
    if (const auto it = root.find("schedule"); it != root.end()) {
        ScheduleFromJson(*it, config.schedule);
    }
    if (const auto it = root.find("profiles"); it != root.end()) {
        ProfilesFromJson(*it, config.profiles);
    }
    std::string reducedMotion;
    ReadValue(root, "reducedMotion", reducedMotion);
    for (size_t i = 0; i < std::size(kReducedMotionNames); ++i) {
        if (reducedMotion == kReducedMotionNames[i]) {
            config.reducedMotion = static_cast<ReducedMotion>(i);
        }
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
