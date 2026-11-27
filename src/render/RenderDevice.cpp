#include "render/RenderDevice.h"

#include <d3d11_4.h>
#include <d3dcompiler.h>
#include <dxgi1_3.h>

#include <format>
#include <iterator>

#include "core/Log.h"
#include "core/Win32Util.h"

namespace lwe {
namespace {

using Microsoft::WRL::ComPtr;

// Triangle couvrant tout l'ecran genere a partir de SV_VertexID : ni vertex buffer ni input layout.
constexpr char kFullscreenVertexShader[] = R"hlsl(
float4 main(uint id : SV_VertexID) : SV_Position
{
    float2 uv = float2((id << 1) & 2, id & 2);
    return float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}
)hlsl";

// Priorite GPU legerement abaissee : un jeu au premier plan passe toujours avant le fond d'ecran.
constexpr INT kGpuThreadPriority = -4;

const wchar_t* FeatureLevelName(D3D_FEATURE_LEVEL level) noexcept {
    switch (level) {
    case D3D_FEATURE_LEVEL_11_1: return L"11_1";
    case D3D_FEATURE_LEVEL_11_0: return L"11_0";
    case D3D_FEATURE_LEVEL_10_1: return L"10_1";
    case D3D_FEATURE_LEVEL_10_0: return L"10_0";
    default: return L"?";
    }
}

}  // namespace

bool RenderDevice::Create(std::wstring& error) {
    Release();

    static constexpr D3D_FEATURE_LEVEL kLevels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
                                                    D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    // PREVENT_INTERNAL_THREADING_OPTIMIZATIONS : charge GPU tres legere, donc pas de thread de travail
    // dans le pilote. Mesure sur RTX 3080 / 2 ecrans a 60 FPS : -35 % de CPU (le thread appelant
    // ne fait plus d'attente active sur le thread du pilote a chaque Present).
    const UINT baseFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS;
    // Le support video peut etre refuse par certains pilotes/WARP : on retente sans.
    const UINT attempts[] = {baseFlags | D3D11_CREATE_DEVICE_VIDEO_SUPPORT, baseFlags};

    HRESULT hr = E_FAIL;
    for (const UINT flags : attempts) {
#ifdef _DEBUG
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags | D3D11_CREATE_DEVICE_DEBUG,
                               kLevels, static_cast<UINT>(std::size(kLevels)), D3D11_SDK_VERSION, &device_,
                               &featureLevel_, &context_);
        if (SUCCEEDED(hr)) {
            break;
        }
#endif
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, kLevels,
                               static_cast<UINT>(std::size(kLevels)), D3D11_SDK_VERSION, &device_, &featureLevel_,
                               &context_);
        if (SUCCEEDED(hr)) {
            break;
        }
    }
    if (FAILED(hr)) {
        error = L"D3D11CreateDevice : " + HResultToString(hr);
        return false;
    }

    ComPtr<IDXGIDevice1> dxgiDevice;
    hr = device_.As(&dxgiDevice);
    if (FAILED(hr)) {
        error = L"IDXGIDevice1 : " + HResultToString(hr);
        Release();
        return false;
    }
    // Une seule image en file d'attente : latence et memoire minimales.
    dxgiDevice->SetMaximumFrameLatency(1);
    if (FAILED(dxgiDevice->SetGPUThreadPriority(kGpuThreadPriority))) {
        log::Debug(L"SetGPUThreadPriority refuse (sans effet sur le rendu)");
    }

    ComPtr<IDXGIAdapter> adapter;
    hr = dxgiDevice->GetAdapter(&adapter);
    if (SUCCEEDED(hr)) {
        hr = adapter->GetParent(IID_PPV_ARGS(&factory_));
    }
    if (FAILED(hr)) {
        error = L"IDXGIFactory2 : " + HResultToString(hr);
        Release();
        return false;
    }
    DXGI_ADAPTER_DESC adapterDesc{};
    if (SUCCEEDED(adapter->GetDesc(&adapterDesc))) {
        adapterName_ = adapterDesc.Description;
    }

    // Media Foundation (etape 3) accede au peripherique depuis ses propres threads.
    ComPtr<ID3D11Multithread> multithread;
    if (SUCCEEDED(context_.As(&multithread))) {
        multithread->SetMultithreadProtected(TRUE);
    }

    std::string diagnostics;
    ComPtr<ID3DBlob> vsCode =
        CompileShader(kFullscreenVertexShader, "lwe_fullscreen_vs", "main", "vs_5_0", diagnostics);
    if (!vsCode) {
        error = L"Compilation du vertex shader plein ecran impossible";
        Release();
        return false;
    }
    hr = device_->CreateVertexShader(vsCode->GetBufferPointer(), vsCode->GetBufferSize(), nullptr, &fullscreenVs_);
    if (FAILED(hr)) {
        error = L"CreateVertexShader : " + HResultToString(hr);
        Release();
        return false;
    }

    log::Info(L"GPU : {} (Direct3D {}, VRAM dediee {} Mo)", adapterName_, FeatureLevelName(featureLevel_),
              adapterDesc.DedicatedVideoMemory / (1024 * 1024));
    return true;
}

void RenderDevice::Release() noexcept {
    if (context_) {
        context_->ClearState();
        context_->Flush();
    }
    fullscreenVs_.Reset();
    factory_.Reset();
    context_.Reset();
    device_.Reset();
}

void RenderDevice::Trim() noexcept {
    if (!device_) {
        return;
    }
    context_->ClearState();
    context_->Flush();
    ComPtr<IDXGIDevice3> dxgiDevice;
    if (SUCCEEDED(device_.As(&dxgiDevice))) {
        dxgiDevice->Trim();
    }
}

Microsoft::WRL::ComPtr<ID3DBlob> RenderDevice::CompileShader(std::string_view source, const std::string& sourceName,
                                                             const char* entryPoint, const char* target,
                                                             std::string& diagnostics) {
    UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
#ifdef _DEBUG
    flags |= D3DCOMPILE_DEBUG;
#endif
    ComPtr<ID3DBlob> code;
    ComPtr<ID3DBlob> errors;
    const HRESULT hr = D3DCompile(source.data(), source.size(), sourceName.c_str(), nullptr,
                                  D3D_COMPILE_STANDARD_FILE_INCLUDE, entryPoint, target, flags, 0, &code, &errors);
    diagnostics.clear();
    if (errors && errors->GetBufferSize() > 0) {
        diagnostics.assign(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize());
        while (!diagnostics.empty() && (diagnostics.back() == '\0' || diagnostics.back() == '\n' ||
                                        diagnostics.back() == '\r')) {
            diagnostics.pop_back();
        }
    }
    if (FAILED(hr)) {
        if (diagnostics.empty()) {
            diagnostics = std::format("D3DCompile a echoue (HRESULT 0x{:08X})", static_cast<unsigned>(hr));
        }
        return nullptr;
    }
    return code;
}

}  // namespace lwe
