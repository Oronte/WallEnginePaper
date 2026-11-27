#pragma once

#include <windows.h>
#include <d3d11.h>
#include <d3dcommon.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <string>
#include <string_view>

namespace lwe {

// Peripherique D3D11 unique, partage par toutes les surfaces et tous les backends.
// Cree avec le support video (D3D11VA) pour que Media Foundation decode directement en VRAM.
class RenderDevice {
public:
    bool Create(std::wstring& error);
    void Release() noexcept;
    bool IsValid() const noexcept { return device_ != nullptr; }

    ID3D11Device* Device() const noexcept { return device_.Get(); }
    ID3D11DeviceContext* Context() const noexcept { return context_.Get(); }
    IDXGIFactory2* Factory() const noexcept { return factory_.Get(); }
    ID3D11VertexShader* FullscreenVertexShader() const noexcept { return fullscreenVs_.Get(); }
    const std::wstring& AdapterName() const noexcept { return adapterName_; }

    // Rend au pilote la memoire temporaire (a appeler quand tout est en pause).
    void Trim() noexcept;

    // Compile du HLSL a l'execution (d3dcompiler_47.dll, fourni avec Windows).
    // `sourceName` sert aux messages d'erreur et a la resolution des #include relatifs.
    static Microsoft::WRL::ComPtr<ID3DBlob> CompileShader(std::string_view source, const std::string& sourceName,
                                                          const char* entryPoint, const char* target,
                                                          std::string& diagnostics);

private:
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<IDXGIFactory2> factory_;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> fullscreenVs_;
    D3D_FEATURE_LEVEL featureLevel_ = D3D_FEATURE_LEVEL_11_0;
    std::wstring adapterName_;
};

}  // namespace lwe
