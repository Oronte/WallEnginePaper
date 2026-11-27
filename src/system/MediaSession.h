#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>

#include "core/Win32Util.h"
#include "wallpaper/SystemData.h"

namespace lwe {

// Morceau en cours via les controles multimedia de Windows (GlobalSystemMediaTransportControlsSessionManager,
// C++/WinRT) : titre, artiste, album, pochette, lecture / pause, position. API locale de Windows, aucune
// requete reseau. Interroge une fois par seconde sur un thread dedie (MTA), seulement quand une page s'abonne.
class MediaSession {
public:
    MediaSession();
    ~MediaSession();
    MediaSession(const MediaSession&) = delete;
    MediaSession& operator=(const MediaSession&) = delete;

    void SetEnabled(bool enabled);
    bool IsEnabled() const noexcept { return thread_.joinable(); }

    // Copie l'etat courant ; retourne son numero de serie (incremente a chaque changement).
    uint64_t Snapshot(MediaInfo& out) const;

private:
    void ThreadMain();

    std::thread thread_;
    UniqueHandle stopEvent_;
    mutable std::mutex mutex_;
    MediaInfo info_;
    uint64_t serial_ = 0;
};

}  // namespace lwe
