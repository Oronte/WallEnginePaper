#pragma once

#include <windows.h>

#include <array>
#include <optional>

#include "config/AppConfig.h"

namespace lwe {

enum class HotkeyStatus : uint8_t {
    Unset,       // aucun raccourci pour cette action
    Disabled,    // raccourcis desactives
    Registered,  // actif
    Conflict,    // deja pris par une autre application (RegisterHotKey refuse)
};

// Raccourcis clavier globaux (RegisterHotKey) recus par la fenetre de controle sous forme de WM_HOTKEY.
class HotkeyManager {
public:
    ~HotkeyManager() { UnregisterAll(); }

    // (Re)enregistre tous les raccourcis ; l'etat de chacun est consultable ensuite.
    void Apply(HWND window, const HotkeySettings& settings);
    void UnregisterAll();

    HotkeyStatus Status(HotkeyAction action) const noexcept { return status_[static_cast<size_t>(action)]; }
    // Action correspondant a l'identifiant recu dans WM_HOTKEY.
    std::optional<HotkeyAction> ActionFor(WPARAM id) const noexcept;

private:
    static constexpr int kFirstId = 0x5100;

    HWND window_ = nullptr;
    std::array<bool, kHotkeyActionCount> registered_{};
    std::array<HotkeyStatus, kHotkeyActionCount> status_{};
};

}  // namespace lwe
