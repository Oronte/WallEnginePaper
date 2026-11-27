#include "desktop/MonitorEnumerator.h"

#include <shellscalingapi.h>

#include <algorithm>
#include <map>
#include <set>

#include "core/StringUtil.h"

namespace lwe {
namespace {

struct TargetNames {
    std::wstring devicePath;
    std::wstring friendlyName;
};

// QueryDisplayConfig donne, pour chaque source GDI (\\.\DISPLAYn), le chemin PnP stable
// du moniteur et son nom EDID. C'est ce chemin qui sert de cle par ecran dans la config.
std::map<std::wstring, TargetNames> QueryTargetNames() {
    std::map<std::wstring, TargetNames> result;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths;
    std::vector<DISPLAYCONFIG_MODE_INFO> modes;
    LONG status = ERROR_SUCCESS;
    do {
        UINT32 pathCount = 0;
        UINT32 modeCount = 0;
        status = GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount);
        if (status != ERROR_SUCCESS) {
            return result;
        }
        paths.resize(pathCount);
        modes.resize(modeCount);
        status = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(),
                                    nullptr);
        paths.resize(pathCount);
    } while (status == ERROR_INSUFFICIENT_BUFFER);
    if (status != ERROR_SUCCESS) {
        return result;
    }

    for (const auto& path : paths) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
        source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        source.header.size = sizeof(source);
        source.header.adapterId = path.sourceInfo.adapterId;
        source.header.id = path.sourceInfo.id;
        if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS) {
            continue;
        }
        DISPLAYCONFIG_TARGET_DEVICE_NAME target{};
        target.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
        target.header.size = sizeof(target);
        target.header.adapterId = path.targetInfo.adapterId;
        target.header.id = path.targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&target.header) != ERROR_SUCCESS) {
            continue;
        }
        // En mode clone, plusieurs cibles partagent une source : on garde la premiere.
        result.try_emplace(source.viewGdiDeviceName,
                           TargetNames{target.monitorDevicePath, target.monitorFriendlyDeviceName});
    }
    return result;
}

struct EnumState {
    std::vector<MonitorInfo>* monitors;
    const std::map<std::wstring, TargetNames>* names;
};

BOOL CALLBACK OnMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM param) {
    auto* state = reinterpret_cast<EnumState*>(param);
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(monitor, &mi)) {
        return TRUE;
    }
    MonitorInfo info;
    info.handle = monitor;
    info.gdiName = mi.szDevice;
    info.bounds = mi.rcMonitor;
    info.workArea = mi.rcWork;
    info.primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;

    UINT dpiX = 96;
    UINT dpiY = 96;
    if (SUCCEEDED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY))) {
        info.dpi = dpiX;
    }
    if (const auto it = state->names->find(info.gdiName); it != state->names->end()) {
        if (!it->second.devicePath.empty()) {
            info.id = WideToUtf8(it->second.devicePath);
        }
        info.friendlyName = it->second.friendlyName;
    }
    if (info.id.empty()) {
        info.id = WideToUtf8(info.gdiName);
    }
    if (info.friendlyName.empty()) {
        info.friendlyName = info.gdiName;
    }
    state->monitors->push_back(std::move(info));
    return TRUE;
}

}  // namespace

std::vector<MonitorInfo> EnumerateMonitors() {
    const auto names = QueryTargetNames();
    std::vector<MonitorInfo> monitors;
    EnumState state{&monitors, &names};
    EnumDisplayMonitors(nullptr, nullptr, OnMonitor, reinterpret_cast<LPARAM>(&state));

    std::stable_sort(monitors.begin(), monitors.end(), [](const MonitorInfo& a, const MonitorInfo& b) {
        if (a.primary != b.primary) {
            return a.primary;
        }
        if (a.bounds.left != b.bounds.left) {
            return a.bounds.left < b.bounds.left;
        }
        return a.bounds.top < b.bounds.top;
    });

    // Garantit l'unicite des identifiants (deux ecrans sans chemin PnP exploitable).
    std::set<std::string> seen;
    for (auto& monitor : monitors) {
        std::string candidate = monitor.id;
        for (int suffix = 2; !seen.insert(candidate).second; ++suffix) {
            candidate = monitor.id + "#" + std::to_string(suffix);
        }
        monitor.id = std::move(candidate);
    }
    return monitors;
}

MonitorInfo MakeSpanMonitor(const std::vector<MonitorInfo>& monitors) {
    MonitorInfo span;
    span.id = kSpanMonitorId;
    span.gdiName = L"span";
    span.friendlyName = L"Tous les ecrans";
    span.primary = true;
    if (monitors.empty()) {
        return span;
    }
    span.bounds = monitors.front().bounds;
    span.dpi = monitors.front().dpi;
    for (const auto& monitor : monitors) {
        UnionRect(&span.bounds, &span.bounds, &monitor.bounds);
    }
    span.workArea = span.bounds;
    return span;
}

}  // namespace lwe
