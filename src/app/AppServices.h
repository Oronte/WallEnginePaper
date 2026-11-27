#pragma once

#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "config/AppConfig.h"
#include "control/HotkeyManager.h"
#include "control/Profiles.h"
#include "desktop/MonitorEnumerator.h"
#include "wallpaper/IWallpaperBackend.h"

namespace lwe {

class ConfigManager;
class RenderDevice;
class Library;
struct LibraryItem;

struct MonitorEntry {
    MonitorInfo info;
    int number = 0;      // 1, 2, ... (ecran principal = 1)
    std::wstring label;  // "Écran 1 · LF24T35"
};

struct SurfaceStatusInfo {
    std::string monitorId;  // kSpanMonitorId en mode etendu
    std::wstring label;
    RECT bounds{};
    std::filesystem::path source;  // fond affiche (absolu ; vide = shader de secours)
    bool paused = false;
    bool autoPaused = false;  // recouvert par une application plein ecran
};

// Ce que l'interface peut lire et piloter. Implemente par Application : l'UI ne connait ni les
// surfaces, ni la boucle de rendu, ni le bureau Windows.
class IAppServices {
public:
    virtual ~IAppServices() = default;

    virtual ConfigManager& Config() = 0;
    virtual RenderDevice& Device() = 0;

    // Bibliotheque
    virtual const Library& GetLibrary() const = 0;
    virtual void RescanLibrary() = 0;
    virtual bool ImportIntoLibrary(const std::filesystem::path& file, std::wstring& error) = 0;
    virtual void SetLibraryFolders(std::vector<std::filesystem::path> folders) = 0;

    // Ecrans et fonds affiches
    virtual std::vector<MonitorEntry> Monitors() const = 0;
    virtual std::vector<SurfaceStatusInfo> Surfaces() const = 0;
    virtual void ApplyWallpaper(const LibraryItem& item, const std::string& monitorId) = 0;  // "" = tous
    virtual void SetWallpaperParameter(const LibraryItem& item, const std::string& name, const ParamValue& value) = 0;
    virtual void ResetWallpaperParameters(const LibraryItem& item) = 0;
    // Effets superposables du fond (post-effets, particules, ondulations, trainee).
    virtual void SetWallpaperEffects(const LibraryItem& item, const EffectSettings& effects) = 0;
    virtual void ResetWallpaperEffects(const LibraryItem& item) = 0;
    virtual void NextWallpaper() = 0;
    virtual void PreviousWallpaper() = 0;
    // Backend actuellement affiche pour ce fond (reglages des fonds web, qui n'ont pas d'apercu), ou nullptr.
    virtual IWallpaperBackend* LiveBackendFor(const LibraryItem& item) = 0;

    // Audio des fonds video / web
    virtual void SetVolume(float volume) = 0;
    virtual void SetMuted(bool muted) = 0;

    // Reactivite au son (capture du son joue par le PC + spectre)
    virtual void SetAudioReactive(bool reactive) = 0;
    virtual void SetAudioSensitivity(float sensitivity) = 0;
    virtual void SetAudioAutoGain(bool enabled) = 0;
    virtual void SetAudioBeatSensitivity(float sensitivity) = 0;
    virtual void SetAudioSmoothing(float attackMs, float releaseMs) = 0;
    virtual void CurrentAudio(AudioSpectrum& out) const = 0;  // spectre courant (sensibilite globale appliquee)
    virtual std::wstring AudioDeviceName() const = 0;

    // Etat et performances
    virtual bool IsManuallyPaused() const = 0;
    virtual void SetManualPause(bool paused) = 0;
    virtual std::wstring StatusText() const = 0;
    virtual bool IsRendering() const = 0;
    virtual float MeasuredFps() const = 0;
    virtual void SetFpsLimit(uint32_t fps) = 0;
    virtual void SetRenderScale(float scale) = 0;
    virtual void SetLayout(LayoutMode layout) = 0;

    // Demarrage, playlist, divers
    virtual void SetStartWithWindows(bool enabled) = 0;
    virtual void RestartPlaylistTimer() = 0;
    virtual double MinutesUntilPlaylistChange() const = 0;
    // Raccourcis, planification, profils, accessibilite
    virtual void ApplyHotkeys() = 0;                  // apres modification des raccourcis
    virtual void SuspendHotkeys(bool suspended) = 0;  // pendant la saisie d'un raccourci dans l'interface
    virtual HotkeyStatus HotkeyState(HotkeyAction action) const = 0;
    virtual void RefreshAutomation() = 0;             // planification, profils ou accessibilite modifies
    virtual std::wstring ScheduleStatus() const = 0;
    virtual const EffectiveSettings& Effective() const = 0;
    virtual bool SystemAnimationsEnabled() const = 0;

    virtual void OpenConfigFolder() = 0;
    virtual void OpenLogFile() = 0;
};

}  // namespace lwe
