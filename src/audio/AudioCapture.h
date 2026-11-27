#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

#include "core/Win32Util.h"
#include "wallpaper/IWallpaperBackend.h"

namespace lwe {

// Capture du son joue par le PC (WASAPI "loopback" sur la sortie audio par defaut) et calcul du spectre,
// sur un thread dedie. Uniquement ce qui sort des haut-parleurs : jamais le micro, et rien ne quitte le PC.
// Suit automatiquement le changement de sortie par defaut (casque branche, autre carte son...).
class AudioCapture {
public:
    AudioCapture();
    ~AudioCapture();
    AudioCapture(const AudioCapture&) = delete;
    AudioCapture& operator=(const AudioCapture&) = delete;

    // Demarre / arrete la capture (arretee : aucun thread, aucun cout).
    void SetEnabled(bool enabled);
    bool IsEnabled() const noexcept { return thread_.joinable(); }

    // Dernier spectre calcule (retombe a zero quand plus rien ne joue ou que la capture est arretee).
    void Snapshot(AudioSpectrum& out) const;
    std::wstring DeviceName() const;
    bool IsCapturing() const noexcept { return capturing_; }

private:
    void ThreadMain();

    std::thread thread_;
    UniqueHandle stopEvent_;
    std::atomic<bool> capturing_{false};

    mutable std::mutex mutex_;
    AudioSpectrum spectrum_;
    std::wstring deviceName_;
};

}  // namespace lwe
