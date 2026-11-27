#include "render/FrameLimiter.h"

#include <algorithm>

namespace lwe {

FrameLimiter::FrameLimiter() {
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    frequency_ = frequency.QuadPart;

    HANDLE timer =
        CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    highResolution_ = timer != nullptr;
    if (!timer) {
        timer = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
    }
    timer_.reset(timer);
    SetTargetFps(60);
}

int64_t FrameLimiter::Now() const noexcept {
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    return now.QuadPart;
}

void FrameLimiter::SetTargetFps(uint32_t fps) noexcept {
    fps_ = std::clamp<uint32_t>(fps, 1, 360);
    period_ = std::max<int64_t>(1, frequency_ / fps_);
    Reset();
}

void FrameLimiter::Reset() noexcept {
    next_ = Now();
}

bool FrameLimiter::IsFrameDue() const noexcept {
    return Now() >= next_;
}

void FrameLimiter::OnFrameRendered() noexcept {
    next_ += period_;
    // Trop en retard (pause, deplacement de fenetre, hibernation) : on se recale au lieu de rattraper.
    const int64_t now = Now();
    if (next_ < now - period_) {
        next_ = now;
    }
}

void FrameLimiter::WaitUntil(int64_t deadline) const {
    const int64_t remaining = deadline - Now();
    if (remaining <= 0) {
        return;
    }
    LARGE_INTEGER due{};
    due.QuadPart = -(remaining * 10'000'000 / frequency_);  // relatif, unites de 100 ns
    if (due.QuadPart == 0) {
        return;
    }
    if (timer_ && SetWaitableTimer(timer_.get(), &due, 0, nullptr, nullptr, FALSE)) {
        HANDLE handle = timer_.get();
        MsgWaitForMultipleObjectsEx(1, &handle, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    } else {
        const DWORD milliseconds = static_cast<DWORD>(remaining * 1000 / frequency_);
        MsgWaitForMultipleObjectsEx(0, nullptr, milliseconds, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    }
}

}  // namespace lwe
