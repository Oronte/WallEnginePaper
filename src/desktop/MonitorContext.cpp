#include "desktop/MonitorContext.h"

#include <algorithm>
#include <cmath>

#include "core/Log.h"
#include "core/StringUtil.h"
#include "core/Win32Util.h"
#include "desktop/DesktopHost.h"
#include "render/RenderDevice.h"
#include "render/TextureWriter.h"

namespace lwe {
namespace {

constexpr wchar_t kSurfaceClass[] = L"LWE_WallpaperSurface";
constexpr double kOcclusionTestInterval = 0.25;
constexpr float kParallaxResponsiveness = 6.0f;  // plus grand = parallaxe plus nerveuse

LRESULT CALLBACK SurfaceWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_ERASEBKGND:
        return 1;  // la swap chain couvre toute la surface : pas d'effacement GDI (evite le scintillement)
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(window, &ps);
        EndPaint(window, &ps);
        return 0;
    }
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

void EnsureSurfaceClass(HINSTANCE instance) {
    static bool registered = false;
    if (registered) {
        return;
    }
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = SurfaceWindowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kSurfaceClass;
    registered = RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

bool IsDeviceLost(HRESULT hr) noexcept {
    return hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET || hr == DXGI_ERROR_DEVICE_HUNG ||
           hr == DXGI_ERROR_DRIVER_INTERNAL_ERROR;
}

}  // namespace

MonitorContext::MonitorContext(RenderDevice& device, const DesktopHost& host, HINSTANCE instance, MonitorInfo info)
    : device_(device), host_(host), instance_(instance), info_(std::move(info)) {}

MonitorContext::~MonitorContext() {
    // Ordre important : le backend peut posseder des fenetres enfants (WebView2) et des vues sur
    // le back buffer ; il disparait avant la swap chain, qui disparait avant la fenetre.
    outgoing_.reset();
    backend_.reset();
    renderTarget_.Reset();
    frameLatencyWaitable_.reset();
    swapChain_.Reset();
    if (window_) {
        DestroyWindow(window_);
        window_ = nullptr;
    }
}

bool MonitorContext::Create(float renderScale, std::wstring& error) {
    EnsureSurfaceClass(instance_);
    window_ = host_.CreateSurfaceWindow(info_.bounds, kSurfaceClass, instance_);
    if (!window_) {
        error = L"creation de la fenetre de surface impossible";
        return false;
    }
    ComputeBufferSize(renderScale);
    return CreateSwapChain(error);
}

void MonitorContext::ComputeBufferSize(float renderScale) {
    const float width = static_cast<float>(std::max(1, info_.Width()));
    const float height = static_cast<float>(std::max(1, info_.Height()));
    constexpr float kMaxDimension = static_cast<float>(D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION);
    // En mode Span sur plusieurs ecrans 4K, on peut depasser la taille de texture maximale.
    const float scale =
        std::min({std::clamp(renderScale, 0.25f, 1.0f), kMaxDimension / width, kMaxDimension / height});
    bufferWidth_ = std::max<uint32_t>(1, static_cast<uint32_t>(std::lround(width * scale)));
    bufferHeight_ = std::max<uint32_t>(1, static_cast<uint32_t>(std::lround(height * scale)));
}

HRESULT MonitorContext::TryCreateSwapChain(DXGI_SWAP_EFFECT effect, UINT bufferCount) {
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = bufferWidth_;
    desc.Height = bufferHeight_;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = bufferCount;
    desc.Scaling = DXGI_SCALING_STRETCH;  // renderScale < 1 : le DWM etire, gratuitement
    desc.SwapEffect = effect;
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    const bool flip = effect == DXGI_SWAP_EFFECT_FLIP_DISCARD || effect == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    if (flip) {
        desc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
    }

    frameLatencyWaitable_.reset();
    swapChain_.Reset();
    const HRESULT hr =
        device_.Factory()->CreateSwapChainForHwnd(device_.Device(), window_, &desc, nullptr, nullptr, &swapChain_);
    if (FAILED(hr)) {
        return hr;
    }
    device_.Factory()->MakeWindowAssociation(window_, DXGI_MWA_NO_WINDOW_CHANGES | DXGI_MWA_NO_ALT_ENTER);
    flipModel_ = flip;
    Microsoft::WRL::ComPtr<IDXGISwapChain2> swapChain2;
    if (flip && SUCCEEDED(swapChain_.As(&swapChain2))) {
        swapChain2->SetMaximumFrameLatency(1);
        frameLatencyWaitable_.reset(swapChain2->GetFrameLatencyWaitableObject());
    }
    return hr;
}

bool MonitorContext::CreateSwapChain(std::wstring& error) {
    // 1) Flip model : latence minimale, composition directe par le DWM, aucune copie.
    HRESULT hr = TryCreateSwapChain(DXGI_SWAP_EFFECT_FLIP_DISCARD, 2);
    if (FAILED(hr) && DesktopHost::IsLayered(window_)) {
        log::Warn(L"Swap chain flip refusee sur fenetre layered ({}), nouvel essai sans WS_EX_LAYERED",
                  HResultToString(hr));
        DesktopHost::SetLayered(window_, false);
        hr = TryCreateSwapChain(DXGI_SWAP_EFFECT_FLIP_DISCARD, 2);
    }
    // 2) Repli : modele "blt" historique, compatible avec toutes les configurations de fenetre.
    if (FAILED(hr)) {
        log::Warn(L"Swap chain flip indisponible ({}), repli sur le modele blt", HResultToString(hr));
        hr = TryCreateSwapChain(DXGI_SWAP_EFFECT_DISCARD, 1);
    }
    if (FAILED(hr)) {
        error = L"CreateSwapChainForHwnd : " + HResultToString(hr);
        return false;
    }

    Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
    hr = swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (SUCCEEDED(hr)) {
        hr = device_.Device()->CreateRenderTargetView(backBuffer.Get(), nullptr, &renderTarget_);
    }
    if (FAILED(hr)) {
        error = L"CreateRenderTargetView : " + HResultToString(hr);
        return false;
    }

    log::Info(L"Surface « {} » : {}x{} px, rendu {}x{}, {}{}", info_.friendlyName, info_.Width(),
              info_.Height(), bufferWidth_, bufferHeight_, flipModel_ ? L"DXGI flip" : L"DXGI blt",
              DesktopHost::IsLayered(window_) ? L", layered" : L"");
    return true;
}

bool MonitorContext::PrepareTransition() {
    ID3D11Device* device = device_.Device();
    if (fromTexture_.width != bufferWidth_ || fromTexture_.height != bufferHeight_) {
        if (!fromTexture_.Create(device, bufferWidth_, bufferHeight_) ||
            !toTexture_.Create(device, bufferWidth_, bufferHeight_)) {
            fromTexture_.Reset();
            toTexture_.Reset();
            return false;
        }
    }
    if (!transitionRenderer_) {
        auto renderer = std::make_unique<TransitionRenderer>();
        std::wstring error;
        if (!renderer->Initialize(device_, error)) {
            log::Warn(L"Transition indisponible : {}", error);
            return false;
        }
        transitionRenderer_ = std::move(renderer);
    }
    // Contenu initial defini (une video peut ne pas avoir encore de trame a la premiere image).
    const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    device_.Context()->ClearRenderTargetView(fromTexture_.rtv.Get(), black);
    device_.Context()->ClearRenderTargetView(toTexture_.rtv.Get(), black);
    return true;
}

void MonitorContext::EndTransition() {
    if (!outgoing_) {
        return;
    }
    outgoing_.reset();
    log::Info(L"« {} » : transition terminee ({} images en {:.2f} s)", info_.friendlyName, transitionFrames_,
              transitionElapsed_);
    // Les textures (2 x la taille de l'ecran en VRAM) ne servent plus : liberees jusqu'a la prochaine fois.
    fromTexture_.Reset();
    toTexture_.Reset();
    if (backend_) {
        backend_->Invalidate();  // de nouveau dessine directement dans la swap chain
    }
}

void MonitorContext::AttachBackend(std::unique_ptr<IWallpaperBackend> backend, WallpaperDesc desc,
                                   const TransitionRequest& transition) {
    const bool animate = transition.seconds > 0.0f && transition.type != TransitionType::Random && backend_ &&
                         backend && backend_->PresentsToSwapChain() && backend->PresentsToSwapChain() && !paused_ &&
                         swapChain_ && renderTarget_;
    if (animate) {
        // Une transition deja en cours : le plus ancien des deux fonds disparait tout de suite.
        outgoing_.reset();
        outgoing_ = std::move(backend_);
        if (PrepareTransition()) {
            outgoing_->Invalidate();
            transition_ = transition;
            transitionElapsed_ = 0.0f;
            transitionFrames_ = 0;
            log::Info(L"« {} » : transition {} ({:.1f} s)", info_.friendlyName,
                      Utf8ToWide(ToString(transition.type)), transition.seconds);
        } else {
            outgoing_.reset();
        }
    } else {
        EndTransition();
    }
    backend_ = std::move(backend);
    desc_ = std::move(desc);
    if (backend_ && paused_) {
        backend_->OnPauseChanged(true);
    }
    // Backend qui affiche lui-meme (WebView2) : fond noir propre sous son contenu en attendant le chargement.
    if (backend_ && !backend_->PresentsToSwapChain() && renderTarget_ && swapChain_) {
        const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        device_.Context()->ClearRenderTargetView(renderTarget_.Get(), black);
        if (!latencyTokenHeld_ && frameLatencyWaitable_) {
            WaitForSingleObjectEx(frameLatencyWaitable_.get(), 100, FALSE);
        }
        swapChain_->Present(0, 0);
        latencyTokenHeld_ = false;
    }
}

void MonitorContext::UpdatePointer(const GlobalPointerState& pointer, float deltaSeconds) {
    if (!pointer.valid) {
        return;
    }
    const float width = static_cast<float>(std::max(1, info_.Width()));
    const float height = static_cast<float>(std::max(1, info_.Height()));
    const float localX = static_cast<float>(pointer.cursor.x - info_.bounds.left);
    const float localY = static_cast<float>(pointer.cursor.y - info_.bounds.top);
    const float toBufferX = static_cast<float>(bufferWidth_) / width;
    const float toBufferY = static_cast<float>(bufferHeight_) / height;

    mouse_.inside = localX >= 0.0f && localY >= 0.0f && localX < width && localY < height;
    mouse_.x = std::clamp(localX, 0.0f, width) * toBufferX;
    mouse_.y = (height - std::clamp(localY, 0.0f, height)) * toBufferY;
    mouse_.normX = std::clamp(localX / width * 2.0f - 1.0f, -1.0f, 1.0f);
    mouse_.normY = std::clamp(1.0f - localY / height * 2.0f, -1.0f, 1.0f);

    // Lissage exponentiel independant du framerate.
    const float blend = deltaSeconds > 0.0f ? 1.0f - std::exp(-deltaSeconds * kParallaxResponsiveness) : 1.0f;
    mouse_.parallaxX += ((parallaxEnabled_ ? mouse_.normX : 0.0f) - mouse_.parallaxX) * blend;
    mouse_.parallaxY += ((parallaxEnabled_ ? mouse_.normY : 0.0f) - mouse_.parallaxY) * blend;

    const bool pressedHere = pointer.primaryDown && pointer.overDesktop && mouse_.inside;
    if (pressedHere && !mouse_.pressed) {
        mouse_.clickX = mouse_.x;
        mouse_.clickY = mouse_.y;
    }
    mouse_.pressed = pressedHere;
}

SurfaceStatus MonitorContext::RenderFrame(const FrameTiming& timing, const AudioSpectrum& audio) {
    if (!backend_ || !swapChain_) {
        return SurfaceStatus::Skipped;
    }

    FrameContext frame;
    frame.time = timing.seconds;
    frame.deltaTime = timing.deltaSeconds;
    frame.frameIndex = timing.frameIndex;
    frame.frameRate = timing.targetFps;
    frame.width = bufferWidth_;
    frame.height = bufferHeight_;
    frame.mouse = mouse_;
    frame.audio = &audio;

    if (!capturePath_.empty()) {
        // Capture : une image dediee dans une texture hors ecran (le back buffer flip n'est pas relisible).
        const std::filesystem::path file = std::move(capturePath_);
        capturePath_.clear();
        if (!backend_->PresentsToSwapChain()) {
            if (!backend_->CapturePreview(file, captureWidth_)) {
                log::Error(L"Capture impossible : le fond n'est pas pret");
            }
        } else {
            RenderTexture target;
            std::wstring error;
            if (target.Create(device_.Device(), bufferWidth_, bufferHeight_)) {
                backend_->Invalidate();
                backend_->Render(target.rtv.Get(), frame);
                if (SaveTextureToPng(device_, target.texture.Get(), file, captureWidth_, error)) {
                    log::Info(L"Capture enregistree : {}", file.wstring());
                } else {
                    log::Error(L"Capture : {}", error);
                }
            }
            backend_->Invalidate();
        }
    }

    if (!backend_->PresentsToSwapChain()) {
        backend_->Render(nullptr, frame);
        return SurfaceStatus::Presented;
    }

    // Surface entierement masquee : on ne dessine plus, on se contente de sonder periodiquement.
    if (occluded_) {
        if (timing.seconds - lastOcclusionTest_ < kOcclusionTestInterval) {
            return SurfaceStatus::Skipped;
        }
        lastOcclusionTest_ = timing.seconds;
        const HRESULT test = swapChain_->Present(0, DXGI_PRESENT_TEST);
        if (IsDeviceLost(test)) {
            return SurfaceStatus::DeviceLost;
        }
        if (test == DXGI_STATUS_OCCLUDED) {
            return SurfaceStatus::Skipped;
        }
        occluded_ = false;
    }

    // Le GPU/DWM n'a pas encore consomme l'image precedente : plutot que de laisser Present attendre
    // (le pilote fait souvent de l'attente active), on saute cette surface pour cette image.
    // L'attente consomme un jeton : il est conserve tant qu'aucun Present n'a eu lieu.
    if (frameLatencyWaitable_ && !latencyTokenHeld_) {
        if (WaitForSingleObjectEx(frameLatencyWaitable_.get(), 0, FALSE) != WAIT_OBJECT_0) {
            return SurfaceStatus::Skipped;
        }
        latencyTokenHeld_ = true;
    }

    bool finished = false;
    if (outgoing_) {
        // Les deux fonds dessinent dans leur texture (s'ils n'ont rien de neuf, la texture garde leur
        // derniere image), puis le shader de transition compose le resultat dans la swap chain.
        transitionElapsed_ += frame.deltaTime;
        const float progress = std::clamp(transitionElapsed_ / std::max(transition_.seconds, 0.001f), 0.0f, 1.0f);
        outgoing_->Render(fromTexture_.rtv.Get(), frame);
        backend_->Render(toTexture_.rtv.Get(), frame);
        transitionRenderer_->Draw(renderTarget_.Get(), fromTexture_.srv.Get(), toTexture_.srv.Get(), bufferWidth_,
                                  bufferHeight_, progress, transition_.type, transition_.seed);
        finished = progress >= 1.0f;
        ++transitionFrames_;
    } else if (!backend_->Render(renderTarget_.Get(), frame)) {
        return SurfaceStatus::Skipped;  // rien de neuf : l'ecran garde la derniere image, sans Present
    }

    // Intervalle 0 : la cadence est imposee par FrameLimiter ; en flip model fenetre, le DWM compose
    // sans dechirure et un Present multi-ecran ne bloque jamais sur la VBlank d'un autre ecran.
    const HRESULT hr = swapChain_->Present(0, 0);
    latencyTokenHeld_ = false;
    if (finished) {
        EndTransition();
    }
    if (hr == DXGI_STATUS_OCCLUDED) {
        occluded_ = true;
        lastOcclusionTest_ = timing.seconds;
        return SurfaceStatus::Skipped;
    }
    if (IsDeviceLost(hr)) {
        return SurfaceStatus::DeviceLost;
    }
    if (FAILED(hr)) {
        if (!presentErrorLogged_) {
            presentErrorLogged_ = true;
            log::Error(L"Present sur « {} » : {}", info_.friendlyName, HResultToString(hr));
        }
        return SurfaceStatus::Error;
    }
    return SurfaceStatus::Presented;
}

void MonitorContext::Show() {
    if (window_) {
        ShowWindow(window_, SW_SHOWNA);
    }
}

void MonitorContext::SetPaused(bool globalPause, bool autoPause) {
    autoPaused_ = autoPause;
    const bool paused = globalPause || autoPause;
    if (paused == paused_) {
        return;
    }
    paused_ = paused;
    if (paused) {
        EndTransition();  // inutile de terminer un fondu que personne ne voit
    }
    if (backend_) {
        backend_->OnPauseChanged(paused);
        if (paused) {
            backend_->ReleaseNonCriticalResources();
        }
    }
    occluded_ = false;
}

void MonitorContext::RequestCapture(std::filesystem::path file, uint32_t maxWidth) {
    capturePath_ = std::move(file);
    captureWidth_ = maxWidth;
}

void MonitorContext::MaintenanceTick() {
    if (backend_) {
        backend_->MaintenanceTick();
    }
}

}  // namespace lwe
