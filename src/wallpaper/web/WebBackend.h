#pragma once

#include <windows.h>
#include <wrl/client.h>

#include <WebView2.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "wallpaper/IWallpaperBackend.h"

namespace lwe {

// Fond web (HTML / CSS / JS / WebGL) affiche par Microsoft WebView2 dans la fenetre de la surface.
//
// 100 % local : le dossier du fond est servi sous https://wallpaper.example/ (domaine virtuel, aucune
// requete reseau) ; toute autre requete ou navigation est bloquee, les services d'arriere-plan du
// navigateur sont desactives.
//
// API exposee a la page : window.wallEnginePaper
//   .mouse { x, y, nx, ny, inside }   .audio (Float32Array(64))   .audioLevel   .audioBands { bass, mid, treble }
//   .bpm   .beat   .params {...}   .volume  .paused
//   .on('mouse' | 'audio' | 'beat' | 'params' | 'pause', callback)
// Les icones du bureau captant la souris, les mouvements et clics sont aussi rejoues dans la page sous
// forme d'evenements pointermove / mousemove / mousedown / mouseup / click.
//
// Reglages : section "properties" du project.json, par ex.
//   "properties": { "count": { "type": "slider", "label": "Particules", "value": 300, "min": 50, "max": 1000 },
//                   "color": { "type": "color", "value": [0.5, 0.3, 1.0] }, "trails": { "type": "bool", "value": true } }
class WebBackend final : public IWallpaperBackend {
public:
    ~WebBackend() override;

    WallpaperType Type() const noexcept override { return WallpaperType::Web; }

    bool Initialize(const BackendInitContext& context, std::wstring& error) override;
    void Resize(uint32_t width, uint32_t height) override;
    bool Render(ID3D11RenderTargetView* target, const FrameContext& frame) override;
    bool PresentsToSwapChain() const noexcept override { return false; }

    std::vector<ParamDescriptor> Parameters() const override;
    ParamMap ParameterValues() const override;
    void SetParameter(std::string_view name, const ParamValue& value) override;

    void OnPauseChanged(bool paused) override;
    void SetAudioOutput(float volume, bool muted) override;
    bool HasFailed(std::wstring& error) const override;

    static bool RuntimeAvailable();

private:
    struct Property {
        ParamDescriptor desc;
        ParamValue value;
    };

    void LoadProperties();
    HRESULT OnEnvironmentCreated(HRESULT result, ICoreWebView2Environment* environment);
    HRESULT OnControllerCreated(HRESULT result, ICoreWebView2Controller* controller);
    void Fail(const std::wstring& message);
    void Post(const std::wstring& json);
    void SendParams();
    void SendVolume();
    void ApplyVisibility();
    void UpdateBounds();

    HWND parent_ = nullptr;
    std::filesystem::path folder_;
    std::filesystem::path entry_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);

    Microsoft::WRL::ComPtr<ICoreWebView2Environment> environment_;
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> controller_;
    Microsoft::WRL::ComPtr<ICoreWebView2> webview_;

    std::vector<Property> properties_;
    bool registered_ = false;  // compte dans l'environnement WebView2 partage
    bool ready_ = false;
    bool failed_ = false;
    std::wstring error_;
    bool paused_ = false;
    float volume_ = 0.5f;
    bool muted_ = false;

    float lastX_ = -1.0f;
    float lastY_ = -1.0f;
    bool lastDown_ = false;
    bool lastInside_ = false;
    uint32_t lastBeatCount_ = UINT32_MAX;
};

}  // namespace lwe
