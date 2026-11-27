#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>
#include <memory>

#include "wallpaper/IWallpaperBackend.h"
#include "wallpaper/media/MediaCompositor.h"
#include "wallpaper/scene/GifAnimation.h"

namespace lwe {

// Fond "image" : PNG, JPEG, BMP, WebP (premiere image) ou GIF anime, avec parallaxe souris.
// Une image fixe n'est redessinee que lorsque la parallaxe ou un reglage change : cout quasi nul.
class SceneBackend final : public IWallpaperBackend {
public:
    WallpaperType Type() const noexcept override { return WallpaperType::Scene; }

    bool Initialize(const BackendInitContext& context, std::wstring& error) override;
    void Resize(uint32_t width, uint32_t height) override;
    bool Render(ID3D11RenderTargetView* target, const FrameContext& frame) override;

    std::vector<ParamDescriptor> Parameters() const override;
    ParamMap ParameterValues() const override;
    void SetParameter(std::string_view name, const ParamValue& value) override;

private:
    void UploadGifCanvas();

    RenderDevice* device_ = nullptr;
    MediaCompositor compositor_;
    MediaAppearance appearance_;
    float speed_ = 1.0f;  // vitesse des GIF animes

    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> texture_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> dynamicTexture_;  // GIF : toile mise a jour a chaque image
    std::unique_ptr<GifAnimation> gif_;
    uint32_t imageWidth_ = 0;
    uint32_t imageHeight_ = 0;
    uint32_t width_ = 1;
    uint32_t height_ = 1;

    bool dirty_ = true;
    float drawnParallaxX_ = 0.0f;
    float drawnParallaxY_ = 0.0f;
};

}  // namespace lwe
