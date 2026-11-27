#pragma once

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <memory>
#include <string>

#include "ui/UiStyle.h"

namespace lwe {

class IAppServices;
class ControlPanelViews;

// Fenetre de configuration WallEnginePaper : fenetre Win32 classique (barre de titre sombre), swap chain
// flip dediee sur le device partage, et Dear ImGui pour le contenu.
// Creee a l'ouverture et detruite a la fermeture : aucune memoire ni GPU consommes tant qu'elle est fermee.
class ControlPanel {
public:
    ControlPanel(HINSTANCE instance, IAppServices& services);
    ~ControlPanel();
    ControlPanel(const ControlPanel&) = delete;
    ControlPanel& operator=(const ControlPanel&) = delete;

    bool Create(std::wstring& error);
    void Show();
    bool IsVisible() const;
    bool CloseRequested() const noexcept { return closeRequested_; }
    HWND Window() const noexcept { return window_; }

    // Construit et presente une image de l'interface. Retourne false si le GPU a ete perdu.
    bool RenderFrame(float deltaSeconds);

private:
    static LRESULT CALLBACK StaticWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

    bool CreateSwapChain(std::wstring& error);
    void ResizeSwapChain();
    void ApplyDpi();
    float DpiScale() const;

    HINSTANCE instance_;
    IAppServices& services_;
    HWND window_ = nullptr;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> renderTarget_;
    std::unique_ptr<ControlPanelViews> views_;
    ui::Fonts fonts_;
    bool imguiReady_ = false;

    bool closeRequested_ = false;
    bool resizePending_ = false;
    bool dpiPending_ = false;
    bool inFrame_ = false;
    bool inSizeMove_ = false;
};

}  // namespace lwe
