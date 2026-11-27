#include "app/Application.h"

#include <dxgi.h>
#include <mfapi.h>
#include <wrl/client.h>
#include <shellapi.h>
#include <wtsapi32.h>

#include <algorithm>
#include <cwchar>
#include <format>
#include <limits>
#include <string>

#include "core/Log.h"
#include "core/Paths.h"
#include "core/StringUtil.h"
#include "desktop/MonitorEnumerator.h"
#include "lwe_version.h"
#include "resource.h"
#include "control/Scheduler.h"
#include "system/Autostart.h"
#include "system/SystemInfo.h"
#include "ui/FileDialogs.h"
#include "wallpaper/WallpaperFactory.h"
#include "wallpaper/shader/ShaderBackend.h"

namespace lwe {
namespace {

constexpr wchar_t kControlWindowClass[] = L"WEP_ControlWindow";
constexpr wchar_t kSingleInstanceMutex[] = L"Local\\WallEnginePaper.SingleInstance";

constexpr UINT kMsgSecondInstance = WM_APP + 2;
constexpr UINT_PTR kTimerMaintenance = 1;
constexpr UINT_PTR kTimerRebuild = 2;
constexpr UINT_PTR kTimerWatchdog = 3;
constexpr UINT kMaintenanceIntervalMs = 1000;
constexpr UINT kWatchdogIntervalMs = 500;
constexpr UINT kRebuildDebounceMs = 600;
constexpr uint32_t kRetryWithoutSurfaceTicks = 5;
constexpr uint32_t kUiFps = 60;
constexpr UINT kTrayIconId = 1;
constexpr uint32_t kIdleAfterFrames = 30;  // ~0,5 s sans rien de neuf a 60 FPS
constexpr uint32_t kIdleWakeupsPerSecond = 10;

// GUID_CONSOLE_DISPLAY_STATE (winnt.h), redefini ici pour ne pas dependre de initguid.h.
constexpr GUID kConsoleDisplayState = {0x6fe69556, 0x704a, 0x47a0, {0x8f, 0x24, 0xc2, 0x8d, 0x93, 0x6f, 0xda, 0x47}};

enum Command : UINT {
    kCmdOpen = 100,
    kCmdPause,
    kCmdMute,
    kCmdNext,
    kCmdPrevious,
    kCmdSchedule,
    kCmdNoProfile,
    kCmdProfileBase = 2000,
    kCmdSpan,
    kCmdQuit,
    kCmdFpsBase = 1000,
};
constexpr uint32_t kFpsPresets[] = {15, 30, 60, 144};

int64_t QpcNow() noexcept {
    LARGE_INTEGER value{};
    QueryPerformanceCounter(&value);
    return value.QuadPart;
}

std::wstring PathKey(const std::filesystem::path& path) {
    return ToLower(path.lexically_normal().wstring());
}

std::wstring OsVersionString() {
    using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    RTL_OSVERSIONINFOW info{};
    info.dwOSVersionInfoSize = sizeof(info);
    if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll")) {
        if (auto fn = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion"))) {
            fn(&info);
        }
    }
    return std::format(L"Windows {}.{} build {}", info.dwMajorVersion, info.dwMinorVersion, info.dwBuildNumber);
}

LONG WINAPI CrashFilter(EXCEPTION_POINTERS* exception) {
    const unsigned code =
        exception && exception->ExceptionRecord ? static_cast<unsigned>(exception->ExceptionRecord->ExceptionCode) : 0u;
    log::Error(L"Exception non geree 0x{:08X} : restauration du bureau", code);
    DesktopHost::EmergencyRestore();
    return EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace

CommandLineOptions ParseCommandLine() {
    CommandLineOptions options;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) {
        return options;
    }
    for (int i = 1; i < argc; ++i) {
        const std::wstring_view arg = argv[i];
        if (arg == L"--quit") {
            options.quit = true;
        } else if (arg == L"--minimized") {
            options.minimized = true;
        } else if (arg == L"--exit-after" && i + 1 < argc) {
            options.exitAfterSeconds = std::wcstod(argv[++i], nullptr);
        } else if (arg == L"--capture" && i + 1 < argc) {
            std::error_code ec;
            options.capturePath = std::filesystem::absolute(argv[++i], ec);
        } else if (arg == L"--capture-delay" && i + 1 < argc) {
            options.captureDelaySeconds = std::wcstod(argv[++i], nullptr);
        } else if (arg == L"--capture-width" && i + 1 < argc) {
            options.captureWidth = static_cast<uint32_t>(std::wcstoul(argv[++i], nullptr, 10));
        } else if (arg == L"--next-every" && i + 1 < argc) {
            options.nextEverySeconds = std::wcstod(argv[++i], nullptr);
        } else if (arg == L"--no-autopause") {
            options.noAutoPause = true;
        } else if (arg == L"--audio-debug") {
            options.audioDebug = true;
        } else if (arg == L"--wallpaper" && i + 1 < argc) {
            std::error_code ec;
            options.wallpaperOverride = std::filesystem::absolute(argv[++i], ec);
        }
    }
    LocalFree(argv);
    return options;
}

Application::Application(HINSTANCE instance, CommandLineOptions options)
    : instance_(instance), options_(std::move(options)) {}

Application::~Application() {
    Shutdown();
}

// =============================================================================================== //
//  Cycle de vie
// =============================================================================================== //

int Application::Run() {
    if (options_.quit) {
        if (HWND existing = FindWindowW(kControlWindowClass, nullptr)) {
            PostMessageW(existing, WM_CLOSE, 0, 0);
            return 0;
        }
        return 1;
    }

    ScopedComInit com;
    if (FAILED(com.Result())) {
        return 1;
    }

    HANDLE mutex = CreateMutexW(nullptr, FALSE, kSingleInstanceMutex);
    const DWORD mutexError = GetLastError();
    instanceMutex_.reset(mutex);
    if (mutexError == ERROR_ALREADY_EXISTS) {
        // Deja lance : on demande a l'instance existante d'ouvrir son interface.
        if (HWND existing = FindWindowW(kControlWindowClass, nullptr)) {
            DWORD processId = 0;
            GetWindowThreadProcessId(existing, &processId);
            AllowSetForegroundWindow(processId);
            PostMessageW(existing, kMsgSecondInstance, options_.minimized ? 1 : 0, 0);
        }
        return 0;
    }

    int exitCode = 0;
    try {
        if (Initialize()) {
            RunMessageLoop();
        } else {
            exitCode = 1;
        }
    } catch (const std::exception& e) {
        log::Error(L"Erreur fatale : {}", Utf8ToWide(e.what()));
        exitCode = 2;
    }
    Shutdown();  // avant la fin de vie de `com`
    return exitCode;
}

bool Application::Initialize() {
    const std::filesystem::path dataDir = AppDataDir();
    log::Initialize(dataDir / L"log.txt");
    log::Info(L"{} {} - {}", kProductName, LWE_VERSION_WSTRING, OsVersionString());

    SetUnhandledExceptionFilter(CrashFilter);
    // Le fond d'ecran ne doit jamais voler de temps CPU a l'application au premier plan.
    SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);

    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    qpcFrequency_ = frequency.QuadPart;
    startTickMs_ = GetTickCount64();

    config_.Load(dataDir / L"config.json");
    audioCapture_.SetDebugLogging(options_.audioDebug);
    audioCapture_.SetAutoGain(config_.Data().audio.autoGain);
    audioCapture_.SetBeatSensitivity(config_.Data().audio.beatSensitivity);
    audioCapture_.SetSmoothing(config_.Data().audio.attackMs, config_.Data().audio.releaseMs);
    autostart::Sync(config_.Data().startWithWindows);
    systemAnimations_ = systeminfo::ClientAreaAnimationsEnabled();
    RefreshTheme();
    RefreshEffectiveSettings(config_.Data().profiles.active);
    limiter_.SetTargetFps(effective_.fpsLimit);
    uiLimiter_.SetTargetFps(kUiFps);
    if (!limiter_.UsesHighResolutionTimer()) {
        log::Warn(L"Timer haute resolution indisponible : cadence moins precise");
    }

