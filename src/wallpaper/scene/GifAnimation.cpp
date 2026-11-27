#include "wallpaper/scene/GifAnimation.h"

#include <windows.h>
#include <propidl.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstring>

namespace lwe {
namespace {

using Microsoft::WRL::ComPtr;

constexpr size_t kMaxFrames = 1500;
constexpr uint32_t kMaxDimension = 4096;

uint32_t ReadUInt(IWICMetadataQueryReader* reader, const wchar_t* name, uint32_t fallback) {
    PROPVARIANT value;
    PropVariantInit(&value);
    uint32_t result = fallback;
    if (reader && SUCCEEDED(reader->GetMetadataByName(name, &value))) {
        if (value.vt == VT_UI2) {
            result = value.uiVal;
        } else if (value.vt == VT_UI1) {
            result = value.bVal;
        } else if (value.vt == VT_UI4) {
            result = value.ulVal;
        }
    }
    PropVariantClear(&value);
    return result;
}

}  // namespace

bool GifAnimation::Load(const std::filesystem::path& file, std::wstring& error) {
    frames_.clear();
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) ||
        FAILED(factory->CreateDecoderFromFilename(file.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand,
                                                  &decoder))) {
        error = L"image illisible";
        return false;
    }
    GUID container{};
    UINT frameCount = 0;
    decoder->GetContainerFormat(&container);
    decoder->GetFrameCount(&frameCount);
    if (container != GUID_ContainerFormatGif || frameCount < 2) {
        return false;
    }

    ComPtr<IWICMetadataQueryReader> global;
    decoder->GetMetadataQueryReader(&global);
    width_ = ReadUInt(global.Get(), L"/logscrdesc/Width", 0);
    height_ = ReadUInt(global.Get(), L"/logscrdesc/Height", 0);

    for (UINT i = 0; i < frameCount && frames_.size() < kMaxFrames; ++i) {
        ComPtr<IWICBitmapFrameDecode> source;
        if (FAILED(decoder->GetFrame(i, &source))) {
            continue;
        }
        ComPtr<IWICMetadataQueryReader> metadata;
        source->GetMetadataQueryReader(&metadata);
        Frame frame;
        UINT width = 0;
        UINT height = 0;
        source->GetSize(&width, &height);
        frame.left = ReadUInt(metadata.Get(), L"/imgdesc/Left", 0);
        frame.top = ReadUInt(metadata.Get(), L"/imgdesc/Top", 0);
        frame.width = width;
        frame.height = height;
        // Delai en centiemes de seconde ; comme les navigateurs, on corrige les valeurs nulles ou minuscules.
        const uint32_t delay = ReadUInt(metadata.Get(), L"/grctlext/Delay", 10);
        frame.delay = delay <= 1 ? 0.1 : delay / 100.0;
        frame.disposal = static_cast<uint8_t>(ReadUInt(metadata.Get(), L"/grctlext/Disposal", 0));

        ComPtr<IWICFormatConverter> converter;
        if (FAILED(factory->CreateFormatConverter(&converter)) ||
            FAILED(converter->Initialize(source.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr,
                                         0.0, WICBitmapPaletteTypeCustom))) {
            continue;
        }
        frame.rgba.resize(static_cast<size_t>(width) * height * 4);
        if (FAILED(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(frame.rgba.size()), frame.rgba.data()))) {
            continue;
        }
        width_ = std::max(width_, frame.left + frame.width);
        height_ = std::max(height_, frame.top + frame.height);
        frames_.push_back(std::move(frame));
    }
    if (frames_.size() < 2 || width_ == 0 || height_ == 0 || width_ > kMaxDimension || height_ > kMaxDimension) {
        frames_.clear();
        error = L"GIF anime non pris en charge";
        return false;
    }

    canvas_.assign(static_cast<size_t>(width_) * height_ * 4, 0);
    current_ = 0;
    timeInFrame_ = 0.0;
    DrawFrame(0);
    return true;
}

void GifAnimation::DrawFrame(size_t index) {
    const Frame& frame = frames_[index];
    if (frame.disposal == 3) {
        previous_ = canvas_;
    }
    const uint32_t right = std::min(frame.left + frame.width, width_);
    const uint32_t bottom = std::min(frame.top + frame.height, height_);
    for (uint32_t y = frame.top; y < bottom; ++y) {
        const uint8_t* src = frame.rgba.data() + (static_cast<size_t>(y - frame.top) * frame.width) * 4;
        uint8_t* dst = canvas_.data() + (static_cast<size_t>(y) * width_ + frame.left) * 4;
        for (uint32_t x = frame.left; x < right; ++x, src += 4, dst += 4) {
            if (src[3] != 0) {  // GIF : transparence binaire
                std::memcpy(dst, src, 4);
            }
        }
    }
}

void GifAnimation::DisposeFrame(size_t index) {
    const Frame& frame = frames_[index];
    if (frame.disposal == 2) {
        const uint32_t right = std::min(frame.left + frame.width, width_);
        const uint32_t bottom = std::min(frame.top + frame.height, height_);
        for (uint32_t y = frame.top; y < bottom; ++y) {
            std::memset(canvas_.data() + (static_cast<size_t>(y) * width_ + frame.left) * 4, 0,
                        static_cast<size_t>(right - frame.left) * 4);
        }
    } else if (frame.disposal == 3 && previous_.size() == canvas_.size()) {
        canvas_ = previous_;
    }
}

bool GifAnimation::Advance(double seconds) {
    if (frames_.size() < 2) {
        return false;
    }
    timeInFrame_ += seconds;
    bool changed = false;
    int guard = 0;
    while (timeInFrame_ >= frames_[current_].delay && guard++ < 64) {
        timeInFrame_ -= frames_[current_].delay;
        DisposeFrame(current_);
        current_ = (current_ + 1) % frames_.size();
        if (current_ == 0) {
            std::fill(canvas_.begin(), canvas_.end(), uint8_t{0});  // nouvelle boucle : toile vierge
        }
        DrawFrame(current_);
        changed = true;
    }
    if (guard >= 64) {
        timeInFrame_ = 0.0;  // retard enorme (sortie de pause) : on ne rattrape pas
    }
    return changed;
}

}  // namespace lwe
