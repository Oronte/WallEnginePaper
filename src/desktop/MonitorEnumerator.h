#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace lwe {

inline constexpr char kSpanMonitorId[] = "span";

struct MonitorInfo {
    HMONITOR handle = nullptr;
    std::string id;             // identifiant stable (chemin du peripherique d'affichage), cle de config.json
    std::wstring gdiName;       // \\.\DISPLAYn : peut changer d'un demarrage a l'autre
    std::wstring friendlyName;  // nom EDID du moniteur ("DELL U2720Q")
    RECT bounds{};              // pixels physiques, coordonnees de l'ecran virtuel
    RECT workArea{};
    UINT dpi = 96;
    bool primary = false;

    int Width() const noexcept { return bounds.right - bounds.left; }
    int Height() const noexcept { return bounds.bottom - bounds.top; }
};

// Ecrans actifs, ecran principal en premier puis tries de gauche a droite.
// Le processus etant Per-Monitor DPI Aware v2, toutes les coordonnees sont en pixels physiques.
std::vector<MonitorInfo> EnumerateMonitors();

// Pseudo-ecran couvrant le rectangle englobant de tous les ecrans (mode "etendre").
MonitorInfo MakeSpanMonitor(const std::vector<MonitorInfo>& monitors);

}  // namespace lwe
