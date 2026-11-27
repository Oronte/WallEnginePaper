#pragma once

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <wrl/client.h>

#include <cstdint>
#include <memory>
#include <string>

#include "config/AppConfig.h"
#include "core/Win32Util.h"
#include "desktop/MonitorEnumerator.h"
#include "input/InputForwarder.h"
#include "render/RenderTexture.h"
#include "render/TransitionRenderer.h"
#include "wallpaper/IWallpaperBackend.h"

namespace lwe {

class DesktopHost;
class RenderDevice;

struct FrameTiming {
    double seconds = 0.0;
    float deltaSeconds = 0.0f;
    uint64_t frameIndex = 0;
    float targetFps = 60.0f;
};

enum class SurfaceStatus { Presented, Skipped, DeviceLost, Error };

// Transition demandee lors d'un changement de fond (type deja resolu : jamais Random).
struct TransitionRequest {
    TransitionType type = TransitionType::Fade;
    float seconds = 0.0f;  // 0 = coupe nette
    float seed = 0.0f;     // [0, 1] : direction du balayage, motif du dissolve
};

// Une surface de rendu = une fenetre enfant de la couche du papier peint + sa swap chain DXGI
// + le backend de fond d'ecran qui y dessine. Une par ecran (ou une seule en mode Span).
class MonitorContext {
public:
    MonitorContext(RenderDevice& device, const DesktopHost& host, HINSTANCE instance, MonitorInfo info);
    ~MonitorContext();
    MonitorContext(const MonitorContext&) = delete;
    MonitorContext& operator=(const MonitorContext&) = delete;

    bool Create(float renderScale, std::wstring& error);

    // Avec une transition : l'ancien backend continue d'etre rendu (texture "depuis") jusqu'a la fin du
    // fondu, puis il est libere. Sans transition, ou si l'un des deux fonds s'affiche lui-meme (web), coupe nette.
    void AttachBackend(std::unique_ptr<IWallpaperBackend> backend, WallpaperDesc desc,
                       const TransitionRequest& transition = {});
    bool InTransition() const noexcept { return outgoing_ != nullptr; }
    IWallpaperBackend* Backend() const noexcept { return backend_.get(); }
    const WallpaperDesc& Desc() const noexcept { return desc_; }

    const MonitorInfo& Info() const noexcept { return info_; }
    HWND Window() const noexcept { return window_; }
    uint32_t BufferWidth() const noexcept { return bufferWidth_; }
    uint32_t BufferHeight() const noexcept { return bufferHeight_; }
    bool UsesFlipModel() const noexcept { return flipModel_; }

    void UpdatePointer(const GlobalPointerState& pointer, float deltaSeconds);
    SurfaceStatus RenderFrame(const FrameTiming& timing, const AudioSpectrum& audio);
    void Show();

    // `globalPause` : pause manuelle, ecran eteint, batterie... ; `autoPause` : cet ecran est recouvert
    // par une application plein ecran (PerformanceWatchdog). Le backend n'est notifie qu'aux transitions.
    void SetPaused(bool globalPause, bool autoPause);
    bool IsPaused() const noexcept { return paused_; }
    bool IsAutoPaused() const noexcept { return autoPaused_; }

    void MaintenanceTick();

private:
    void ComputeBufferSize(float renderScale);
    bool CreateSwapChain(std::wstring& error);
    HRESULT TryCreateSwapChain(DXGI_SWAP_EFFECT effect, UINT bufferCount);
    bool PrepareTransition();
    void EndTransition();

    RenderDevice& device_;
    const DesktopHost& host_;
    HINSTANCE instance_;
    MonitorInfo info_;

    HWND window_ = nullptr;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> renderTarget_;
    UniqueHandle frameLatencyWaitable_;  // signale quand la swap chain peut accepter une nouvelle image
    uint32_t bufferWidth_ = 1;
    uint32_t bufferHeight_ = 1;
    bool flipModel_ = false;

    std::unique_ptr<IWallpaperBackend> backend_;
    WallpaperDesc desc_;

    // Transition en cours (outgoing_ != nullptr).
    std::unique_ptr<IWallpaperBackend> outgoing_;
    TransitionRequest transition_;
    float transitionElapsed_ = 0.0f;
    uint32_t transitionFrames_ = 0;
    RenderTexture fromTexture_;
    RenderTexture toTexture_;
    std::unique_ptr<TransitionRenderer> transitionRenderer_;
    MouseState mouse_;

    bool latencyTokenHeld_ = false;
    bool paused_ = false;
    bool autoPaused_ = false;
    bool occluded_ = false;
    double lastOcclusionTest_ = 0.0;
    bool presentErrorLogged_ = false;
};

}  // namespace lwe
