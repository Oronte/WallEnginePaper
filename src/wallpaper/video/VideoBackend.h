#pragma once

#include <cstdint>
#include <memory>

#include "wallpaper/IWallpaperBackend.h"
#include "wallpaper/media/MediaCompositor.h"
#include "wallpaper/video/VideoPlayer.h"

namespace lwe {

// Fond video (.mp4, .webm, .mkv, .mov...) en boucle, decode par le GPU. Ne redessine que lorsqu'une
// nouvelle trame arrive (ou que la parallaxe / un reglage change).
class VideoBackend final : public IWallpaperBackend {
public:
    ~VideoBackend() override;

    WallpaperType Type() const noexcept override { return WallpaperType::Video; }

    bool Initialize(const BackendInitContext& context, std::wstring& error) override;
    void Resize(uint32_t width, uint32_t height) override;
    bool Render(ID3D11RenderTargetView* target, const FrameContext& frame) override;

    std::vector<ParamDescriptor> Parameters() const override;
    ParamMap ParameterValues() const override;
    void SetParameter(std::string_view name, const ParamValue& value) override;

    void OnPauseChanged(bool paused) override;
    void ReleaseNonCriticalResources() override;
    void SetAudioOutput(float volume, bool muted) override;
    bool HasFailed(std::wstring& error) const override;

private:
    std::shared_ptr<VideoPlayer> player_;
    MediaCompositor compositor_;
    MediaAppearance appearance_;
    float speed_ = 1.0f;
    uint32_t width_ = 1;
    uint32_t height_ = 1;
    uint64_t drawnSerial_ = 0;
    bool dirty_ = true;
    float drawnParallaxX_ = 0.0f;
    float drawnParallaxY_ = 0.0f;
};

}  // namespace lwe
