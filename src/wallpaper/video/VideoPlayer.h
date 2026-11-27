#pragma once

#include <d3d11.h>
#include <mfapi.h>
#include <mfmediaengine.h>
#include <wrl/client.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <set>
#include <string>

namespace lwe {

class RenderDevice;

// Lecteur video partage : IMFMediaEngine en mode "frame server", decodage materiel (D3D11VA via le
// gestionnaire DXGI), trames copiees par le GPU dans une texture D3D11 (TransferVideoFrame).
// Un meme fichier affiche sur plusieurs ecrans (ou dans l'apercu) n'est decode qu'une fois et son
// audio n'est joue qu'une fois. La lecture tourne tant qu'au moins un utilisateur est actif.
class VideoPlayer {
public:
    static std::shared_ptr<VideoPlayer> Acquire(RenderDevice& device, const std::filesystem::path& file,
                                                std::wstring& error);
    ~VideoPlayer();
    VideoPlayer(const VideoPlayer&) = delete;
    VideoPlayer& operator=(const VideoPlayer&) = delete;

    // Recupere la trame la plus recente si elle est nouvelle (a appeler a chaque image).
    void Update();
    uint64_t FrameSerial() const noexcept { return serial_; }
    ID3D11ShaderResourceView* Texture() const noexcept { return frameView_.Get(); }
    uint32_t Width() const noexcept { return width_; }
    uint32_t Height() const noexcept { return height_; }

    void SetActive(const void* user, bool active);
    void SetAudio(float volume, bool muted);
    void SetPlaybackRate(double rate);
    // Plus aucun utilisateur actif : libere le decodeur (la derniere trame reste affichee).
    void Suspend();
    bool Failed(std::wstring& error) const;

    struct SharedState {
        std::atomic<bool> failed{false};
        std::atomic<uint32_t> errorCode{0};
        std::atomic<HRESULT> errorResult{S_OK};
    };

private:
    VideoPlayer(RenderDevice& device, std::filesystem::path file);
    bool CreateEngine(std::wstring& error);
    void DestroyEngine();
    void ApplyPlayState();

    RenderDevice& device_;
    std::filesystem::path file_;
    std::shared_ptr<SharedState> state_ = std::make_shared<SharedState>();
    Microsoft::WRL::ComPtr<IMFDXGIDeviceManager> deviceManager_;
    Microsoft::WRL::ComPtr<IMFMediaEngine> engine_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> frameTexture_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> frameView_;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint64_t serial_ = 0;

    std::set<const void*> activeUsers_;
    float volume_ = 0.5f;
    bool muted_ = false;
    double rate_ = 1.0;
    double resumeTime_ = -1.0;
    bool playing_ = false;
};

}  // namespace lwe
