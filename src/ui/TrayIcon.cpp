#include "ui/TrayIcon.h"

#include <cwchar>
#include <string>

namespace lwe {
namespace {

template <size_t N>
void CopyText(wchar_t (&destination)[N], std::wstring_view text) {
    const std::wstring copy(text);
    wcsncpy_s(destination, copy.c_str(), _TRUNCATE);
}

}  // namespace

TrayIcon::~TrayIcon() {
    Remove();
}

bool TrayIcon::Add(HWND owner, UINT id, HICON icon, std::wstring_view tooltip) {
    Remove();
    data_ = {};
    data_.cbSize = sizeof(data_);
    data_.hWnd = owner;
    data_.uID = id;
    data_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    data_.uCallbackMessage = kCallbackMessage;
    data_.hIcon = icon;
    CopyText(data_.szTip, tooltip);
    return Readd();
}

bool TrayIcon::Readd() {
    if (!data_.hWnd) {
        return false;
    }
    NOTIFYICONDATAW add = data_;
    add.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    if (!Shell_NotifyIconW(NIM_ADD, &add)) {
        // Apres un redemarrage de l'Explorateur, l'icone peut deja exister : on la met a jour.
        if (!Shell_NotifyIconW(NIM_MODIFY, &add)) {
            added_ = false;
            return false;
        }
    }
    add.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &add);
    added_ = true;
    return true;
}

void TrayIcon::Remove() {
    if (added_) {
        Shell_NotifyIconW(NIM_DELETE, &data_);
        added_ = false;
    }
}

void TrayIcon::SetTooltip(std::wstring_view tooltip) {
    CopyText(data_.szTip, tooltip);
    if (added_) {
        NOTIFYICONDATAW update = data_;
        update.uFlags = NIF_TIP | NIF_SHOWTIP;
        Shell_NotifyIconW(NIM_MODIFY, &update);
    }
}

void TrayIcon::ShowNotification(std::wstring_view title, std::wstring_view text) {
    if (!added_) {
        return;
    }
    NOTIFYICONDATAW info = data_;
    info.uFlags = NIF_INFO;
    info.dwInfoFlags = NIIF_INFO | NIIF_RESPECT_QUIET_TIME;
    CopyText(info.szInfoTitle, title);
    CopyText(info.szInfo, text);
    Shell_NotifyIconW(NIM_MODIFY, &info);
}

}  // namespace lwe