    if (!CreateControlWindow()) {
        return false;
    }
    taskbarCreatedMessage_ = RegisterWindowMessageW(L"TaskbarCreated");
    ChangeWindowMessageFilterEx(controlWindow_, taskbarCreatedMessage_, MSGFLT_ALLOW, nullptr);

    const int iconSize = GetSystemMetrics(SM_CXSMICON);
    HICON trayIcon = static_cast<HICON>(
        LoadImageW(instance_, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, iconSize, iconSize, LR_SHARED));
    if (!tray_.Add(controlWindow_, kTrayIconId, trayIcon ? trayIcon : LoadIconW(nullptr, IDI_APPLICATION),
                   kProductName)) {
        log::Warn(L"Icone de notification non ajoutee (barre des taches absente ?)");
    }
    displayNotification_ =
        RegisterPowerSettingNotification(controlWindow_, &kConsoleDisplayState, DEVICE_NOTIFY_WINDOW_HANDLE);
    sessionNotificationsRegistered_ = WTSRegisterSessionNotification(controlWindow_, NOTIFY_FOR_THIS_SESSION) != FALSE;
    SetTimer(controlWindow_, kTimerMaintenance, kMaintenanceIntervalMs, nullptr);
    SetTimer(controlWindow_, kTimerWatchdog, kWatchdogIntervalMs, nullptr);
    hotkeys_.Apply(controlWindow_, config_.Data().hotkeys);

    std::wstring error;
    if (!device_.Create(error)) {
        log::Error(L"Direct3D 11 : {}", error);
        MessageBoxW(nullptr, (L"Impossible d'initialiser Direct3D 11.\n\n" + error).c_str(), kProductName,
                    MB_ICONERROR);
        return false;
    }
    // Media Foundation (fonds video, miniatures video). Mode "lite" : pas de sockets, rien que du local.
    mediaFoundationStarted_ = SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_LITE));
    if (!mediaFoundationStarted_) {
        log::Warn(L"Media Foundation indisponible : les fonds video ne pourront pas etre lus");
    }

    library_.Scan(config_.Data().libraryFolders);
    qpcStart_ = qpcLast_ = QpcNow();
    RebuildSurfaces();
    playlistAnchorMs_ = GetTickCount64();
    fpsWindowStartMs_ = GetTickCount64();
    if (config_.Data().playlist.enabled && config_.Data().playlist.advanceOnStartup) {
        AdvanceWallpapers();
    }
    running_ = true;

    if (!options_.minimized) {
        OpenControlPanel();
    }
    return true;
}

bool Application::CreateControlWindow() {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = StaticWindowProc;
    wc.hInstance = instance_;
    wc.lpszClassName = kControlWindowClass;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        log::Error(L"RegisterClassEx (controle) : erreur {}", GetLastError());
        return false;
    }
    // Fenetre de premier niveau invisible (et non message-only) : elle doit recevoir les diffusions
    // TaskbarCreated, WM_DISPLAYCHANGE et WM_POWERBROADCAST.
    controlWindow_ = CreateWindowExW(WS_EX_TOOLWINDOW, kControlWindowClass, kProductName, WS_POPUP, 0, 0, 0, 0,
                                     nullptr, nullptr, instance_, this);
    if (!controlWindow_) {
        log::Error(L"CreateWindowEx (controle) : erreur {}", GetLastError());
        return false;
    }
    return true;
}

