#pragma once

#include <windows.h>

#include <array>
#include <string>

namespace lwe::systeminfo {

// Theme des applications Windows (Parametres > Personnalisation > Couleurs) : true = clair.
bool AppsUseLightTheme();

// "Afficher les animations dans Windows" (Accessibilite > Effets visuels) : false = animations coupees.
bool ClientAreaAnimationsEnabled();

// Couleur d'accentuation Windows (RGBA dans [0, 1]).
std::array<float, 4> AccentColor();

// Chemin complet de l'executable de la fenetre au premier plan ("" si inconnu ou inaccessible).
std::wstring ForegroundExecutable();

// Message WM_SETTINGCHANGE signalant un changement de theme clair/sombre ou de couleur d'accentuation.
bool IsThemeChangeMessage(LPARAM lParam);

}  // namespace lwe::systeminfo
