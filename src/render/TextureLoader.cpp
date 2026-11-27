#include "render/TextureLoader.h"

#include <windows.h>
#include <wincodec.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace lwe {
namespace {

using Microsoft::WRL::ComPtr;

ComPtr<IWICImagingFactory> CreateFactory() {
    ComPtr<IWICImagingFactory> factory;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    return factory;
}

LoadedTexture FromDecoder(ID3D11Device* device, IWICImagingFactory* factory, IWICBitmapDecoder* decoder,
                          uint32_t maxDimension) {
    LoadedTexture result;
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame))) {
        return result;
    }
    UINT width = 0;
    UINT height = 0;
    frame->GetSize(&width, &height);
    if (width == 0 || height == 0) {
        return result;
    }

    ComPtr<IWICBitmapSource> source = frame;
    if (maxDimension > 0 && (width > maxDimension || height > maxDimension)) {
        const double scale = static_cast<double>(maxDimension) / std::max(width, height);
        const UINT scaledWidth = std::max<UINT>(1, static_cast<UINT>(std::lround(width * scale)));
        const UINT scaledHeight = std::max<UINT>(1, static_cast<UINT>(std::lround(height * scale)));
        ComPtr<IWICBitmapScaler> scaler;
        if (SUCCEEDED(factory->CreateBitmapScaler(&scaler)) &&
            SUCCEEDED(scaler->Initialize(frame.Get(), scaledWidth, scaledHeight,
                                         WICBitmapInterpolationModeHighQualityCubic))) {
            source = scaler;
            width = scaledWidth;
            height = scaledHeight;
        }
    }

    ComPtr<IWICFormatConverter> converter;
    if (FAILED(factory->CreateFormatConverter(&converter)) ||
        FAILED(converter->Initialize(source.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr,
                                     0.0, WICBitmapPaletteTypeCustom))) {
        return result;
    }
    const UINT stride = width * 4;
    std::vector<uint8_t> pixels(static_cast<size_t>(stride) * height);
    if (FAILED(converter->CopyPixels(nullptr, stride, static_cast<UINT>(pixels.size()), pixels.data()))) {
        return result;
    }

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    const D3D11_SUBRESOURCE_DATA initial{pixels.data(), stride, 0};
    ComPtr<ID3D11Texture2D> texture;
    if (FAILED(device->CreateTexture2D(&desc, &initial, &texture)) ||
        FAILED(device->CreateShaderResourceView(texture.Get(), nullptr, &result.view))) {
        result.view.Reset();
        return result;
    }
    result.width = width;
    result.height = height;
    return result;
}

}  // namespace

LoadedTexture LoadTextureFromFile(ID3D11Device* device, const std::filesystem::path& file, uint32_t maxDimension) {
    const ComPtr<IWICImagingFactory> factory = CreateFactory();
    ComPtr<IWICBitmapDecoder> decoder;
    if (!factory || FAILED(factory->CreateDecoderFromFilename(file.c_str(), nullptr, GENERIC_READ,
                                                              WICDecodeMetadataCacheOnDemand, &decoder))) {
        return {};
    }
    return FromDecoder(device, factory.Get(), decoder.Get(), maxDimension);
}

LoadedTexture LoadTextureFromMemory(ID3D11Device* device, const void* data, size_t size, uint32_t maxDimension) {
    const ComPtr<IWICImagingFactory> factory = CreateFactory();
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapDecoder> decoder;
    if (!factory || FAILED(factory->CreateStream(&stream)) ||
        FAILED(stream->InitializeFromMemory(static_cast<BYTE*>(const_cast<void*>(data)), static_cast<DWORD>(size))) ||
        FAILED(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder))) {
        return {};
    }
    return FromDecoder(device, factory.Get(), decoder.Get(), maxDimension);
}

LoadedTexture LoadTextureFromResource(ID3D11Device* device, int resourceId, uint32_t maxDimension) {
    HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
    if (!resource) {
        return {};
    }
    HGLOBAL handle = LoadResource(nullptr, resource);
    const void* data = handle ? LockResource(handle) : nullptr;
    const DWORD size = SizeofResource(nullptr, resource);
    if (!data || size == 0) {
        return {};
    }
    return LoadTextureFromMemory(device, data, size, maxDimension);
}

}  // namespace lwe