void Application::RunMessageLoop() {
    MSG message{};
    while (running_) {
        ++loopWakeups_;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                running_ = false;
                break;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (!running_) {
            break;
        }
        if (panel_ && panel_->CloseRequested()) {
            CloseControlPanel();
        }

        const bool uiActive = panel_ && panel_->IsVisible();
        const bool wallpapersActive = IsRendering();
        if (!uiActive && !wallpapersActive) {
            // Rien a dessiner : le thread dort jusqu'au prochain message (0 % CPU).
            MsgWaitForMultipleObjectsEx(0, nullptr, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            continue;
        }
        if (wallpapersActive && limiter_.IsFrameDue() && QpcNow() >= idleUntilQpc_) {
            RenderFrame();
            ++framesRendered_;
            limiter_.OnFrameRendered();
        }
        if (uiActive && panel_ && uiLimiter_.IsFrameDue()) {
            RenderControlPanel();
            uiLimiter_.OnFrameRendered();
        }

        int64_t deadline = std::numeric_limits<int64_t>::max();
        if (wallpapersActive) {
            deadline = std::max(limiter_.NextDeadline(), idleUntilQpc_);
        }
        if (uiActive) {
            deadline = std::min(deadline, uiLimiter_.NextDeadline());
        }
        limiter_.WaitUntil(deadline);
    }
}

void Application::Shutdown() {
    if (shutDown_) {
        return;
    }
    shutDown_ = true;
    running_ = false;
    LogSessionStatistics();
    audioCapture_.SetEnabled(false);
    media_.SetEnabled(false);
    stats_.Stop();

    if (controlWindow_) {
        KillTimer(controlWindow_, kTimerMaintenance);
        KillTimer(controlWindow_, kTimerRebuild);
        KillTimer(controlWindow_, kTimerWatchdog);
        hotkeys_.UnregisterAll();
        if (sessionNotificationsRegistered_) {
            WTSUnRegisterSessionNotification(controlWindow_);
            sessionNotificationsRegistered_ = false;
        }
    }
    if (displayNotification_) {
        UnregisterPowerSettingNotification(displayNotification_);
        displayNotification_ = nullptr;
    }

    panel_.reset();
    const bool hadSurfaces = !surfaces_.empty();
    DestroySurfaces();
    if (hadSurfaces) {
        desktop_.RestoreDesktop();
    }
    tray_.Remove();
    config_.FlushIfDirty(true);
    device_.Release();
    if (mediaFoundationStarted_) {
        MFShutdown();
        mediaFoundationStarted_ = false;
    }

    if (controlWindow_) {
        HWND window = controlWindow_;
        controlWindow_ = nullptr;
        DestroyWindow(window);
    }
    if (startTickMs_ != 0) {
        log::Info(L"Arret termine");
    }
    log::Shutdown();
}

// =============================================================================================== //
//  Surfaces
// =============================================================================================== //

void Application::ScheduleRebuild() {
    if (controlWindow_) {
        SetTimer(controlWindow_, kTimerRebuild, kRebuildDebounceMs, nullptr);
    }
}

void Application::DestroySurfaces() {
    surfaces_.clear();
    if (device_.IsValid()) {
        device_.Context()->ClearState();
        device_.Context()->Flush();
    }
}

WallpaperRef Application::AssignedWallpaper(const MonitorInfo& monitor) const {
    const AppConfig& config = config_.Data();
    if (monitor.id == kSpanMonitorId) {
        return config.spanWallpaper.Empty() ? config.defaultWallpaper : config.spanWallpaper;
    }
    if (const auto it = config.monitorWallpapers.find(monitor.id); it != config.monitorWallpapers.end()) {
        return it->second;
    }
    return config.defaultWallpaper;
}

void Application::AssignWallpaper(const MonitorInfo& monitor, const WallpaperRef& ref) {
    AppConfig& config = config_.Data();
    if (monitor.id == kSpanMonitorId) {
        config.spanWallpaper = ref;
    } else {
        config.monitorWallpapers[monitor.id] = ref;
    }
    config_.MarkDirty();
}

std::wstring Application::MonitorLabel(const MonitorInfo& monitor) const {
    if (monitor.id == kSpanMonitorId) {
        return L"Tous les écrans";
    }
    for (size_t i = 0; i < monitors_.size(); ++i) {
        if (monitors_[i].id == monitor.id) {
            return std::format(L"Écran {} · {}{}", i + 1, monitor.friendlyName,
                               monitor.primary ? L" (principal)" : L"");
        }
    }
    return monitor.friendlyName;
}

void Application::RebuildSurfaces() {
    if (controlWindow_) {
        KillTimer(controlWindow_, kTimerRebuild);
    }
    ticksWithoutSurface_ = 0;
    failureRetries_.clear();
    DestroySurfaces();

    if (!device_.IsValid()) {
        std::wstring error;
        if (!device_.Create(error)) {
            log::Error(L"Direct3D 11 : {}", error);
            return;
        }
    }
    if (!desktop_.Attach()) {
        return;
    }

    monitors_ = EnumerateMonitors();
    if (monitors_.empty()) {
        log::Warn(L"Aucun ecran actif detecte");
        return;
    }
    for (const auto& m : monitors_) {
        log::Info(L"Ecran « {} » {}x{} @ ({}, {}), {} dpi{} - id {}", m.friendlyName, m.Width(), m.Height(),
                  m.bounds.left, m.bounds.top, m.dpi, m.primary ? L", principal" : L"", Utf8ToWide(m.id));
    }

    const AppConfig& config = config_.Data();
    std::vector<MonitorInfo> targets;
    if (config.layout == LayoutMode::Span) {
        targets.push_back(MakeSpanMonitor(monitors_));
    } else {
        targets = monitors_;
    }

    watchdog_.Update(EffectivePauseRules(), monitors_, panel_ ? panel_->Window() : nullptr);
    input_.Update();
    const FrameTiming timing = NextTiming();
    for (auto& target : targets) {
        auto surface = std::make_unique<MonitorContext>(device_, desktop_, instance_, std::move(target));
        std::wstring error;
        surface->SetParallaxEnabled(!effective_.reducedMotion);
        if (!surface->Create(effective_.renderScale, error)) {
            log::Error(L"Surface « {} » : {}", surface->Info().friendlyName, error);
            continue;
        }

        WallpaperDesc desc;
        if (!options_.wallpaperOverride.empty()) {
            desc = config_.Resolve(WallpaperRef{
                GuessWallpaperType(options_.wallpaperOverride).value_or(WallpaperType::Shader), options_.wallpaperOverride});
        } else {
            desc = config_.Resolve(AssignedWallpaper(surface->Info()));
        }
        InstallWallpaper(*surface, desc);

        // Premiere image avant affichage : pas de flash noir a l'apparition.
        surface->SetPaused(IsPaused(), watchdog_.IsAreaCovered(surface->Info().bounds));
        surface->UpdatePointer(input_.State(), 0.0f);
        surface->RenderFrame(timing, audio_);
        surface->Show();
        surfaces_.push_back(std::move(surface));
    }
    limiter_.Reset();
    log::Info(L"{} surface(s) active(s)", surfaces_.size());
}

TransitionRequest Application::MakeTransition(const TransitionSettings& settings) {
    TransitionRequest request;
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    request.type = ResolveTransitionType(settings.type, unit(random_));
    request.seconds = effective_.reducedMotion ? 0.0f : settings.seconds;  // accessibilite : coupe nette
    request.seed = unit(random_);
    return request;
}

const TransitionSettings& Application::PlaylistTransition() const {
    const PlaylistSettings& playlist = config_.Data().playlist;
    return playlist.customTransition ? playlist.transition : config_.Data().transition;
}

void Application::InstallWallpaper(MonitorContext& surface, const WallpaperDesc& desc,
                                   const TransitionRequest& transition) {
    idleFrames_ = 0;  // le nouveau fond doit dessiner tout de suite
    idleUntilQpc_ = 0;
    // Meme fond (reinstallation apres un reglage, nouvel essai) : jamais de transition.
    const bool sameSource = PathKey(surface.Desc().source) == PathKey(desc.source) && surface.Backend();
    std::wstring error;
    if (auto backend = CreateWallpaperBackend(desc.type, error)) {
        const BackendInitContext init{device_, surface.Window(),   surface.BufferWidth(), surface.BufferHeight(),
                                      desc,    effective_.volume, effective_.muted};
        if (backend->Initialize(init, error)) {
            log::Info(L"« {} » affiche {}", surface.Info().friendlyName,
                      desc.source.empty() ? std::wstring(L"le shader integre") : desc.source.wstring());
            surface.AttachBackend(std::move(backend), desc, sameSource ? TransitionRequest{} : transition);
            return;
        }
    }
    log::Warn(L"Fond {} indisponible sur « {} », shader de secours utilise :\n{}", desc.source.wstring(),
              surface.Info().friendlyName, error);

    WallpaperDesc fallbackDesc;
    auto fallback = std::make_unique<ShaderBackend>();
    const BackendInitContext init{device_, surface.Window(), surface.BufferWidth(), surface.BufferHeight(),
                                  fallbackDesc};
    std::wstring fallbackError;
    if (fallback->Initialize(init, fallbackError)) {
        surface.AttachBackend(std::move(fallback), std::move(fallbackDesc));
    } else {
        log::Error(L"Shader de secours : {}", fallbackError);
    }
}

// =============================================================================================== //
//  Rendu
// =============================================================================================== //

FrameTiming Application::NextTiming() {
    const int64_t now = QpcNow();
    FrameTiming timing;
    timing.seconds = static_cast<double>(now - qpcStart_) / static_cast<double>(qpcFrequency_);
    timing.deltaSeconds =
        std::clamp(static_cast<float>(static_cast<double>(now - qpcLast_) / static_cast<double>(qpcFrequency_)),
                   0.0f, 0.1f);
    timing.frameIndex = frameIndex_++;
    timing.targetFps = static_cast<float>(effective_.fpsLimit);
    qpcLast_ = now;
    return timing;
}

void Application::RenderFrame() {
    CurrentAudio(audio_);
    input_.Update();
    const FrameTiming timing = NextTiming();
    bool anyPresented = false;
    for (auto& surface : surfaces_) {
        if (surface->IsPaused()) {
            continue;
        }
        surface->UpdatePointer(input_.State(), timing.deltaSeconds);
        const SurfaceStatus status = surface->RenderFrame(timing, audio_);
        if (status == SurfaceStatus::DeviceLost) {
            RecoverFromDeviceLoss();
            return;
        }
        anyPresented = anyPresented || status == SurfaceStatus::Presented;
    }
    UpdateIdleState(anyPresented);

    ++fpsWindowFrames_;
    const uint64_t now = GetTickCount64();
    if (now - fpsWindowStartMs_ >= 1000) {
        measuredFps_ = static_cast<float>(fpsWindowFrames_ * 1000.0 / static_cast<double>(now - fpsWindowStartMs_));
        fpsWindowFrames_ = 0;
        fpsWindowStartMs_ = now;
    }
}

void Application::UpdateIdleState(bool anyPresented) {
    const GlobalPointerState& pointer = input_.State();
    const bool pointerMoved = pointer.valid && (pointer.cursor.x != lastCursor_.x || pointer.cursor.y != lastCursor_.y ||
                                                pointer.primaryDown != lastPrimaryDown_);
    lastCursor_ = pointer.cursor;
    lastPrimaryDown_ = pointer.primaryDown;
    if (anyPresented || pointerMoved) {
        idleFrames_ = 0;
        idleUntilQpc_ = 0;
        return;
    }
    if (++idleFrames_ >= kIdleAfterFrames) {
        idleUntilQpc_ = QpcNow() + qpcFrequency_ / kIdleWakeupsPerSecond;
    }
}

void Application::RecoverFromDeviceLoss() {
    const HRESULT reason = device_.IsValid() ? device_.Device()->GetDeviceRemovedReason() : E_FAIL;
    log::Warn(L"Peripherique GPU perdu ({}) : recreation complete", HResultToString(reason));
    const bool hadPanel = panel_ != nullptr;
    panel_.reset();
    DestroySurfaces();
    device_.Release();
    RebuildSurfaces();
    if (hadPanel) {
        OpenControlPanel();
    }
}

// =============================================================================================== //
//  Pauses et surveillance
// =============================================================================================== //

bool Application::AnySurfaceRunning() const {
    return std::any_of(surfaces_.begin(), surfaces_.end(), [](const auto& surface) { return !surface->IsPaused(); });
}

bool Application::IsRendering() const {
    return !IsPaused() && AnySurfaceRunning();
}

void Application::ApplyPauseState() {
    const bool wasRendering = IsRendering();
    for (auto& surface : surfaces_) {
        const bool covered = watchdog_.IsAreaCovered(surface->Info().bounds);
        if (covered != surface->IsAutoPaused()) {
            log::Info(L"« {} » {}", surface->Info().friendlyName,
                      covered ? L"recouvert par une application plein ecran : pause" : L"de nouveau visible : reprise");
        }
        surface->SetPaused(IsPaused(), covered);
    }
    const bool rendering = IsRendering();
    if (wasRendering && !rendering && config_.Data().pause.releaseGpuResources) {
        device_.Trim();
    }
    if (!wasRendering && rendering) {
        qpcLast_ = QpcNow();
        limiter_.Reset();
    }
    UpdateAudioCapture();
}

void Application::SetPauseReason(uint32_t reason, bool active) {
    const uint32_t previous = pauseReasons_;
    if (active) {
        pauseReasons_ |= reason;
    } else {
        pauseReasons_ &= ~reason;
    }
    if (previous == pauseReasons_) {
        return;
    }
    ApplyPauseState();
    log::Info(L"Raisons de pause : 0x{:X}", pauseReasons_);
    UpdateTrayTooltip();
}

PauseRules Application::EffectivePauseRules() const {
    PauseRules rules = effective_.pause;
    if (options_.noAutoPause) {
        rules.onFullscreenApp = false;
        rules.onMaximizedApp = false;
        rules.onBattery = false;
    }
    return rules;
}

void Application::WatchdogTick() {
    const PauseRules rules = EffectivePauseRules();
    watchdog_.Update(rules, monitors_, panel_ ? panel_->Window() : nullptr);
    ProfileTick();
    SetPauseReason(kPauseBattery, rules.onBattery && watchdog_.OnBattery());
    ApplyPauseState();
}

std::wstring Application::StatusText() const {
    if (pauseReasons_ & kPauseManual) {
        return L"En pause";
    }
    if (pauseReasons_ & kPauseDisplayOff) {
        return L"En pause · écran éteint";
    }
    if (pauseReasons_ & kPauseSessionLocked) {
        return L"En pause · session verrouillée";
    }
    if (pauseReasons_ & kPauseBattery) {
        return L"En pause · sur batterie";
    }
    if (surfaces_.empty()) {
        return L"Aucun écran actif";
    }
    const auto covered = std::count_if(surfaces_.begin(), surfaces_.end(),
                                       [](const auto& surface) { return surface->IsAutoPaused(); });
    if (covered == static_cast<std::ptrdiff_t>(surfaces_.size())) {
        return L"En pause · application plein écran";
    }
    if (covered > 0) {
        return std::format(L"Actif · {} écran(s) en pause", covered);
    }
    return L"Actif";
}

void Application::MaintenanceTick() {
    config_.FlushIfDirty();

    if (warmCpuMs_ < 0.0 && GetTickCount64() - startTickMs_ >= 5000) {
        warmCpuMs_ = ProcessCpuMs();
        warmTickMs_ = GetTickCount64();
        warmFrames_ = framesRendered_;
    }
    if (options_.exitAfterSeconds > 0.0 &&
        static_cast<double>(GetTickCount64() - startTickMs_) / 1000.0 >= options_.exitAfterSeconds) {
        log::Info(L"--exit-after atteint : arret");
        running_ = false;
        return;
    }
    if (!IsRendering() && GetTickCount64() - fpsWindowStartMs_ > 2000) {
        measuredFps_ = 0.0f;
    }

    // L'Explorateur a pu etre redemarre (ou la couche WorkerW recreee) : nos fenetres sont alors detruites.
    bool lost = !surfaces_.empty() && !desktop_.IsAttached();
    for (const auto& surface : surfaces_) {
        lost = lost || !IsWindow(surface->Window());
    }
    if (lost) {
        log::Warn(L"Couche du bureau perdue : reconstruction des surfaces");
        ScheduleRebuild();
        return;
    }
    if (surfaces_.empty() && ++ticksWithoutSurface_ >= kRetryWithoutSurfaceTicks) {
        RebuildSurfaces();
        return;
    }
    for (auto& surface : surfaces_) {
        if (!surface->IsPaused()) {
            surface->MaintenanceTick();
        }
    }
    ReplaceFailedBackends();
    if (!options_.capturePath.empty() && !options_.captureDone && !surfaces_.empty() &&
        static_cast<double>(GetTickCount64() - startTickMs_) / 1000.0 >= options_.captureDelaySeconds) {
        options_.captureDone = true;
        surfaces_.front()->RequestCapture(options_.capturePath, options_.captureWidth);
    }
    SystemFeedsTick();
    ScheduleTick(false);
    PlaylistTick();
    if (options_.nextEverySeconds > 0.0 &&
        static_cast<double>(GetTickCount64() - lastForcedNextMs_) >= options_.nextEverySeconds * 1000.0) {
        if (lastForcedNextMs_ != 0) {
            log::Info(L"--next-every : fond suivant");
            NextWallpaper();
        }
        lastForcedNextMs_ = GetTickCount64();
    }
    UpdateAudioCapture();
}

void Application::ReplaceFailedBackends() {
    for (auto& surface : surfaces_) {
        std::wstring error;
        if (!surface->Backend() || !surface->Backend()->HasFailed(error)) {
            continue;
        }
        int& retries = failureRetries_[surface->Info().id];
        if (retries == 0 && !surface->Desc().source.empty()) {
            // Echec transitoire possible (demarrage du runtime, fichier en cours d'ecriture) : un nouvel essai.
            ++retries;
            log::Warn(L"« {} » : {} – nouvel essai", surface->Info().friendlyName, error);
            const WallpaperDesc desc = surface->Desc();
            InstallWallpaper(*surface, desc);
        } else {
            log::Error(L"« {} » : {} – remplacement par le shader de secours", surface->Info().friendlyName,
                       error);
            tray_.ShowNotification(kProductName, L"Ce fond n'a pas pu être lu : " + error);
            InstallWallpaper(*surface, WallpaperDesc{});
        }
        surface->SetPaused(IsPaused(), surface->IsAutoPaused());
    }
}

void Application::UpdateAudioCapture() {
    // Capture seulement si la reactivite est activee ET que quelque chose est dessine (fonds ou interface) :
    // en jeu plein ecran ou en pause, aucun thread audio ne tourne.
    const bool uiVisible = panel_ && panel_->IsVisible();
    const bool wanted =
        config_.Data().audio.reactive && !shutDown_ && (IsRendering() || uiVisible || options_.audioDebug);
    if (wanted != audioCapture_.IsEnabled()) {
        audioCapture_.SetEnabled(wanted);
        log::Info(L"Audio : capture {}", wanted ? L"demarree" : L"arretee");
    }
}

void Application::CurrentAudio(AudioSpectrum& out) const {
    audioCapture_.Snapshot(out);
    const float sensitivity = config_.Data().audio.sensitivity;
    for (auto& band : out.bands) {
        band = std::min(1.0f, band * sensitivity);
    }
    out.level = std::min(1.0f, out.level * sensitivity);
    out.bass = std::min(1.0f, out.bass * sensitivity);
    out.mid = std::min(1.0f, out.mid * sensitivity);
    out.treble = std::min(1.0f, out.treble * sensitivity);
}

void Application::SetAudioReactive(bool reactive) {
    config_.Data().audio.reactive = reactive;
    config_.MarkDirty();
    UpdateAudioCapture();
}

void Application::SetAudioSensitivity(float sensitivity) {
    config_.Data().audio.sensitivity = std::clamp(sensitivity, 0.0f, 8.0f);
    config_.MarkDirty();
}

void Application::SetAudioAutoGain(bool enabled) {
    config_.Data().audio.autoGain = enabled;
    config_.MarkDirty();
    audioCapture_.SetAutoGain(enabled);
}

void Application::SetAudioBeatSensitivity(float sensitivity) {
    config_.Data().audio.beatSensitivity = std::clamp(sensitivity, 0.25f, 2.0f);
    config_.MarkDirty();
    audioCapture_.SetBeatSensitivity(config_.Data().audio.beatSensitivity);
}

void Application::SetAudioSmoothing(float attackMs, float releaseMs) {
    AudioSettings& audio = config_.Data().audio;
    audio.attackMs = std::clamp(attackMs, 0.0f, 500.0f);
    audio.releaseMs = std::clamp(releaseMs, 10.0f, 3000.0f);
    config_.MarkDirty();
    audioCapture_.SetSmoothing(audio.attackMs, audio.releaseMs);
}

void Application::ApplyAudioOutput() {
    for (auto& surface : surfaces_) {
        if (surface->Backend()) {
            surface->Backend()->SetAudioOutput(effective_.volume, effective_.muted);
        }
    }
}

void Application::SetVolume(float volume) {
    config_.Data().audio.volume = std::clamp(volume, 0.0f, 1.0f);
    config_.MarkDirty();
    RefreshEffectiveSettings(effective_.profile);
}

void Application::SetMuted(bool muted) {
    config_.Data().audio.muted = muted;
    config_.MarkDirty();
    RefreshEffectiveSettings(effective_.profile);
}

IWallpaperBackend* Application::LiveBackendFor(const LibraryItem& item) {
    const std::wstring key = PathKey(item.source);
    for (auto& surface : surfaces_) {
        if (surface->Backend() && PathKey(surface->Desc().source) == key) {
            return surface->Backend();
        }
    }
    return nullptr;
}

// =============================================================================================== //
//  Playlist / rotation
// =============================================================================================== //

std::vector<const LibraryItem*> Application::RotationCandidates() const {
    const PlaylistSettings& playlist = config_.Data().playlist;
    std::vector<std::string> keys;
    if (playlist.enabled) {
        for (const auto& path : playlist.items) {
            keys.push_back(WallpaperSettingsKey(path));
        }
    }
    std::vector<const LibraryItem*> candidates;
    for (const auto& item : library_.Items()) {
        if (item.supported && (keys.empty() || std::find(keys.begin(), keys.end(), item.id) != keys.end())) {
            candidates.push_back(&item);
        }
    }
    return candidates;
}

void Application::RememberCurrent(const MonitorContext& surface) {
    if (surface.Desc().source.empty()) {
        return;
    }
    auto& history = history_[surface.Info().id];
    const WallpaperRef current{surface.Desc().type, surface.Desc().source};
    if (history.empty() || !(history.back() == current)) {
        history.push_back(current);
    }
    constexpr size_t kMaxHistory = 20;
    if (history.size() > kMaxHistory) {
        history.erase(history.begin());
    }
}

void Application::AdvanceWallpapers(int direction) {
    const std::vector<const LibraryItem*> candidates = RotationCandidates();
    if (candidates.empty()) {
        log::Warn(L"Aucun fond disponible pour la rotation");
        return;
    }
    options_.wallpaperOverride.clear();
    failureRetries_.clear();
    const bool shuffle = config_.Data().playlist.enabled && config_.Data().playlist.shuffle;
    const TransitionRequest transition = MakeTransition(PlaylistTransition());
    for (auto& surface : surfaces_) {
        const std::wstring current = PathKey(surface->Desc().source);
        size_t currentIndex = candidates.size();
        for (size_t i = 0; i < candidates.size(); ++i) {
            if (PathKey(candidates[i]->source) == current) {
                currentIndex = i;
            }
        }
        size_t next = 0;
        if (direction < 0) {
            // Fond precedent : historique de l'ecran, sinon ordre de rotation a rebours.
            auto& history = history_[surface->Info().id];
            if (!history.empty()) {
                const WallpaperRef previous = history.back();
                history.pop_back();
                AssignWallpaper(surface->Info(), previous);
                InstallWallpaper(*surface, config_.Resolve(previous), transition);
                continue;
            }
            next = currentIndex < candidates.size() ? (currentIndex + candidates.size() - 1) % candidates.size()
                                                    : candidates.size() - 1;
        } else if (shuffle && candidates.size() > 1) {
            std::uniform_int_distribution<size_t> pick(0, candidates.size() - 1);
            do {
                next = pick(random_);
            } while (next == currentIndex);
        } else if (currentIndex < candidates.size()) {
            next = (currentIndex + 1) % candidates.size();
        }
        const WallpaperRef ref = candidates[next]->Ref();
        if (direction > 0) {
            RememberCurrent(*surface);
        }
        AssignWallpaper(surface->Info(), ref);
        InstallWallpaper(*surface, config_.Resolve(ref), transition);
    }
    ApplyPauseState();
}

void Application::PlaylistTick() {
    const PlaylistSettings& playlist = config_.Data().playlist;
    const uint64_t now = GetTickCount64();
    if (!playlist.enabled || IsPaused() || ScheduleOwnsWallpaper()) {
        return;  // la planification a la priorite sur la rotation
    }
    if (now - playlistAnchorMs_ >= static_cast<uint64_t>(playlist.intervalMinutes) * 60'000) {
        log::Info(L"Playlist : fond suivant");
        AdvanceWallpapers();
        playlistAnchorMs_ = now;
    }
}

void Application::RestartPlaylistTimer() {
    playlistAnchorMs_ = GetTickCount64();
}

double Application::MinutesUntilPlaylistChange() const {
    const double elapsed = static_cast<double>(GetTickCount64() - playlistAnchorMs_) / 60'000.0;
    return static_cast<double>(config_.Data().playlist.intervalMinutes) - elapsed;
}

// =============================================================================================== //
//  IAppServices
// =============================================================================================== //

void Application::RescanLibrary() {
    library_.Scan(config_.Data().libraryFolders);
}

bool Application::ImportIntoLibrary(const std::filesystem::path& file, std::wstring& error) {
    std::filesystem::path imported;
    if (!Library::Import(file, imported, error)) {
        log::Warn(L"Import de {} impossible : {}", file.wstring(), error);
        return false;
    }
    RescanLibrary();
    return true;
}

void Application::SetLibraryFolders(std::vector<std::filesystem::path> folders) {
    config_.Data().libraryFolders = std::move(folders);
    config_.MarkDirty();
    RescanLibrary();
}

std::vector<MonitorEntry> Application::Monitors() const {
    std::vector<MonitorEntry> entries;
    for (size_t i = 0; i < monitors_.size(); ++i) {
        entries.push_back(MonitorEntry{monitors_[i], static_cast<int>(i + 1), MonitorLabel(monitors_[i])});
    }
    return entries;
}

std::vector<SurfaceStatusInfo> Application::Surfaces() const {
    std::vector<SurfaceStatusInfo> result;
    for (const auto& surface : surfaces_) {
        SurfaceStatusInfo info;
        info.monitorId = surface->Info().id;
        info.label = MonitorLabel(surface->Info());
        info.bounds = surface->Info().bounds;
        info.source = surface->Desc().source;
        info.paused = surface->IsPaused();
        info.autoPaused = surface->IsAutoPaused();
        result.push_back(std::move(info));
    }
    return result;
}

void Application::ApplyWallpaper(const LibraryItem& item, const std::string& monitorId) {
    if (!item.supported) {
        return;
    }
    ApplyWallpaperRef(item.Ref(), monitorId, MakeTransition(config_.Data().transition));
    RestartPlaylistTimer();
}

void Application::ApplyWallpaperRef(const WallpaperRef& ref, const std::string& monitorId,
                                    const TransitionRequest& transition) {
    options_.wallpaperOverride.clear();
    failureRetries_.clear();
    AppConfig& config = config_.Data();
    const bool allScreens = monitorId.empty() || config.layout == LayoutMode::Span;
    if (allScreens) {
        if (config.layout == LayoutMode::Span) {
            config.spanWallpaper = ref;
        } else {
            config.defaultWallpaper = ref;
            for (const auto& monitor : monitors_) {
                config.monitorWallpapers[monitor.id] = ref;
            }
        }
    } else {
        config.monitorWallpapers[monitorId] = ref;
    }
    config_.MarkDirty();

    const WallpaperDesc desc = config_.Resolve(ref);
    for (auto& surface : surfaces_) {
        if (allScreens || surface->Info().id == monitorId) {
            if (PathKey(surface->Desc().source) != PathKey(desc.source)) {
                RememberCurrent(*surface);
            }
            InstallWallpaper(*surface, desc, transition);
        }
    }
    ApplyPauseState();
}

void Application::SetWallpaperParameter(const LibraryItem& item, const std::string& name, const ParamValue& value) {
    config_.SetWallpaperParam(item.Ref(), name, value);
    const std::wstring key = PathKey(item.source);
    for (auto& surface : surfaces_) {
        if (surface->Backend() && PathKey(surface->Desc().source) == key) {
            surface->Backend()->SetParameter(name, value);
        }
    }
}

void Application::ResetWallpaperParameters(const LibraryItem& item) {
    config_.ResetWallpaperParams(item.Ref());
    const std::wstring key = PathKey(item.source);
    const WallpaperDesc desc = config_.Resolve(item.Ref());
    for (auto& surface : surfaces_) {
        if (PathKey(surface->Desc().source) == key) {
            InstallWallpaper(*surface, desc);
        }
    }
    ApplyPauseState();
}

void Application::NextWallpaper() {
    AdvanceWallpapers(1);
    RestartPlaylistTimer();
}

void Application::PreviousWallpaper() {
    AdvanceWallpapers(-1);
    RestartPlaylistTimer();
}

// =============================================================================================== //
//  Profils, planification, raccourcis, accessibilite
// =============================================================================================== //

void Application::RefreshEffectiveSettings(const std::string& profile) {
    const AppConfig& config = config_.Data();
    const bool reduced = config.reducedMotion == ReducedMotion::On ||
                         (config.reducedMotion == ReducedMotion::Auto && !systemAnimations_);
    const EffectiveSettings previous = effective_;
    effective_ = ComputeEffectiveSettings(config, FindProfile(config.profiles, profile), reduced);

    if (previous.profile != effective_.profile) {
        log::Info(L"Profil : {}", effective_.profile.empty() ? std::wstring(L"aucun (reglages de base)")
                                                              : Utf8ToWide(effective_.profile));
    }
    if (previous.reducedMotion != effective_.reducedMotion) {
        log::Info(L"Animations reduites : {}", effective_.reducedMotion ? L"oui" : L"non");
        for (auto& surface : surfaces_) {
            surface->SetParallaxEnabled(!effective_.reducedMotion);
        }
    }
    if (previous.fpsLimit != effective_.fpsLimit) {
        limiter_.SetTargetFps(effective_.fpsLimit);
    }
    if (previous.volume != effective_.volume || previous.muted != effective_.muted) {
        ApplyAudioOutput();
    }
    if (previous.renderScale != effective_.renderScale && running_) {
        RebuildSurfaces();
    }
    if (running_) {
        UpdateTrayTooltip();
    }
}

std::string Application::DesiredProfile() const {
    const ProfileSettings& settings = config_.Data().profiles;
    const bool needForeground = settings.autoSwitch && !settings.rules.empty();
    return ResolveProfileName(settings, needForeground ? systeminfo::ForegroundExecutable() : std::wstring(),
                              watchdog_.OnBattery());
}

void Application::ProfileTick() {
    // Bascule seulement si le choix reste le meme ~1 s (pas de va-et-vient a chaque Alt+Tab).
    const std::string desired = DesiredProfile();
    if (desired == effective_.profile) {
        pendingProfileTicks_ = 0;
        return;
    }
    if (desired != pendingProfile_) {
        pendingProfile_ = desired;
        pendingProfileTicks_ = 1;
        return;
    }
    if (++pendingProfileTicks_ >= 2) {
        pendingProfileTicks_ = 0;
        RefreshEffectiveSettings(desired);
    }
}

void Application::RefreshAutomation() {
    config_.MarkDirty();
    RefreshEffectiveSettings(DesiredProfile());
    ScheduleTick(true);
}

bool Application::ScheduleOwnsWallpaper() const {
    return config_.Data().schedule.enabled && !scheduleTarget_.Empty();
}

void Application::ScheduleTick(bool force) {
    const ScheduleSettings& schedule = config_.Data().schedule;
    if (!schedule.enabled) {
        scheduleTarget_ = {};
        scheduleStatus_ = L"Désactivée";
        return;
    }
    // Heure locale et decalage UTC du jour (heure d'ete comprise).
    FILETIME utc{};
    GetSystemTimeAsFileTime(&utc);
    FILETIME local{};
    FileTimeToLocalFileTime(&utc, &local);
    SYSTEMTIME now{};
    FileTimeToSystemTime(&local, &now);
    const auto ticks = [](const FILETIME& ft) {
        return static_cast<int64_t>((static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime);
    };
    ScheduleContext context;
    context.year = now.wYear;
    context.month = now.wMonth;
    context.day = now.wDay;
    context.minutesOfDay = now.wHour * 60.0 + now.wMinute + now.wSecond / 60.0;
    context.utcOffsetHours = static_cast<double>(ticks(local) - ticks(utc)) / 36'000'000'000.0;
    context.lightTheme = lightTheme_;
    const ScheduleDecision decision = EvaluateSchedule(schedule, context);

    std::wstring detail;
    if (schedule.mode == ScheduleMode::Sun) {
        const SunTimes sun = ComputeSunTimes(context.year, context.month, context.day, schedule.latitude,
                                             schedule.longitude, context.utcOffsetHours);
        detail = sun.kind == SunTimes::Kind::Normal
                     ? std::format(L" (lever {}, coucher {})", Utf8ToWide(FormatMinutes(sun.sunriseMinutes)),
                                   Utf8ToWide(FormatMinutes(sun.sunsetMinutes)))
                     : std::wstring(sun.kind == SunTimes::Kind::PolarDay ? L" (soleil de minuit)" : L" (nuit polaire)");
    }
    if (decision.wallpaper.Empty()) {
        scheduleStatus_ = L"Aucun fond prévu pour ce moment" + detail;
        scheduleTarget_ = {};
        return;
    }
    scheduleStatus_ = L"Active : " + Utf8ToWide(decision.reason) + detail;
    if (force || !(decision.wallpaper == scheduleTarget_)) {
        scheduleTarget_ = decision.wallpaper;
        log::Info(L"Planification ({}) : {}", Utf8ToWide(decision.reason), decision.wallpaper.source.wstring());
        ApplyWallpaperRef(decision.wallpaper, std::string(), MakeTransition(config_.Data().transition));
    }
}

void Application::RefreshTheme() {
    lightTheme_ = systeminfo::AppsUseLightTheme();
    ThemeInfo theme;
    theme.dark = !lightTheme_;
    theme.accent = systeminfo::AccentColor();
    if (themeSerial_ == 0 || theme.dark != theme_.dark || theme.accent != theme_.accent) {
        theme_ = theme;
        ++themeSerial_;
    }
}

void Application::SystemFeedsTick() {
    uint32_t topics = 0;
    for (const auto& surface : surfaces_) {
        if (surface->Backend() && !surface->IsPaused()) {
            topics |= surface->Backend()->SystemTopics();
        }
    }
    const IntegrationSettings& integration = config_.Data().integration;
    media_.SetEnabled(integration.media && (topics & kTopicMedia) != 0 && !shutDown_);
    if (media_.IsEnabled()) {
        mediaSerial_ = media_.Snapshot(mediaInfo_);
    }
    if (integration.systemStats && (topics & kTopicSystem) != 0) {
        if (!stats_.IsRunning()) {
            Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
            Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
            if (device_.IsValid() && SUCCEEDED(device_.Device()->QueryInterface(IID_PPV_ARGS(&dxgiDevice)))) {
                dxgiDevice->GetAdapter(&adapter);
            }
            stats_.Start(adapter.Get());
        }
        stats_.Sample(statsInfo_);
        ++statsSerial_;
    } else {
        stats_.Stop();
    }
    SystemSnapshot snapshot;
    snapshot.media = &mediaInfo_;
    snapshot.mediaSerial = mediaSerial_;
    snapshot.stats = &statsInfo_;
    snapshot.statsSerial = statsSerial_;
    snapshot.theme = theme_;
    snapshot.themeSerial = themeSerial_;
    for (auto& surface : surfaces_) {
        if (surface->Backend() && !surface->IsPaused()) {
            surface->Backend()->PushSystemData(snapshot);
        }
    }
}

void Application::ApplyHotkeys() {
    config_.MarkDirty();
    hotkeys_.Apply(controlWindow_, config_.Data().hotkeys);
}

void Application::SuspendHotkeys(bool suspended) {
    if (suspended) {
        hotkeys_.UnregisterAll();
    } else {
        hotkeys_.Apply(controlWindow_, config_.Data().hotkeys);
    }
}

void Application::ToggleControlPanel() {
    if (panel_ && panel_->IsVisible() && GetForegroundWindow() == panel_->Window()) {
        CloseControlPanel();
    } else {
        OpenControlPanel();
        if (panel_) {
            SetForegroundWindow(panel_->Window());
        }
    }
}

void Application::OnHotkey(HotkeyAction action) {
    switch (action) {
    case HotkeyAction::Next: NextWallpaper(); break;
    case HotkeyAction::Previous: PreviousWallpaper(); break;
    case HotkeyAction::Pause: SetManualPause(!IsManuallyPaused()); break;
    case HotkeyAction::Mute: SetMuted(!config_.Data().audio.muted); break;
    case HotkeyAction::TogglePanel: ToggleControlPanel(); break;
    }
}

void Application::SetFpsLimit(uint32_t fps) {
    fps = std::clamp<uint32_t>(fps, 1, 360);
    if (config_.Data().fpsLimit == fps) {
        return;
    }
    config_.Data().fpsLimit = fps;
    config_.MarkDirty();
    RefreshEffectiveSettings(effective_.profile);
}

void Application::SetRenderScale(float scale) {
    scale = std::clamp(scale, 0.25f, 1.0f);
    config_.Data().renderScale = scale;
    config_.MarkDirty();
    RefreshEffectiveSettings(effective_.profile);  // reconstruit les surfaces si la qualite effective change
}

void Application::SetLayout(LayoutMode layout) {
    if (config_.Data().layout == layout) {
        return;
    }
    config_.Data().layout = layout;
    config_.MarkDirty();
    log::Info(L"Disposition : {}", layout == LayoutMode::Span ? L"etendue sur tous les ecrans" : L"par ecran");
    RebuildSurfaces();
}

void Application::SetStartWithWindows(bool enabled) {
    config_.Data().startWithWindows = enabled;
    config_.MarkDirty();
    autostart::Sync(enabled);
}

void Application::OpenConfigFolder() {
    OpenInShell(config_.FilePath().parent_path());
}

void Application::OpenLogFile() {
    OpenInShell(AppDataDir() / L"log.txt");
}

// =============================================================================================== //
//  Interface et zone de notification
// =============================================================================================== //

void Application::OpenControlPanel() {
    if (!panel_) {
        auto panel = std::make_unique<ControlPanel>(instance_, *this);
        std::wstring error;
        if (!panel->Create(error)) {
            log::Error(L"Ouverture de l'interface impossible : {}", error);
            return;
        }
        panel_ = std::move(panel);
    }
    panel_->Show();
    uiLimiter_.Reset();
    uiQpcLast_ = QpcNow();
    UpdateAudioCapture();
}

void Application::CloseControlPanel() {
    panel_.reset();
    UpdateAudioCapture();
    if (!IsRendering() && config_.Data().pause.releaseGpuResources) {
        device_.Trim();
    }
    log::Info(L"Interface fermee (l'application reste dans la zone de notification)");
    if (!trayHintShown_) {
        trayHintShown_ = true;
        tray_.ShowNotification(kProductName, L"WallEnginePaper continue de tourner ici. Cliquez sur l'icône "
                                             L"pour rouvrir la bibliothèque.");
    }
}

void Application::RenderControlPanel() {
    const int64_t now = QpcNow();
    const float delta = std::clamp(static_cast<float>(static_cast<double>(now - uiQpcLast_) / qpcFrequency_), 0.0f, 0.1f);
    uiQpcLast_ = now;
    if (!panel_->RenderFrame(delta)) {
        RecoverFromDeviceLoss();
    }
}

void Application::UpdateTrayTooltip() {
    const std::wstring profile = effective_.profile.empty() ? std::wstring() : L" · profil " + Utf8ToWide(effective_.profile);
    tray_.SetTooltip(std::format(L"{} – {}{}", kProductName, StatusText(), profile));
}

double Application::ProcessCpuMs() const {
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user)) {
        return 0.0;
    }
    const auto toMs = [](const FILETIME& ft) {
        return static_cast<double>((static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime) / 10'000.0;
    };
    return toMs(kernel) + toMs(user);
}

void Application::LogSessionStatistics() const {
    if (startTickMs_ == 0) {
        return;
    }
    const auto toMs = [](const FILETIME& ft) {
        return static_cast<double>((static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime) / 10'000.0;
    };
    FILETIME creation{}, exit{}, kernel{}, user{};
    double threadMs = 0.0;
    double processMs = 0.0;
    if (GetThreadTimes(GetCurrentThread(), &creation, &exit, &kernel, &user)) {
        threadMs = toMs(kernel) + toMs(user);
    }
    if (GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user)) {
        processMs = toMs(kernel) + toMs(user);
    }
    const double seconds = static_cast<double>(GetTickCount64() - startTickMs_) / 1000.0;
    log::Info(L"Session : {:.1f} s, {} images ({:.1f} FPS), {} reveils de boucle, CPU thread principal {:.0f} ms, "
              L"CPU processus {:.0f} ms",
              seconds, framesRendered_, seconds > 0.0 ? static_cast<double>(framesRendered_) / seconds : 0.0,
              loopWakeups_, threadMs, processMs);
    if (warmCpuMs_ >= 0.0) {
        const double warmSeconds = static_cast<double>(GetTickCount64() - warmTickMs_) / 1000.0;
        if (warmSeconds >= 1.0) {
            const double cpuPerSecond = (processMs - warmCpuMs_) / warmSeconds;
            log::Info(L"Regime etabli : {:.1f} s, {:.1f} FPS, CPU processus {:.1f} ms/s ({:.2f} % d'un coeur)",
                      warmSeconds, static_cast<double>(framesRendered_ - warmFrames_) / warmSeconds, cpuPerSecond,
                      cpuPerSecond / 10.0);
        }
    }
}

void Application::ShowTrayMenu() {
    const AppConfig& config = config_.Data();
    const bool manualPause = IsManuallyPaused();

    HMENU menu = CreatePopupMenu();
    HMENU fpsMenu = CreatePopupMenu();
    for (const uint32_t fps : kFpsPresets) {
        AppendMenuW(fpsMenu, MF_STRING | (config.fpsLimit == fps ? MF_CHECKED : 0u), kCmdFpsBase + fps,
                    std::format(L"{} FPS", fps).c_str());
    }
    AppendMenuW(menu, MF_STRING, kCmdOpen, L"Ouvrir WallEnginePaper");
    SetMenuDefaultItem(menu, kCmdOpen, FALSE);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCmdPause, manualPause ? L"Reprendre" : L"Pause");
    AppendMenuW(menu, MF_STRING | (config.audio.muted ? MF_CHECKED : 0u), kCmdMute, L"Muet");
    AppendMenuW(menu, MF_STRING, kCmdNext, L"Fond suivant");
    AppendMenuW(menu, MF_STRING, kCmdPrevious, L"Fond précédent");
    AppendMenuW(menu, MF_STRING | (config.schedule.enabled ? MF_CHECKED : 0u), kCmdSchedule, L"Planification");
    HMENU profileMenu = CreatePopupMenu();
    AppendMenuW(profileMenu, MF_STRING | (config.profiles.active.empty() ? MF_CHECKED : 0u), kCmdNoProfile,
                L"Aucun (réglages de base)");
    for (size_t i = 0; i < config.profiles.profiles.size() && i < 100; ++i) {
        const Profile& profile = config.profiles.profiles[i];
        std::wstring label = Utf8ToWide(profile.name);
        if (effective_.profile == profile.name && config.profiles.active != profile.name) {
            label += L"  (automatique)";
        }
        AppendMenuW(profileMenu, MF_STRING | (config.profiles.active == profile.name ? MF_CHECKED : 0u),
                    kCmdProfileBase + i, label.c_str());
    }
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(profileMenu), L"Profil");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (config.layout == LayoutMode::Span ? MF_CHECKED : 0u), kCmdSpan,
                L"Étendre sur tous les écrans");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(fpsMenu), L"Limite d'images par seconde");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCmdQuit, L"Quitter");

    POINT cursor{};
    GetCursorPos(&cursor);
    // Indispensable pour que le menu se ferme quand on clique ailleurs.
    SetForegroundWindow(controlWindow_);
    const UINT alignment = GetSystemMetrics(SM_MENUDROPALIGNMENT) ? TPM_RIGHTALIGN : TPM_LEFTALIGN;
    const UINT command = static_cast<UINT>(TrackPopupMenuEx(
        menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | alignment, cursor.x, cursor.y,
        controlWindow_, nullptr));
    PostMessageW(controlWindow_, WM_NULL, 0, 0);
    DestroyMenu(menu);  // detruit aussi le sous-menu

    if (command != 0) {
        ExecuteCommand(command);
    }
}

