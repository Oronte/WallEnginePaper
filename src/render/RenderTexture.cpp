#include "render/RenderTexture.h"

namespace lwe {

bool RenderTexture::Create(ID3D11Device* device, uint32_t w, uint32_t h) {
    Reset();
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = w;
    desc.Height = h;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(device->CreateTexture2D(&desc, nullptr, &texture)) ||
        FAILED(device->CreateRenderTargetView(texture.Get(), nullptr, &rtv)) ||
        FAILED(device->CreateShaderResourceView(texture.Get(), nullptr, &srv))) {
        Reset();
        return false;
    }
    width = w;
    height = h;
    return true;
}

void RenderTexture::Reset() noexcept {
    srv.Reset();
    rtv.Reset();
    texture.Reset();
    width = height = 0;
}

}  // namespace lwe
