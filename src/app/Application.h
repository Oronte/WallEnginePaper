#pragma once

#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <random>
#include <vector>

#include "app/AppServices.h"
#include "audio/AudioCapture.h"
#include "config/ConfigManager.h"
#include "core/Win32Util.h"
#include "desktop/DesktopHost.h"
#include "desktop/MonitorContext.h"
#include "input/InputForwarder.h"
#include "library/Library.h"
#include "render/FrameLimiter.h"
#include "render/RenderDevice.h"
#include "ui/ControlPanel.h"
#include "ui/TrayIcon.h"
#include "watchdog/PerformanceWatchdog.h"

namespace lwe {

struct CommandLineOptions {
    bool quit = false;                        // --quit : ferme l'instance en cours
    bool minimized = false;                   // --minimized : demarre sans ouvrir l'interface (demarrage Windows)
    double exitAfterSeconds = 0.0;            // --exit-after <s> : arret automatique (tests)
    std::filesystem::path wallpaperOverride;  // --wallpaper <fichier> : force un fond (non sauvegarde)
    bool audioDebug = false;                  // --audio-debug : statistiques audio chaque seconde dans le journal
    bool noAutoPause = false;                 // --no-autopause : ignore plein ecran / batterie (mesures)
    double nextEverySeconds = 0.0;            // --next-every <s> : fond suivant periodique (tests des transitions)
};

CommandLineOptions ParseCommandLine();

// Orchestrateur : fenetre de controle cachee (messages systeme, tray), boucle de rendu cadencee,
// cycle de vie des surfaces par ecran, regles de pause, playlist et interface de configuration.
class Application final : public IAppServices {
public:
    Application(HINSTANCE instance, CommandLineOptions options);
    ~Application() override;
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    int Run();

    // --- IAppServices ------------------------------------------------------------------------
    ConfigManager& Config() override { return config_; }
    RenderDevice& Device() override { return device_; }
    const Library& GetLibrary() const override { return library_; }
    void RescanLibrary() override;
    bool ImportIntoLibrary(const std::filesystem::path& file, std::wstring& error) override;
    void SetLibraryFolders(std::vector<std::filesystem::path> folders) override;
    std::vector<MonitorEntry> Monitors() const override;
    std::vector<SurfaceStatusInfo> Surfaces() const override;
    void ApplyWallpaper(const LibraryItem& item, const std::string& monitorId) override;
    void SetWallpaperParameter(const LibraryItem& item, const std::string& name, const ParamValue& value) override;
    void ResetWallpaperParameters(const LibraryItem& item) override;
    void NextWallpaper() override;
    IWallpaperBackend* LiveBackendFor(const LibraryItem& item) override;
    void SetVolume(float volume) override;
    void SetMuted(bool muted) override;
    void SetAudioReactive(bool reactive) override;
    void SetAudioSensitivity(float sensitivity) override;
    void SetAudioAutoGain(bool enabled) override;
    void SetAudioBeatSensitivity(float sensitivity) override;
    void SetAudioSmoothing(float attackMs, float releaseMs) override;
    void CurrentAudio(AudioSpectrum& out) const override;
    std::wstring AudioDeviceName() const override { return audioCapture_.DeviceName(); }
    bool IsManuallyPaused() const override { return (pauseReasons_ & kPauseManual) != 0; }
    void SetManualPause(bool paused) override { SetPauseReason(kPauseManual, paused); }
    std::wstring StatusText() const override;
    bool IsRendering() const override;
    float MeasuredFps() const override { return measuredFps_; }
    void SetFpsLimit(uint32_t fps) override;
    void SetRenderScale(float scale) override;
    void SetLayout(LayoutMode layout) override;
    void SetStartWithWindows(bool enabled) override;
    void RestartPlaylistTimer() override;
    double MinutesUntilPlaylistChange() const override;
    void OpenConfigFolder() override;
    void OpenLogFile() override;

private:
    enum PauseReason : uint32_t {
        kPauseManual = 1u << 0,
        kPauseDisplayOff = 1u << 1,
        kPauseSessionLocked = 1u << 2,
        kPauseBattery = 1u << 3,
    };

    bool Initialize();
    void Shutdown();
    bool CreateControlWindow();
    void RunMessageLoop();

