#pragma once

#include <windows.h>

#include <vector>

#include "config/AppConfig.h"
#include "desktop/MonitorEnumerator.h"

namespace lwe {

// Surveille, a basse frequence (~2 Hz), ce qui rend un fond inutile ou indesirable :
//   - une application plein ecran / borderless (jeu, video, presentation) sur un ecran donne ;
//   - optionnellement une fenetre maximisee qui recouvre l'ecran ;
//   - le passage sur batterie.
// Un ecran recouvert n'est de toute facon pas visible : le mettre en pause ne coute rien a l'utilisateur
// et rend le GPU au jeu.
class PerformanceWatchdog {
public:
    // `ignoredWindow` : notre propre fenetre de configuration (ne doit jamais mettre les fonds en pause).
    void Update(const PauseRules& rules, const std::vector<MonitorInfo>& monitors, HWND ignoredWindow);

    bool OnBattery() const noexcept { return onBattery_; }
    bool PresentationMode() const noexcept { return presentationMode_; }

    // Vrai si tous les ecrans que touche `area` sont recouverts.
    bool IsAreaCovered(const RECT& area) const;
    size_t CoveredMonitorCount() const noexcept { return covered_.size(); }

private:
    bool IsMonitorCovered(const MonitorInfo& monitor, const PauseRules& rules, HWND ignoredWindow) const;

    bool onBattery_ = false;
    bool presentationMode_ = false;
    std::vector<RECT> monitors_;
    std::vector<RECT> covered_;
};

}  // namespace lwe
