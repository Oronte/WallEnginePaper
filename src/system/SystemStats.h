#pragma once

#include <windows.h>
#include <pdh.h>

#include <cstdint>

#include "wallpaper/SystemData.h"

struct IDXGIAdapter;

namespace lwe {

// Statistiques systeme pour les fonds web (wep.system) : CPU (GetSystemTimes), RAM (GlobalMemoryStatusEx),
// GPU et memoire video (compteurs PDH "GPU Engine" / "GPU Adapter Memory"), batterie. Echantillonnees une
// fois par seconde sur le thread principal, seulement quand une page s'abonne ; Stop() libere tout.
class SystemStats {
public:
    ~SystemStats() { Stop(); }

    // `adapter` (facultatif) : taille de la memoire video dediee.
    void Start(IDXGIAdapter* adapter);
    void Stop();
    bool IsRunning() const noexcept { return running_; }

    // Nouvel echantillon (deltas depuis le precedent).
    void Sample(SystemStatsInfo& out);

private:
    bool running_ = false;
    uint64_t lastIdle_ = 0;
    uint64_t lastTotal_ = 0;
    PDH_HQUERY query_ = nullptr;
    PDH_HCOUNTER gpuCounter_ = nullptr;
    PDH_HCOUNTER vramCounter_ = nullptr;
    double vramTotalGb_ = 0.0;
};

}  // namespace lwe