void Application::ExecuteCommand(UINT command) {
    switch (command) {
    case kCmdOpen:
        OpenControlPanel();
        break;
    case kCmdPause:
        SetManualPause(!IsManuallyPaused());
        break;
    case kCmdMute:
        SetMuted(!config_.Data().audio.muted);
        break;
    case kCmdNext:
        NextWallpaper();
        break;
    case kCmdPrevious:
        PreviousWallpaper();
        break;
    case kCmdSchedule:
        config_.Data().schedule.enabled = !config_.Data().schedule.enabled;
        RefreshAutomation();
        break;
    case kCmdNoProfile:
        config_.Data().profiles.active.clear();
        RefreshAutomation();
        break;
    case kCmdSpan:
        SetLayout(config_.Data().layout == LayoutMode::Span ? LayoutMode::PerMonitor : LayoutMode::Span);
        break;
    case kCmdQuit:
        running_ = false;
        break;
    default:
        if (command > kCmdFpsBase && command <= kCmdFpsBase + 360) {
            SetFpsLimit(command - kCmdFpsBase);
        } else if (command >= kCmdProfileBase && command < kCmdProfileBase + 100) {
            const size_t index = command - kCmdProfileBase;
            if (index < config_.Data().profiles.profiles.size()) {
                config_.Data().profiles.active = config_.Data().profiles.profiles[index].name;
                RefreshAutomation();
            }
        }
        break;
    }
}

