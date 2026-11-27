#include "wallpaper/video/VideoThumbnail.h"

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <propidl.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace lwe {
namespace {

using Microsoft::WRL::ComPtr;

constexpr LONGLONG kSeekPosition = 15'000'000;  // 1,5 s en unites de 100 ns
constexpr int kMaxSamplesToRead = 60;

}  // namespace

LoadedTexture LoadVideoThumbnail(ID3D11Device* device, const std::filesystem::path& file, uint32_t maxDimension) {
    ComPtr<IMFAttributes> attributes;
    ComPtr<IMFSourceReader> reader;
    if (FAILED(MFCreateAttributes(&attributes, 1)) ||
        FAILED(attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE)) ||
        FAILED(MFCreateSourceReaderFromURL(file.c_str(), attributes.Get(), &reader))) {
        return {};
    }
    const DWORD stream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
    reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
    reader->SetStreamSelection(stream, TRUE);

    ComPtr<IMFMediaType> requested;
    ComPtr<IMFMediaType> actual;
    UINT32 width = 0;
    UINT32 height = 0;
    if (FAILED(MFCreateMediaType(&requested)) || FAILED(requested->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video)) ||
        FAILED(requested->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32)) ||
        FAILED(reader->SetCurrentMediaType(stream, nullptr, requested.Get())) ||
        FAILED(reader->GetCurrentMediaType(stream, &actual)) ||
        FAILED(MFGetAttributeSize(actual.Get(), MF_MT_FRAME_SIZE, &width, &height)) || width == 0 || height == 0) {
        return {};
    }
    LONG stride = static_cast<LONG>(width * 4);
    UINT32 defaultStride = 0;
    if (SUCCEEDED(actual->GetUINT32(MF_MT_DEFAULT_STRIDE, &defaultStride))) {
        stride = static_cast<LONG>(static_cast<INT32>(defaultStride));
    }

    PROPVARIANT position;
    PropVariantInit(&position);
    position.vt = VT_I8;
    position.hVal.QuadPart = kSeekPosition;
    reader->SetCurrentPosition(GUID_NULL, position);  // video trop courte : on garde la premiere trame
    PropVariantClear(&position);

    ComPtr<IMFSample> sample;
    for (int i = 0; i < kMaxSamplesToRead && !sample; ++i) {
        DWORD streamIndex = 0;
        DWORD flags = 0;
        LONGLONG timestamp = 0;
        if (FAILED(reader->ReadSample(stream, 0, &streamIndex, &flags, &timestamp, &sample)) ||
            (flags & MF_SOURCE_READERF_ENDOFSTREAM)) {
            break;
        }
    }
    ComPtr<IMFMediaBuffer> buffer;
    if (!sample || FAILED(sample->ConvertToContiguousBuffer(&buffer))) {
        return {};
    }
    BYTE* data = nullptr;
    DWORD length = 0;
    if (FAILED(buffer->Lock(&data, nullptr, &length))) {
        return {};
    }
    const size_t absStride = static_cast<size_t>(std::abs(stride));
    if (length < absStride * height) {
        buffer->Unlock();
        return {};
    }

    // Reduction par moyenne de blocs (facteur entier) : suffisant pour une miniature.
    const uint32_t factor = std::max<uint32_t>(1, (std::max(width, height) + maxDimension - 1) / maxDimension);
    const uint32_t outWidth = std::max<uint32_t>(1, width / factor);
    const uint32_t outHeight = std::max<uint32_t>(1, height / factor);
    std::vector<uint8_t> pixels(static_cast<size_t>(outWidth) * outHeight * 4);
    for (uint32_t y = 0; y < outHeight; ++y) {
        for (uint32_t x = 0; x < outWidth; ++x) {
            uint32_t sum[3] = {0, 0, 0};
            for (uint32_t dy = 0; dy < factor; ++dy) {
                const uint32_t sy = y * factor + dy;
                const BYTE* row = stride >= 0 ? data + sy * absStride : data + (height - 1 - sy) * absStride;
                for (uint32_t dx = 0; dx < factor; ++dx) {
                    const BYTE* px = row + static_cast<size_t>(x * factor + dx) * 4;
                    sum[0] += px[0];
                    sum[1] += px[1];
                    sum[2] += px[2];
                }
            }
            uint8_t* out = pixels.data() + (static_cast<size_t>(y) * outWidth + x) * 4;
            const uint32_t count = factor * factor;
            out[0] = static_cast<uint8_t>(sum[0] / count);
            out[1] = static_cast<uint8_t>(sum[1] / count);
            out[2] = static_cast<uint8_t>(sum[2] / count);
            out[3] = 255;
        }
    }
    buffer->Unlock();

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = outWidth;
    desc.Height = outHeight;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;  // RGB32 Media Foundation = BGRX
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    const D3D11_SUBRESOURCE_DATA initial{pixels.data(), outWidth * 4, 0};
    ComPtr<ID3D11Texture2D> texture;
    LoadedTexture result;
    if (FAILED(device->CreateTexture2D(&desc, &initial, &texture)) ||
        FAILED(device->CreateShaderResourceView(texture.Get(), nullptr, &result.view))) {
        return {};
    }
    result.width = outWidth;
    result.height = outHeight;
    return result;
}

}  // namespace lwe
