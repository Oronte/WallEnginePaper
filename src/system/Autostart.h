#pragma once

namespace lwe::autostart {

// Lancement avec Windows via HKCU\Software\Microsoft\Windows\CurrentVersion\Run (aucun droit
// administrateur requis). La commande enregistree demarre l'application reduite dans la zone de
// notification : "<chemin>\WallEnginePaper.exe" --minimized

bool IsRegistered();
bool Register();
bool Unregister();

// Aligne le registre sur la configuration ; met a jour le chemin si l'executable a ete deplace.
void Sync(bool enabled);

}  // namespace lwe::autostart
