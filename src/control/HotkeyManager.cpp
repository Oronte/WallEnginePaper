#include "control/HotkeyManager.h"

#include "core/Log.h"
#include "core/StringUtil.h"

namespace lwe {

void HotkeyManager::UnregisterAll() {
    for (size_t i = 0; i < kHotkeyActionCount; ++i) {
        if (registered_[i] && window_) {
            UnregisterHotKey(window_, kFirstId + static_cast<int>(i));
        }
        registered_[i] = false;
    }
}

void HotkeyManager::Apply(HWND window, const HotkeySettings& settings) {
    UnregisterAll();
    window_ = window;
    for (size_t i = 0; i < kHotkeyActionCount; ++i) {
        const Hotkey& hotkey = settings.keys[i];
        if (!settings.enabled) {
            status_[i] = HotkeyStatus::Disabled;
            continue;
        }
        if (hotkey.Empty()) {
            status_[i] = HotkeyStatus::Unset;
            continue;
        }
        // Deux actions sur la meme combinaison : seule la premiere est enregistree.
        bool duplicate = false;
        for (size_t j = 0; j < i; ++j) {
            duplicate = duplicate || (registered_[j] && settings.keys[j] == hotkey);
        }
        if (!duplicate && window &&
            RegisterHotKey(window, kFirstId + static_cast<int>(i), hotkey.modifiers | MOD_NOREPEAT, hotkey.key)) {
            registered_[i] = true;
            status_[i] = HotkeyStatus::Registered;
        } else {
            status_[i] = HotkeyStatus::Conflict;
            log::Warn(L"Raccourci {} ({}) indisponible : deja utilise", Utf8ToWide(ToString(static_cast<HotkeyAction>(i))),
                      Utf8ToWide(FormatHotkey(hotkey, true)));
        }
    }
}

std::optional<HotkeyAction> HotkeyManager::ActionFor(WPARAM id) const noexcept {
    const auto index = static_cast<long long>(id) - kFirstId;
    if (index < 0 || index >= static_cast<long long>(kHotkeyActionCount) || !registered_[static_cast<size_t>(index)]) {
        return std::nullopt;
    }
    return static_cast<HotkeyAction>(index);
}

}  // namespace lwe
