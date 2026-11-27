#include "watchdog/PerformanceWatchdog.h"

#include <dwmapi.h>
#include <shellapi.h>

#include <algorithm>
#include <cwchar>
#include <iterator>

#include "desktop/DesktopHost.h"

namespace lwe {
namespace {

constexpr LONG kTolerance = 2;  // pixels : bordures arrondies, arrondis DPI

bool SameRect(const RECT& a, const RECT& b) noexcept {
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

bool Contains(const RECT& outer, const RECT& inner) noexcept {
    return outer.left <= inner.left + kTolerance && outer.top <= inner.top + kTolerance &&
           outer.right >= inner.right - kTolerance && outer.bottom >= inner.bottom - kTolerance;
}

bool Intersects(const RECT& a, const RECT& b) noexcept {
    RECT out{};
    return IntersectRect(&out, &a, &b) != FALSE;
}

// Elements du shell qui couvrent parfois tout l'ecran sans etre des applications.
bool IsShellChrome(HWND window) {
    wchar_t name[64] = {};
    GetClassNameW(window, name, static_cast<int>(std::size(name)));
    static constexpr const wchar_t* kIgnored[] = {
        L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd", L"Progman", L"WorkerW",
        L"XamlExplorerHostIslandWindow",  // Alt+Tab / vue des taches (Windows 11)
        L"Windows.UI.Core.CoreWindow",    // menu Demarrer, recherche, centre de notifications
        L"ForegroundStaging", L"MultitaskingViewFrame",
    };
    return std::any_of(std::begin(kIgnored), std::end(kIgnored),
                       [&](const wchar_t* ignored) { return std::wcscmp(name, ignored) == 0; });
}

bool IsCloaked(HWND window) {
    DWORD cloaked = 0;
    return SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked != 0;
}

RECT VisibleBounds(HWND window) {
    RECT rect{};
    if (FAILED(DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &rect, sizeof(rect)))) {
        GetWindowRect(window, &rect);
    }
    return rect;
}

}  // namespace

void PerformanceWatchdog::Update(const PauseRules& rules, const std::vector<MonitorInfo>& monitors,
                                 HWND ignoredWindow) {
    SYSTEM_POWER_STATUS power{};
    onBattery_ = GetSystemPowerStatus(&power) && power.ACLineStatus == 0 && power.BatteryFlag != 128;

    QUERY_USER_NOTIFICATION_STATE state{};
    presentationMode_ = SUCCEEDED(SHQueryUserNotificationState(&state)) && state == QUNS_PRESENTATION_MODE;

    monitors_.clear();
    covered_.clear();
    for (const auto& monitor : monitors) {
        monitors_.push_back(monitor.bounds);
        const bool covered = (rules.onFullscreenApp && presentationMode_) || IsMonitorCovered(monitor, rules, ignoredWindow);
        if (covered) {
            covered_.push_back(monitor.bounds);
        }
    }
}

bool PerformanceWatchdog::IsMonitorCovered(const MonitorInfo& monitor, const PauseRules& rules,
                                           HWND ignoredWindow) const {
    if (!rules.onFullscreenApp && !rules.onMaximizedApp) {
        return false;
    }
    // Candidats : la fenetre au centre de l'ecran (couvre les videos plein ecran en arriere-plan)
    // et la fenetre active si elle est sur cet ecran (jeux en plein ecran exclusif).
    const POINT center{(monitor.bounds.left + monitor.bounds.right) / 2, (monitor.bounds.top + monitor.bounds.bottom) / 2};
    HWND candidates[2] = {nullptr, nullptr};
    if (HWND hit = WindowFromPoint(center)) {
        candidates[0] = GetAncestor(hit, GA_ROOT);
    }
    if (HWND foreground = GetForegroundWindow();
        foreground && MonitorFromWindow(foreground, MONITOR_DEFAULTTONULL) == monitor.handle) {
        candidates[1] = foreground;
    }

    for (HWND window : candidates) {
        if (!window || window == ignoredWindow || !IsWindowVisible(window) || IsIconic(window) ||
            DesktopHost::IsShellDesktopWindow(window) || IsShellChrome(window) || IsCloaked(window)) {
            continue;
        }
        const RECT bounds = VisibleBounds(window);
        if (rules.onFullscreenApp && Contains(bounds, monitor.bounds)) {
            return true;
        }
        if (rules.onMaximizedApp && IsZoomed(window) && Contains(bounds, monitor.workArea)) {
            return true;
        }
    }
    return false;
}

bool PerformanceWatchdog::IsAreaCovered(const RECT& area) const {
    bool touchesAny = false;
    for (const auto& monitor : monitors_) {
        if (!Intersects(monitor, area)) {
            continue;
        }
        touchesAny = true;
        const bool covered =
            std::any_of(covered_.begin(), covered_.end(), [&](const RECT& r) { return SameRect(r, monitor); });
        if (!covered) {
            return false;
        }
    }
    return touchesAny;
}

}  // namespace lwe
