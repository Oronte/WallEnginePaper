#include "render/TextureWriter.h"

#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstring>
#include <vector>

#include "core/Win32Util.h"
#include "render/RenderDevice.h"

namespace lwe {

using Microsoft::WRL::ComPtr;

namespace {

// Encode une source WIC en PNG, reduite a `maxWidth` pixels de large si besoin (0 = taille d'origine).
bool EncodePng(IWICImagingFactory* factory, IWICBitmapSource* source, const std::filesystem::path& file,
               uint32_t maxWidth, std::wstring& error) {
    UINT width = 0;
    UINT height = 0;
    source->GetSize(&width, &height);
    ComPtr<IWICBitmapSource> input = source;
    if (maxWidth > 0 && width > maxWidth) {
        ComPtr<IWICBitmapScaler> scaler;
        const UINT scaledHeight = std::max<UINT>(1, static_cast<UINT>(static_cast<uint64_t>(height) * maxWidth / width));
        if (SUCCEEDED(factory->CreateBitmapScaler(&scaler)) &&
            SUCCEEDED(scaler->Initialize(source, maxWidth, scaledHeight, WICBitmapInterpolationModeHighQualityCubic))) {
            input = scaler;
            width = maxWidth;
            height = scaledHeight;
        }
    }
    ComPtr<IWICFormatConverter> converter;
    HRESULT hr = factory->CreateFormatConverter(&converter);
    if (SUCCEEDED(hr)) {
        hr = converter->Initialize(input.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0,
                                   WICBitmapPaletteTypeCustom);
    }
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    if (SUCCEEDED(hr)) hr = factory->CreateStream(&stream);
    if (SUCCEEDED(hr)) hr = stream->InitializeFromFilename(file.c_str(), GENERIC_WRITE);
    if (SUCCEEDED(hr)) hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    if (SUCCEEDED(hr)) hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (SUCCEEDED(hr)) hr = encoder->CreateNewFrame(&frame, nullptr);
    if (SUCCEEDED(hr)) hr = frame->Initialize(nullptr);
    if (SUCCEEDED(hr)) hr = frame->SetSize(width, height);
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (SUCCEEDED(hr)) hr = frame->SetPixelFormat(&format);
    if (SUCCEEDED(hr)) hr = frame->WriteSource(converter.Get(), nullptr);
    if (SUCCEEDED(hr)) hr = frame->Commit();
    if (SUCCEEDED(hr)) hr = encoder->Commit();
    if (FAILED(hr)) {
        error = L"encodage PNG : " + HResultToString(hr);
        return false;
    }
    return true;
}

ComPtr<IWICImagingFactory> WicFactory() {
    ComPtr<IWICImagingFactory> factory;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    return factory;
}

}  // namespace

bool SaveTextureToPng(RenderDevice& device, ID3D11Texture2D* texture, const std::filesystem::path& file,
                      uint32_t maxWidth, std::wstring& error) {
    if (!texture) {
        error = L"aucune texture";
        return false;
    }
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    if (desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM) {
        error = L"format de texture non pris en charge";
        return false;
    }
    D3D11_TEXTURE2D_DESC stagingDesc = desc;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging;
    HRESULT hr = device.Device()->CreateTexture2D(&stagingDesc, nullptr, &staging);
    if (FAILED(hr)) {
        error = L"texture de lecture : " + HResultToString(hr);
        return false;
    }
    device.Context()->CopyResource(staging.Get(), texture);

    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr = device.Context()->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        error = L"lecture de la texture : " + HResultToString(hr);
        return false;
    }
    std::vector<uint8_t> pixels(static_cast<size_t>(desc.Width) * desc.Height * 4);
    for (UINT y = 0; y < desc.Height; ++y) {
        const auto* row = static_cast<const uint8_t*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch;
        uint8_t* out = pixels.data() + static_cast<size_t>(y) * desc.Width * 4;
        std::memcpy(out, row, static_cast<size_t>(desc.Width) * 4);
        for (UINT x = 0; x < desc.Width; ++x) {
            out[x * 4 + 3] = 255;  // alpha opaque
        }
    }
    device.Context()->Unmap(staging.Get(), 0);

    const ComPtr<IWICImagingFactory> factory = WicFactory();
    ComPtr<IWICBitmap> bitmap;
    if (!factory || FAILED(factory->CreateBitmapFromMemory(desc.Width, desc.Height, GUID_WICPixelFormat32bppBGRA,
                                                           desc.Width * 4, static_cast<UINT>(pixels.size()),
                                                           pixels.data(), &bitmap))) {
        error = L"WIC indisponible";
        return false;
    }
    return EncodePng(factory.Get(), bitmap.Get(), file, maxWidth, error);
}

bool ResavePng(IStream* png, const std::filesystem::path& file, uint32_t maxWidth, std::wstring& error) {
    const ComPtr<IWICImagingFactory> factory = WicFactory();
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    LARGE_INTEGER start{};
    png->Seek(start, STREAM_SEEK_SET, nullptr);
    if (!factory || FAILED(factory->CreateDecoderFromStream(png, nullptr, WICDecodeMetadataCacheOnLoad, &decoder)) ||
        FAILED(decoder->GetFrame(0, &frame))) {
        error = L"image capturee illisible";
        return false;
    }
    return EncodePng(factory.Get(), frame.Get(), file, maxWidth, error);
}

}  // namespace lwe
