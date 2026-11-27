#include "system/Autostart.h"

#include <windows.h>

#include <cwchar>
#include <optional>
#include <string>

#include "core/Log.h"
#include "core/Paths.h"
#include "core/StringUtil.h"

namespace lwe::autostart {
namespace {

constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

std::wstring ExpectedCommand() {
    return L"\"" + ExecutablePath().wstring() + L"\" --minimized";
}

std::optional<std::wstring> ReadCommand() {
    DWORD size = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, kRunKey, kProductName, RRF_RT_REG_SZ, nullptr, nullptr, &size) !=
            ERROR_SUCCESS ||
        size == 0) {
        return std::nullopt;
    }
    std::wstring value(size / sizeof(wchar_t), L'\0');
    if (RegGetValueW(HKEY_CURRENT_USER, kRunKey, kProductName, RRF_RT_REG_SZ, nullptr, value.data(), &size) !=
        ERROR_SUCCESS) {
        return std::nullopt;
    }
    value.resize(std::wcslen(value.c_str()));
    return value;
}

}  // namespace

bool IsRegistered() {
    return ReadCommand().has_value();
}

bool Register() {
    const std::wstring command = ExpectedCommand();
    const LSTATUS status =
        RegSetKeyValueW(HKEY_CURRENT_USER, kRunKey, kProductName, REG_SZ, command.c_str(),
                        static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    if (status != ERROR_SUCCESS) {
        log::Error(L"Inscription au demarrage de Windows impossible (erreur {})", status);
        return false;
    }
    log::Info(L"Lancement au demarrage de Windows active : {}", command);
    return true;
}

bool Unregister() {
    const LSTATUS status = RegDeleteKeyValueW(HKEY_CURRENT_USER, kRunKey, kProductName);
    if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND) {
        log::Error(L"Desinscription du demarrage de Windows impossible (erreur {})", status);
        return false;
    }
    log::Info(L"Lancement au demarrage de Windows desactive");
    return true;
}

void Sync(bool enabled) {
    const std::optional<std::wstring> current = ReadCommand();
    if (enabled) {
        if (!current || ToLower(*current) != ToLower(ExpectedCommand())) {
            Register();
        }
    } else if (current) {
        Unregister();
    }
}

}  // namespace lwe::autostart
