#pragma once

#include <windows.h>
#include <shellapi.h>

#include <string_view>

namespace lwe {

// Icone de la zone de notification (Shell_NotifyIcon, NOTIFYICON_VERSION_4).
class TrayIcon {
public:
    static constexpr UINT kCallbackMessage = WM_APP + 1;

    ~TrayIcon();

    bool Add(HWND owner, UINT id, HICON icon, std::wstring_view tooltip);
    // A appeler sur le message "TaskbarCreated" (redemarrage de l'Explorateur).
    bool Readd();
    void Remove();

    void SetTooltip(std::wstring_view tooltip);
    void ShowNotification(std::wstring_view title, std::wstring_view text);

private:
    NOTIFYICONDATAW data_{};
    bool added_ = false;
};

}  // namespace lwe
