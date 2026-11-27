#include "system/SystemInfo.h"

#include <dwmapi.h>

#include <cwchar>

#include "core/Win32Util.h"

namespace lwe::systeminfo {
namespace {

bool ReadDword(HKEY root, const wchar_t* path, const wchar_t* name, DWORD& value) {
    DWORD size = sizeof(value);
    return RegGetValueW(root, path, name, RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS;
}

}  // namespace

bool AppsUseLightTheme() {
    DWORD value = 1;
    ReadDword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
              L"AppsUseLightTheme", value);
    return value != 0;
}

bool ClientAreaAnimationsEnabled() {
    BOOL enabled = TRUE;
    if (!SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0)) {
        return true;
    }
    return enabled != FALSE;
}

std::array<float, 4> AccentColor() {
    // AccentColor (DWM) : 0xAABBGGRR ; repli sur la couleur de colorisation (0xAARRGGBB).
    DWORD abgr = 0;
    if (ReadDword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\DWM", L"AccentColor", abgr)) {
        return {static_cast<float>(abgr & 0xFF) / 255.0f, static_cast<float>((abgr >> 8) & 0xFF) / 255.0f,
                static_cast<float>((abgr >> 16) & 0xFF) / 255.0f, 1.0f};
    }
    DWORD argb = 0;
    BOOL opaque = FALSE;
    if (SUCCEEDED(DwmGetColorizationColor(&argb, &opaque))) {
        return {static_cast<float>((argb >> 16) & 0xFF) / 255.0f, static_cast<float>((argb >> 8) & 0xFF) / 255.0f,
                static_cast<float>(argb & 0xFF) / 255.0f, 1.0f};
    }
    return {0.0f, 0.47f, 0.84f, 1.0f};
}

std::wstring ForegroundExecutable() {
    HWND window = GetForegroundWindow();
    if (!window) {
        return {};
    }
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    if (processId == 0) {
        return {};
    }
    UniqueHandle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId));
    if (!process) {
        return {};
    }
    wchar_t path[MAX_PATH * 2] = {};
    DWORD length = static_cast<DWORD>(std::size(path));
    if (!QueryFullProcessImageNameW(process.get(), 0, path, &length)) {
        return {};
    }
    return std::wstring(path, length);
}

bool IsThemeChangeMessage(LPARAM lParam) {
    const auto* area = reinterpret_cast<const wchar_t*>(lParam);
    return area && std::wcscmp(area, L"ImmersiveColorSet") == 0;
}

}  // namespace lwe::systeminfo
