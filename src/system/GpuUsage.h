#pragma once

#include <windows.h>
#include <pdh.h>

namespace lwe {

// Utilisation GPU de ce processus (compteurs Windows "GPU Engine", comme le Gestionnaire des taches) :
// moteurs 3D (rendu, compositions) et decodage video. Sert aux mesures (--gpu-timing) ; un GPU partage rend
// les horodatages D3D inutilisables pour cela (ils incluent le travail des autres applications).
class GpuUsage {
public:
    ~GpuUsage() { Stop(); }
    bool Start();
    void Stop();
    bool IsRunning() const noexcept { return query_ != nullptr; }

    // Pourcentages depuis l'echantillon precedent (-1 si indisponible).
    void Sample(double& render3d, double& videoDecode);

private:
    PDH_HQUERY query_ = nullptr;
    PDH_HCOUNTER render_ = nullptr;
    PDH_HCOUNTER decode_ = nullptr;
};

}  // namespace lwe