    void RebuildSurfaces();
    void ScheduleRebuild();
    void DestroySurfaces();
    void InstallWallpaper(MonitorContext& surface, const WallpaperDesc& desc, const TransitionRequest& transition = {});
    // Reglage -> transition concrete (type aleatoire resolu, graine tiree) ; coupe nette si desactivee.
    TransitionRequest MakeTransition(const TransitionSettings& settings);
    const TransitionSettings& PlaylistTransition() const;
    WallpaperRef AssignedWallpaper(const MonitorInfo& monitor) const;
    void AssignWallpaper(const MonitorInfo& monitor, const WallpaperRef& ref);
    std::wstring MonitorLabel(const MonitorInfo& monitor) const;

    FrameTiming NextTiming();
    void RenderFrame();
    void UpdateIdleState(bool anyPresented);
    void RecoverFromDeviceLoss();

    void SetPauseReason(uint32_t reason, bool active);
    bool IsPaused() const noexcept { return pauseReasons_ != 0; }
    bool AnySurfaceRunning() const;
    void ApplyPauseState();
    void ApplyAudioOutput();
    void UpdateAudioCapture();
    void ReplaceFailedBackends();
    void WatchdogTick();
    PauseRules EffectivePauseRules() const;
    void MaintenanceTick();
    void PlaylistTick();
    std::vector<const LibraryItem*> RotationCandidates() const;
    void AdvanceWallpapers();

    void OpenControlPanel();
    void CloseControlPanel();
    void RenderControlPanel();

    void ShowTrayMenu();
    void ExecuteCommand(UINT command);
    void UpdateTrayTooltip();
    void LogSessionStatistics() const;
    double ProcessCpuMs() const;

    static LRESULT CALLBACK StaticWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

    HINSTANCE instance_;
    CommandLineOptions options_;
    UniqueHandle instanceMutex_;

    ConfigManager config_;
    RenderDevice device_;
    DesktopHost desktop_;
    InputForwarder input_;
    Library library_;
    PerformanceWatchdog watchdog_;
    FrameLimiter limiter_;
    FrameLimiter uiLimiter_;
    TrayIcon tray_;
    AudioCapture audioCapture_;
    AudioSpectrum audio_;  // spectre de l'image en cours (sensibilite globale appliquee)
    // Declares apres device_/desktop_ : detruits avant eux.
    std::vector<MonitorInfo> monitors_;
    std::vector<std::unique_ptr<MonitorContext>> surfaces_;
    std::unique_ptr<ControlPanel> panel_;

    HWND controlWindow_ = nullptr;
    UINT taskbarCreatedMessage_ = 0;
    HPOWERNOTIFY displayNotification_ = nullptr;
    bool sessionNotificationsRegistered_ = false;

    uint32_t pauseReasons_ = 0;
    bool running_ = false;
    bool shutDown_ = false;
    bool trayHintShown_ = false;
    bool mediaFoundationStarted_ = false;

    int64_t qpcFrequency_ = 1;
    int64_t qpcStart_ = 0;
    int64_t qpcLast_ = 0;
    int64_t uiQpcLast_ = 0;
    uint64_t frameIndex_ = 0;
    uint64_t startTickMs_ = 0;
    uint64_t loopWakeups_ = 0;
    uint64_t framesRendered_ = 0;
    uint32_t ticksWithoutSurface_ = 0;

    uint64_t fpsWindowStartMs_ = 0;
    uint64_t fpsWindowFrames_ = 0;
    float measuredFps_ = 0.0f;

    std::map<std::string, int> failureRetries_;  // par ecran : nouvel essai avant le shader de secours
    uint64_t playlistAnchorMs_ = 0;
    uint64_t lastForcedNextMs_ = 0;
    // Repos : aucune surface n'a rien presente depuis un moment et la souris ne bouge pas (image fixe,
    // video entre deux trames lentes...) -> la boucle ne se reveille plus que ~10 fois par seconde.
    uint32_t idleFrames_ = 0;
    int64_t idleUntilQpc_ = 0;
    POINT lastCursor_{};
    bool lastPrimaryDown_ = false;
    // Mesure en regime etabli : temps CPU et images au bout de 5 s (chargement exclu).
    double warmCpuMs_ = -1.0;
    uint64_t warmTickMs_ = 0;
    uint64_t warmFrames_ = 0;
    std::mt19937 random_{std::random_device{}()};
};

}  // namespace lwe
