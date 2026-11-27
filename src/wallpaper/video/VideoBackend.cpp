#include "wallpaper/video/VideoBackend.h"

#include <algorithm>
#include <cmath>

namespace lwe {
namespace {

constexpr float kDefaultParallax = 0.0f;
constexpr float kParallaxEpsilon = 0.0004f;

}  // namespace

VideoBackend::~VideoBackend() {
    if (player_) {
        player_->SetActive(this, false);
    }
}

bool VideoBackend::Initialize(const BackendInitContext& context, std::wstring& error) {
    width_ = std::max<uint32_t>(1, context.width);
    height_ = std::max<uint32_t>(1, context.height);
    appearance_.parallax = kDefaultParallax;
    if (!compositor_.Initialize(context.device, error)) {
        return false;
    }
    player_ = VideoPlayer::Acquire(context.device, context.desc.source, error);
    if (!player_) {
        return false;
    }
    for (const auto& [name, value] : context.desc.params) {
        SetParameter(name, value);
    }
    player_->SetAudio(context.volume, context.muted);
    player_->SetActive(this, true);
    dirty_ = true;
    return true;
}

void VideoBackend::Resize(uint32_t width, uint32_t height) {
    width_ = std::max<uint32_t>(1, width);
    height_ = std::max<uint32_t>(1, height);
    dirty_ = true;
}

bool VideoBackend::Render(ID3D11RenderTargetView* target, const FrameContext& frame) {
    if (!target || !player_) {
        return false;
    }
    player_->Update();
    const bool newFrame = player_->FrameSerial() != drawnSerial_;
    const float px = frame.mouse.parallaxX;
    const float py = frame.mouse.parallaxY;
    const bool parallaxMoved = appearance_.parallax > 0.0f && (std::fabs(px - drawnParallaxX_) > kParallaxEpsilon ||
                                                              std::fabs(py - drawnParallaxY_) > kParallaxEpsilon);
    if (!newFrame && !dirty_ && !parallaxMoved) {
        return false;
    }
    compositor_.Draw(target, width_, height_, player_->Texture(), player_->Width(), player_->Height(), appearance_, px,
                     py);
    drawnSerial_ = player_->FrameSerial();
    drawnParallaxX_ = px;
    drawnParallaxY_ = py;
    dirty_ = false;
    return true;
}

std::vector<ParamDescriptor> VideoBackend::Parameters() const {
    std::vector<ParamDescriptor> params = MediaAppearance::Descriptors(kDefaultParallax);
    params.insert(params.begin(), ParamDescriptor{"speed", "Vitesse de lecture", ParamKind::Float, 1.0f, 0.25f, 2.0f});
    return params;
}

ParamMap VideoBackend::ParameterValues() const {
    ParamMap values;
    appearance_.Store(values);
    values["speed"] = speed_;
    return values;
}

void VideoBackend::SetParameter(std::string_view name, const ParamValue& value) {
    if (name == "speed") {
        if (const auto* f = std::get_if<float>(&value)) {
            speed_ = std::clamp(*f, 0.25f, 2.0f);
            if (player_) {
                player_->SetPlaybackRate(speed_);
            }
        }
    } else {
        appearance_.Apply(name, value);
    }
    dirty_ = true;
}

void VideoBackend::OnPauseChanged(bool paused) {
    if (player_) {
        player_->SetActive(this, !paused);
    }
    dirty_ = true;
}

void VideoBackend::ReleaseNonCriticalResources() {
    if (player_) {
        player_->Suspend();
    }
}

void VideoBackend::SetAudioOutput(float volume, bool muted) {
    if (player_) {
        player_->SetAudio(volume, muted);
    }
}

bool VideoBackend::HasFailed(std::wstring& error) const {
    return player_ && player_->Failed(error);
}

}  // namespace lwe
