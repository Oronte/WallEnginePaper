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

// --- Raccourcis clavier globaux -------------------------------------------------------------
enum class HotkeyAction : uint8_t { Next, Previous, Pause, Mute, TogglePanel };
inline constexpr size_t kHotkeyActionCount = 5;
const char* ToString(HotkeyAction action) noexcept;  // cle JSON stable

// Combinaison : modificateurs MOD_ALT (1) | MOD_CONTROL (2) | MOD_SHIFT (4) | MOD_WIN (8) + code de touche virtuelle.
struct Hotkey {
    uint32_t modifiers = 0;
    uint32_t key = 0;  // 0 = aucun raccourci
    bool Empty() const noexcept { return key == 0; }
    bool operator==(const Hotkey&) const = default;
};
// "Ctrl+Alt+Right" (JSON) <-> Hotkey ; FormatHotkey(..., true) donne un libelle lisible ("Ctrl + Alt + Flèche droite").
std::string FormatHotkey(const Hotkey& hotkey, bool display = false);
std::optional<Hotkey> ParseHotkey(std::string_view text);

struct HotkeySettings {
    bool enabled = true;
    std::array<Hotkey, kHotkeyActionCount> keys = DefaultHotkeys();
    static std::array<Hotkey, kHotkeyActionCount> DefaultHotkeys() noexcept;
};

// --- Planification ---------------------------------------------------------------------------
enum class ScheduleMode : uint8_t { TimeSlots, Sun, Theme };

struct TimeSlot {
    uint32_t startMinutes = 7 * 60;  // minutes depuis minuit
    uint32_t endMinutes = 19 * 60;   // fin exclue ; fin <= debut : la plage passe minuit
    WallpaperRef wallpaper;
    bool operator==(const TimeSlot&) const = default;
};

struct ScheduleSettings {
    bool enabled = false;
    ScheduleMode mode = ScheduleMode::TimeSlots;
    std::vector<TimeSlot> slots;
    // Soleil : coordonnees saisies a la main (aucune geolocalisation), decalages en minutes.
    double latitude = 48.8566;
    double longitude = 2.3522;
    int sunriseOffsetMinutes = 0;
    int sunsetOffsetMinutes = 0;
    WallpaperRef dayWallpaper;
    WallpaperRef nightWallpaper;
    // Theme clair / sombre de Windows (AppsUseLightTheme).
    WallpaperRef lightWallpaper;
    WallpaperRef darkWallpaper;
};

// --- Profils ---------------------------------------------------------------------------------
struct Profile {
    std::string name;
    uint32_t fpsLimit = 60;
    float renderScale = 1.0f;
    bool pauseOnFullscreen = true;
    bool pauseOnMaximized = false;
    bool pauseOnBattery = true;
    float volume = 0.5f;
    bool muted = false;
    bool operator==(const Profile&) const = default;
};

// Bascule automatique : quand executable (ex. "game.exe", insensible a la casse) est au premier plan.
struct ProfileRule {
    std::string executable;
    std::string profile;
    bool operator==(const ProfileRule&) const = default;
};

struct ProfileSettings {
    std::string active;        // profil choisi a la main ("" = reglages de base, sans profil)
    bool autoSwitch = true;    // regles par executable
    std::string batteryProfile = "Batterie";  // applique sur batterie ("" = aucun)
    std::vector<ProfileRule> rules;
    std::vector<Profile> profiles = DefaultProfiles();
    static std::vector<Profile> DefaultProfiles();
};

// --- Effets superposables (par fond) ------------------------------------------------------------
// Post-effets appliques a l'image finale (shader, video, image ; pas les fonds web qui s'affichent eux-memes).
enum class EffectId : uint8_t { Chromatic, Blur, Bloom, Vignette, Grain, Scanlines };
inline constexpr size_t kEffectCount = 6;
enum class OverlayType : uint8_t { None, Rain, Snow, Fireflies, Dust };
inline constexpr size_t kOverlayCount = 5;
const char* ToString(EffectId effect) noexcept;
const char* ToString(OverlayType overlay) noexcept;

struct EffectSettings {
    std::array<float, kEffectCount> strength{};  // 0 = desactive (aucun cout), 1 = fort
    std::array<EffectId, kEffectCount> order{EffectId::Chromatic, EffectId::Blur,  EffectId::Bloom,
                                             EffectId::Vignette,  EffectId::Grain, EffectId::Scanlines};
    OverlayType overlay = OverlayType::None;
    float overlayIntensity = 0.6f;
    float overlayAudio = 0.5f;   // reaction aux graves / beats
    float overlayMouse = 0.5f;   // reaction a la souris
    bool ripples = false;        // ondulations au clic sur le bureau
    float rippleStrength = 0.5f;
    bool trail = false;          // trainee lumineuse derriere le curseur
    Color trailColor{0.55f, 0.42f, 1.0f, 1.0f};

    float Strength(EffectId effect) const noexcept { return strength[static_cast<size_t>(effect)]; }
    // Au moins un effet, overlay ou interaction actif : sinon le fond est dessine directement, sans cout.
    bool Active() const noexcept;
    // Doit etre redessine a chaque image meme si le fond n'a pas change (grain, particules).
    bool Animated() const noexcept;
    bool operator==(const EffectSettings&) const = default;
};

// Integration systeme pour les fonds web (collectee seulement si une page s'abonne).
struct IntegrationSettings {
    bool media = true;        // morceau en cours (controles multimedia de Windows)
    bool systemStats = true;  // CPU, RAM, GPU, batterie
};

// Accessibilite : "Afficher les animations dans Windows" (SPI_GETCLIENTAREAANIMATION).
enum class ReducedMotion : uint8_t { Auto, On, Off };

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
    std::map<std::string, EffectSettings> effects;          // cle = WallpaperSettingsKey(source)

    PauseRules pause;
    AudioSettings audio;
    PlaylistSettings playlist;
    TransitionSettings transition;
    HotkeySettings hotkeys;
    ScheduleSettings schedule;
    ProfileSettings profiles;
    ReducedMotion reducedMotion = ReducedMotion::Auto;
    IntegrationSettings integration;
    bool startWithWindows = true;
    std::vector<std::filesystem::path> libraryFolders;

    void Sanitize();
};

// Cle stable (minuscules, separateurs '/') pour indexer les reglages d'un fond.
std::string WallpaperSettingsKey(const std::filesystem::path& source);

std::string SerializeConfig(const AppConfig& config);
bool DeserializeConfig(std::string_view text, AppConfig& out, std::string& error);

}  // namespace lwe