LRESULT CALLBACK Application::StaticWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    if (auto* app = reinterpret_cast<Application*>(GetWindowLongPtrW(window, GWLP_USERDATA))) {
        return app->WindowProc(window, message, wParam, lParam);
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT Application::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (taskbarCreatedMessage_ != 0 && message == taskbarCreatedMessage_) {
        log::Info(L"Explorateur redemarre : reinstallation de l'icone et des surfaces");
        tray_.Readd();
        ScheduleRebuild();
        return 0;
    }

    switch (message) {
    case TrayIcon::kCallbackMessage:
        switch (LOWORD(lParam)) {
        case NIN_SELECT:
        case NIN_KEYSELECT:
            OpenControlPanel();
            break;
        case WM_CONTEXTMENU:
            ShowTrayMenu();
            break;
        default:
            break;
        }
        return 0;

    case kMsgSecondInstance:
        if (wParam == 0) {
            OpenControlPanel();
        }
        return 0;

    case WM_TIMER:
        if (wParam == kTimerMaintenance) {
            MaintenanceTick();
        } else if (wParam == kTimerWatchdog) {
            WatchdogTick();
        } else if (wParam == kTimerRebuild) {
            RebuildSurfaces();
        }
        return 0;

    case WM_HOTKEY:
        if (const auto action = hotkeys_.ActionFor(wParam)) {
            OnHotkey(*action);
        }
        return 0;

    case WM_SETTINGCHANGE:
        if (wParam == SPI_SETCLIENTAREAANIMATION) {
            systemAnimations_ = systeminfo::ClientAreaAnimationsEnabled();
            RefreshEffectiveSettings(effective_.profile);
        } else if (systeminfo::IsThemeChangeMessage(lParam)) {
            const bool wasLight = lightTheme_;
            RefreshTheme();
            if (wasLight != lightTheme_) {
                log::Info(L"Theme Windows : {}", lightTheme_ ? L"clair" : L"sombre");
                ScheduleTick(false);
            }
        }
        return DefWindowProcW(window, message, wParam, lParam);

    case WM_DWMCOLORIZATIONCOLORCHANGED:
        RefreshTheme();
        return 0;

    case WM_DISPLAYCHANGE:
    case WM_DPICHANGED:
        // Ajout/retrait d'ecran, changement de resolution ou d'echelle : reconstruction differee
        // (Windows envoie souvent plusieurs messages d'affilee).
        ScheduleRebuild();
        return 0;

    case WM_POWERBROADCAST:
        if (wParam == PBT_POWERSETTINGCHANGE && lParam) {
            const auto* setting = reinterpret_cast<const POWERBROADCAST_SETTING*>(lParam);
            if (IsEqualGUID(setting->PowerSetting, kConsoleDisplayState) && setting->DataLength >= sizeof(DWORD)) {
                const DWORD state = *reinterpret_cast<const DWORD*>(setting->Data);  // 0 eteint, 1 allume, 2 attenue
                SetPauseReason(kPauseDisplayOff, state == 0 && config_.Data().pause.onDisplayOff);
            }
        } else if (wParam == PBT_APMPOWERSTATUSCHANGE) {
            WatchdogTick();  // branchement / debranchement du secteur
        } else if (wParam == PBT_APMRESUMEAUTOMATIC) {
            ScheduleRebuild();
        }
        return TRUE;

    case WM_WTSSESSION_CHANGE:
        if (wParam == WTS_SESSION_LOCK) {
            SetPauseReason(kPauseSessionLocked, config_.Data().pause.onSessionLocked);
        } else if (wParam == WTS_SESSION_UNLOCK) {
            SetPauseReason(kPauseSessionLocked, false);
        }
        return 0;

    case WM_QUERYENDSESSION:
        return TRUE;

    case WM_ENDSESSION:
        if (wParam) {
            Shutdown();
        }
        return 0;

    case WM_CLOSE:
        running_ = false;
        return 0;

    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

}  // namespace lwe
