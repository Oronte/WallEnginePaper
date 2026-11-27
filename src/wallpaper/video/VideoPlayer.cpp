#include "wallpaper/video/VideoPlayer.h"

#include <oleauto.h>
#include <wrl/implements.h>

#include <format>
#include <map>

#include "core/Log.h"
#include "core/StringUtil.h"
#include "core/Win32Util.h"
#include "render/RenderDevice.h"

namespace lwe {
namespace {

using Microsoft::WRL::ComPtr;

// Notifications du moteur : appelees sur ses propres threads, on se contente de positionner des atomiques.
class EngineNotify final
    : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
                                          IMFMediaEngineNotify> {
public:
    explicit EngineNotify(std::shared_ptr<VideoPlayer::SharedState> state) : state_(std::move(state)) {}

    STDMETHODIMP EventNotify(DWORD event, DWORD_PTR param1, DWORD param2) override {
        if (event == MF_MEDIA_ENGINE_EVENT_ERROR) {
            state_->errorCode = static_cast<uint32_t>(param1);
            state_->errorResult = static_cast<HRESULT>(param2);
            state_->failed = true;
        }
        return S_OK;
    }

private:
    std::shared_ptr<VideoPlayer::SharedState> state_;
};

std::map<std::wstring, std::weak_ptr<VideoPlayer>>& Registry() {
    static std::map<std::wstring, std::weak_ptr<VideoPlayer>> registry;
    return registry;
}

std::wstring RegistryKey(const RenderDevice& device, const std::filesystem::path& file) {
    return std::format(L"{:x}|{}", reinterpret_cast<uintptr_t>(device.Device()), ToLower(file.lexically_normal().wstring()));
}

}  // namespace

std::shared_ptr<VideoPlayer> VideoPlayer::Acquire(RenderDevice& device, const std::filesystem::path& file,
                                                  std::wstring& error) {
    const std::wstring key = RegistryKey(device, file);
    auto& registry = Registry();
    if (const auto it = registry.find(key); it != registry.end()) {
        if (auto existing = it->second.lock()) {
            return existing;
        }
    }
    std::error_code ec;
    if (!std::filesystem::exists(file, ec)) {
        error = L"fichier video introuvable : " + file.wstring();
        return nullptr;
    }
    std::shared_ptr<VideoPlayer> player(new VideoPlayer(device, file));
    if (!player->CreateEngine(error)) {
        return nullptr;
    }
    registry[key] = player;
    return player;
}

VideoPlayer::VideoPlayer(RenderDevice& device, std::filesystem::path file) : device_(device), file_(std::move(file)) {}

VideoPlayer::~VideoPlayer() {
    DestroyEngine();
    auto& registry = Registry();
    for (auto it = registry.begin(); it != registry.end();) {
        it = it->second.expired() ? registry.erase(it) : std::next(it);
    }
}

bool VideoPlayer::CreateEngine(std::wstring& error) {
    ComPtr<IMFMediaEngineClassFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_MFMediaEngineClassFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    UINT resetToken = 0;
    if (SUCCEEDED(hr)) {
        hr = MFCreateDXGIDeviceManager(&resetToken, &deviceManager_);
    }
    if (SUCCEEDED(hr)) {
        hr = deviceManager_->ResetDevice(device_.Device(), resetToken);
    }
    ComPtr<IMFAttributes> attributes;
    if (SUCCEEDED(hr)) {
        hr = MFCreateAttributes(&attributes, 3);
    }
    if (SUCCEEDED(hr)) {
        auto notify = Microsoft::WRL::Make<EngineNotify>(state_);
        attributes->SetUnknown(MF_MEDIA_ENGINE_DXGI_MANAGER, deviceManager_.Get());
        attributes->SetUnknown(MF_MEDIA_ENGINE_CALLBACK, notify.Get());
        attributes->SetUINT32(MF_MEDIA_ENGINE_VIDEO_OUTPUT_FORMAT, DXGI_FORMAT_B8G8R8A8_UNORM);
        hr = factory->CreateInstance(0, attributes.Get(), &engine_);
    }
    if (FAILED(hr)) {
        error = L"Media Foundation : " + HResultToString(hr);
        DestroyEngine();
        return false;
    }

    engine_->SetLoop(TRUE);
    engine_->SetAutoPlay(FALSE);
    engine_->SetVolume(volume_);
    engine_->SetMuted(muted_ ? TRUE : FALSE);
    engine_->SetDefaultPlaybackRate(rate_);
    engine_->SetPlaybackRate(rate_);
    BSTR url = SysAllocString(file_.c_str());
    hr = engine_->SetSource(url);
    SysFreeString(url);
    if (FAILED(hr)) {
        error = L"ouverture de la video : " + HResultToString(hr);
        DestroyEngine();
        return false;
    }
    playing_ = false;
    ApplyPlayState();
    log::Info(L"Video ouverte (decodage materiel) : {}", file_.wstring());
    return true;
}

void VideoPlayer::DestroyEngine() {
    if (engine_) {
        engine_->Shutdown();
        engine_.Reset();
    }
    deviceManager_.Reset();
    playing_ = false;
}

void VideoPlayer::ApplyPlayState() {
    if (!engine_) {
        return;
    }
    const bool shouldPlay = !activeUsers_.empty();
    if (shouldPlay && !playing_) {
        engine_->Play();
    } else if (!shouldPlay && playing_) {
        engine_->Pause();
    }
    playing_ = shouldPlay;
}

void VideoPlayer::SetActive(const void* user, bool active) {
    if (active) {
        activeUsers_.insert(user);
    } else {
        activeUsers_.erase(user);
    }
    if (active && !engine_ && !state_->failed) {
        // Reprise apres Suspend() : nouveau moteur, retour a la position memorisee.
        std::wstring error;
        if (!CreateEngine(error)) {
            log::Error(L"Reprise video : {}", error);
            return;
        }
    }
    ApplyPlayState();
}

void VideoPlayer::Suspend() {
    if (!engine_ || !activeUsers_.empty()) {
        return;
    }
    resumeTime_ = engine_->GetCurrentTime();
    DestroyEngine();
    log::Info(L"Video suspendue (decodeur libere) : {}", file_.filename().wstring());
}

void VideoPlayer::SetAudio(float volume, bool muted) {
    volume_ = volume;
    muted_ = muted;
    if (engine_) {
        engine_->SetVolume(volume_);
        engine_->SetMuted(muted_ ? TRUE : FALSE);
    }
}

void VideoPlayer::SetPlaybackRate(double rate) {
    rate_ = rate;
    if (engine_) {
        engine_->SetDefaultPlaybackRate(rate_);
        engine_->SetPlaybackRate(rate_);
    }
}

bool VideoPlayer::Failed(std::wstring& error) const {
    if (!state_->failed) {
        return false;
    }
    switch (state_->errorCode.load()) {
    case MF_MEDIA_ENGINE_ERR_SRC_NOT_SUPPORTED:
        error = L"format video non pris en charge (codec manquant ?)";
        break;
    case MF_MEDIA_ENGINE_ERR_DECODE:
        error = L"erreur de decodage video";
        break;
    default:
        error = L"lecture video impossible : " + HResultToString(state_->errorResult.load());
        break;
    }
    return true;
}

void VideoPlayer::Update() {
    if (!engine_) {
        return;
    }
    if (resumeTime_ >= 0.0 && engine_->GetReadyState() >= MF_MEDIA_ENGINE_READY_HAVE_METADATA) {
        engine_->SetCurrentTime(resumeTime_);
        resumeTime_ = -1.0;
    }
    LONGLONG presentationTime = 0;
    if (engine_->OnVideoStreamTick(&presentationTime) != S_OK) {
        return;  // pas de nouvelle trame
    }
    DWORD width = 0;
    DWORD height = 0;
    if (FAILED(engine_->GetNativeVideoSize(&width, &height)) || width == 0 || height == 0) {
        return;
    }
    if (!frameTexture_ || width != width_ || height != height_) {
        frameView_.Reset();
        frameTexture_.Reset();
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(device_.Device()->CreateTexture2D(&desc, nullptr, &frameTexture_)) ||
            FAILED(device_.Device()->CreateShaderResourceView(frameTexture_.Get(), nullptr, &frameView_))) {
            frameTexture_.Reset();
            frameView_.Reset();
            return;
        }
        width_ = width;
        height_ = height;
        log::Info(L"Video {} : {}x{}", file_.filename().wstring(), width_, height_);
    }
    const MFVideoNormalizedRect source{0.0f, 0.0f, 1.0f, 1.0f};
    const RECT destination{0, 0, static_cast<LONG>(width_), static_cast<LONG>(height_)};
    const MFARGB border{0, 0, 0, 255};
    if (SUCCEEDED(engine_->TransferVideoFrame(frameTexture_.Get(), &source, &destination, &border))) {
        ++serial_;
    }
}

}  // namespace lwe
