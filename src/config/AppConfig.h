#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace lwe {

// Familles de fonds d'ecran. Chaque type correspond a une implementation de IWallpaperBackend.
enum class WallpaperType : uint8_t {
    Shader,  // HLSL temps reel, style Shadertoy
    Scene,   // image (animee) + parallaxe souris
    Video,   // Media Foundation, decodage materiel
    Web,     // HTML/CSS/JS/WebGL via WebView2
};

const char* ToString(WallpaperType type) noexcept;
std::optional<WallpaperType> ParseWallpaperType(std::string_view text) noexcept;
std::optional<WallpaperType> GuessWallpaperType(const std::filesystem::path& file);

using Color = std::array<float, 4>;
using ParamValue = std::variant<float, bool, Color>;
using ParamMap = std::map<std::string, ParamValue, std::less<>>;

// Reference persistante a un fond (ce qui est ecrit dans config.json).
struct WallpaperRef {
    WallpaperType type = WallpaperType::Shader;
    std::filesystem::path source;  // relatif au dossier de l'exe, ou absolu

    bool Empty() const noexcept { return source.empty(); }
    bool operator==(const WallpaperRef&) const = default;
};

// Description complete passee a un backend au moment de son initialisation.
struct WallpaperDesc {
    WallpaperType type = WallpaperType::Shader;
    std::filesystem::path source;  // chemin absolu ; vide = shader de secours integre
    ParamMap params;               // reglages utilisateur (vitesse, teinte, sensibilite audio...)
};

enum class LayoutMode : uint8_t {
    PerMonitor,  // un fond independant par ecran
    Span,        // un seul fond etire sur le rectangle englobant tous les ecrans
};

struct PauseRules {
    bool onFullscreenApp = true;   // jeu / application plein ecran ou borderless sur l'ecran concerne
    bool onMaximizedApp = false;   // fenetre maximisee recouvrant l'ecran
    bool onBattery = true;
    bool onDisplayOff = true;
    bool onSessionLocked = true;
    bool releaseGpuResources = true;  // Trim() DXGI + liberation des ressources non critiques en pause
};

struct AudioSettings {
    float volume = 0.5f;  // volume des fonds video/web
    bool muted = false;
    bool reactive = true;  // capture WASAPI loopback + FFT
    float sensitivity = 1.0f;
    bool autoGain = true;  // remonte les sources calmes (jusqu a +18 dB)
    float beatSensitivity = 1.0f;  // 0,25 (beats nets seulement) a 2
    float attackMs = 10.0f;        // montee des niveaux graves / mediums / aigus
    float releaseMs = 200.0f;      // descente
};

// Transition entre deux fonds (changement manuel, playlist, planification).
enum class TransitionType : uint8_t { Fade, Dissolve, Wipe, ZoomFade, Random };
inline constexpr int kTransitionTypeCount = 5;  // Random compris

const char* ToString(TransitionType type) noexcept;
std::optional<TransitionType> ParseTransitionType(std::string_view text) noexcept;
// Random -> un type concret choisi a partir de random01 dans [0, 1[ ; les autres types sont inchanges.
TransitionType ResolveTransitionType(TransitionType type, float random01) noexcept;

struct TransitionSettings {
    TransitionType type = TransitionType::Fade;
    float seconds = 1.0f;  // 0 = coupe nette, 3 au plus

    bool operator==(const TransitionSettings&) const = default;
};

struct PlaylistSettings {
    bool enabled = false;
    uint32_t intervalMinutes = 15;
    bool shuffle = false;
    bool advanceOnStartup = false;
    std::vector<std::filesystem::path> items;
    bool customTransition = false;  // sinon : transition globale (Parametres)
    TransitionSettings transition;
};

struct AppConfig {
    // v2 : renommage WallEnginePaper, lancement avec Windows active par defaut.
    static constexpr int kCurrentVersion = 2;

    int version = kCurrentVersion;
    uint32_t fpsLimit = 60;
    float renderScale = 1.0f;  // < 1 : rendu sous-echantillonne puis etire par le DWM (economie GPU)
    LayoutMode layout = LayoutMode::PerMonitor;

    WallpaperRef defaultWallpaper{WallpaperType::Shader, L"assets/shaders/aurora.hlsl"};
    WallpaperRef spanWallpaper;
    std::map<std::string, WallpaperRef> monitorWallpapers;  // cle = identifiant stable de l'ecran (UTF-8)
    std::map<std::string, ParamMap> wallpaperSettings;      // cle = WallpaperSettingsKey(source)

    PauseRules pause;
    AudioSettings audio;
    PlaylistSettings playlist;
    TransitionSettings transition;
    bool startWithWindows = true;
    std::vector<std::filesystem::path> libraryFolders;

    void Sanitize();
};

// Cle stable (minuscules, separateurs '/') pour indexer les reglages d'un fond.
std::string WallpaperSettingsKey(const std::filesystem::path& source);

std::string SerializeConfig(const AppConfig& config);
bool DeserializeConfig(std::string_view text, AppConfig& out, std::string& error);

}  // namespace lwe
