#include "wallpaper/scene/SceneBackend.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/Win32Util.h"
#include "render/RenderDevice.h"
#include "render/TextureLoader.h"

namespace lwe {
namespace {

constexpr float kDefaultParallax = 1.0f;
constexpr float kParallaxEpsilon = 0.0004f;  // en dessous, le deplacement est invisible : pas de redessin
constexpr uint32_t kMaxImageDimension = 8192;

}  // namespace

bool SceneBackend::Initialize(const BackendInitContext& context, std::wstring& error) {
    device_ = &context.device;
    width_ = std::max<uint32_t>(1, context.width);
    height_ = std::max<uint32_t>(1, context.height);
    appearance_.parallax = kDefaultParallax;
    if (!compositor_.Initialize(context.device, error)) {
        return false;
    }

    auto gif = std::make_unique<GifAnimation>();
    std::wstring gifError;
    if (gif->Load(context.desc.source, gifError)) {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = gif->Width();
        desc.Height = gif->Height();
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        HRESULT hr = device_->Device()->CreateTexture2D(&desc, nullptr, &dynamicTexture_);
        if (SUCCEEDED(hr)) {
            hr = device_->Device()->CreateShaderResourceView(dynamicTexture_.Get(), nullptr, &texture_);
        }
        if (FAILED(hr)) {
            error = L"texture GIF : " + HResultToString(hr);
            return false;
        }
        imageWidth_ = gif->Width();
        imageHeight_ = gif->Height();
        gif_ = std::move(gif);
        UploadGifCanvas();
    } else {
        LoadedTexture image = LoadTextureFromFile(device_->Device(), context.desc.source, kMaxImageDimension);
        if (!image) {
            error = L"image illisible : " + context.desc.source.wstring();
            return false;
        }
        texture_ = image.view;
        imageWidth_ = image.width;
        imageHeight_ = image.height;
    }

    for (const auto& [name, value] : context.desc.params) {
        SetParameter(name, value);
    }
    dirty_ = true;
    return true;
}

void SceneBackend::UploadGifCanvas() {
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(device_->Context()->Map(dynamicTexture_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        return;
    }
    const auto& canvas = gif_->Canvas();
    const size_t rowBytes = static_cast<size_t>(imageWidth_) * 4;
    for (uint32_t y = 0; y < imageHeight_; ++y) {
        std::memcpy(static_cast<uint8_t*>(mapped.pData) + y * mapped.RowPitch, canvas.data() + y * rowBytes, rowBytes);
    }
    device_->Context()->Unmap(dynamicTexture_.Get(), 0);
}

void SceneBackend::Resize(uint32_t width, uint32_t height) {
    width_ = std::max<uint32_t>(1, width);
    height_ = std::max<uint32_t>(1, height);
    dirty_ = true;
}

bool SceneBackend::Render(ID3D11RenderTargetView* target, const FrameContext& frame) {
    if (!target) {
        return false;
    }
    if (gif_ && gif_->Advance(static_cast<double>(frame.deltaTime) * speed_)) {
        UploadGifCanvas();
        dirty_ = true;
    }
    const float px = frame.mouse.parallaxX;
    const float py = frame.mouse.parallaxY;
    const bool parallaxMoved = appearance_.parallax > 0.0f && (std::fabs(px - drawnParallaxX_) > kParallaxEpsilon ||
                                                              std::fabs(py - drawnParallaxY_) > kParallaxEpsilon);
    if (!dirty_ && !parallaxMoved) {
        return false;
    }
    compositor_.Draw(target, width_, height_, texture_.Get(), imageWidth_, imageHeight_, appearance_, px, py);
    drawnParallaxX_ = px;
    drawnParallaxY_ = py;
    dirty_ = false;
    return true;
}

std::vector<ParamDescriptor> SceneBackend::Parameters() const {
    std::vector<ParamDescriptor> params = MediaAppearance::Descriptors(kDefaultParallax);
    if (gif_) {
        params.insert(params.begin(), ParamDescriptor{"speed", "Vitesse de l'animation", ParamKind::Float, 1.0f, 0.1f, 4.0f});
    }
    return params;
}

ParamMap SceneBackend::ParameterValues() const {
    ParamMap values;
    appearance_.Store(values);
    if (gif_) {
        values["speed"] = speed_;
    }
    return values;
}

void SceneBackend::SetParameter(std::string_view name, const ParamValue& value) {
    if (name == "speed") {
        if (const auto* f = std::get_if<float>(&value)) {
            speed_ = std::clamp(*f, 0.05f, 8.0f);
        }
    } else {
        appearance_.Apply(name, value);
    }
    dirty_ = true;
}

}  // namespace lwe
