#pragma once

#include <cstdint>

#include "core/Win32Util.h"

namespace lwe {

// Cadence le rendu a N images/s sans attente active : un timer haute resolution (Windows 10 1803+)
// est attendu avec MsgWaitForMultipleObjectsEx, si bien que les messages (tray, notifications)
// sont traites immediatement et que le CPU dort entre deux images.
class FrameLimiter {
public:
    FrameLimiter();

    void SetTargetFps(uint32_t fps) noexcept;
    uint32_t TargetFps() const noexcept { return fps_; }

    void Reset() noexcept;
    bool IsFrameDue() const noexcept;
    void OnFrameRendered() noexcept;

    // Retourne quand l'image suivante est due OU qu'un message arrive dans la file.
    void WaitForFrameOrMessage() const { WaitUntil(next_); }

    // Echeance de la prochaine image (unites QueryPerformanceCounter).
    int64_t NextDeadline() const noexcept { return next_; }
    // Attend jusqu'a `deadline` (QPC) ou l'arrivee d'un message ; permet de combiner plusieurs cadences.
    void WaitUntil(int64_t deadline) const;

    bool UsesHighResolutionTimer() const noexcept { return highResolution_; }

private:
    int64_t Now() const noexcept;

    UniqueHandle timer_;
    bool highResolution_ = false;
    int64_t frequency_ = 1;
    int64_t period_ = 1;
    int64_t next_ = 0;
    uint32_t fps_ = 60;
};

}  // namespace lwe
