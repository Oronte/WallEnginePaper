#include "ui/ControlPanel.h"

#include <dwmapi.h>
#include <shellscalingapi.h>

#include <algorithm>
#include <cmath>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include "app/AppServices.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/Win32Util.h"
#include "render/RenderDevice.h"
#include "resource.h"
#include "ui/ControlPanelViews.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

namespace lwe {
namespace {

constexpr wchar_t kPanelClass[] = L"WEP_ControlPanel";
constexpr DWORD kDwmUseImmersiveDarkMode = 20;  // DWMWA_USE_IMMERSIVE_DARK_MODE
constexpr DWORD kDwmCaptionColor = 35;          // DWMWA_CAPTION_COLOR (Windows 11)
constexpr int kBaseWidth = 1340;
constexpr int kBaseHeight = 860;
constexpr int kMinWidth = 980;
constexpr int kMinHeight = 640;

bool IsDeviceLost(HRESULT hr) noexcept {
    return hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET || hr == DXGI_ERROR_DEVICE_HUNG;
}

}  // namespace

ControlPanel::ControlPanel(HINSTANCE instance, IAppServices& services) : instance_(instance), services_(services) {}

ControlPanel::~ControlPanel() {
    views_.reset();  // libere les textures avant le backend ImGui
    if (imguiReady_) {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }
    renderTarget_.Reset();
    swapChain_.Reset();
    if (window_) {
        DestroyWindow(window_);
        window_ = nullptr;
    }
}

bool ControlPanel::Create(std::wstring& error) {
    const int bigIcon = GetSystemMetrics(SM_CXICON);
    const int smallIcon = GetSystemMetrics(SM_CXSMICON);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = StaticWindowProc;
    wc.hInstance = instance_;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, bigIcon, bigIcon, 0));
    wc.hIconSm =
        static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, smallIcon, smallIcon, 0));
    wc.lpszClassName = kPanelClass;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        error = L"RegisterClassEx (interface)";
        return false;
    }

    // Taille initiale adaptee a l'ecran ou se trouve le curseur.
    POINT cursor{};
    GetCursorPos(&cursor);
    HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(monitor, &mi);
    UINT dpiX = 96;
    UINT dpiY = 96;
    GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
    const float scale = dpiX / 96.0f;
    const RECT work = mi.rcWork;
    const int workWidth = work.right - work.left;
    const int workHeight = work.bottom - work.top;
    const int width = std::min(static_cast<int>(kBaseWidth * scale), workWidth * 92 / 100);
    const int height = std::min(static_cast<int>(kBaseHeight * scale), workHeight * 92 / 100);

    window_ = CreateWindowExW(WS_EX_APPWINDOW, kPanelClass, kProductName, WS_OVERLAPPEDWINDOW,
                              work.left + (workWidth - width) / 2, work.top + (workHeight - height) / 2, width, height,
                              nullptr, nullptr, instance_, this);
    if (!window_) {
        error = L"CreateWindowEx (interface)";
        return false;
    }
    const BOOL dark = TRUE;
    DwmSetWindowAttribute(window_, kDwmUseImmersiveDarkMode, &dark, sizeof(dark));
    const COLORREF caption = RGB(14, 14, 21);
    DwmSetWindowAttribute(window_, kDwmCaptionColor, &caption, sizeof(caption));

    if (!CreateSwapChain(error)) {
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // aucun fichier imgui.ini : l'etat de l'interface vit dans config.json
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui_ImplWin32_Init(window_);
    ImGui_ImplDX11_Init(services_.Device().Device(), services_.Device().Context());
    imguiReady_ = true;

    views_ = std::make_unique<ControlPanelViews>(services_, window_);
    ApplyDpi();
    log::Info(L"Interface ouverte");
    return true;
}

bool ControlPanel::CreateSwapChain(std::wstring& error) {
    RECT client{};
    GetClientRect(window_, &client);
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = static_cast<UINT>(std::max<LONG>(1, client.right - client.left));
    desc.Height = static_cast<UINT>(std::max<LONG>(1, client.bottom - client.top));
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    RenderDevice& device = services_.Device();
    HRESULT hr = device.Factory()->CreateSwapChainForHwnd(device.Device(), window_, &desc, nullptr, nullptr, &swapChain_);
    if (FAILED(hr)) {
        error = L"Swap chain de l'interface : " + HResultToString(hr);
        return false;
    }
    device.Factory()->MakeWindowAssociation(window_, DXGI_MWA_NO_ALT_ENTER);
    Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
    hr = swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (SUCCEEDED(hr)) {
        hr = device.Device()->CreateRenderTargetView(backBuffer.Get(), nullptr, &renderTarget_);
    }
    if (FAILED(hr)) {
        error = L"Cible de rendu de l'interface : " + HResultToString(hr);
        return false;
    }
    return true;
}

void ControlPanel::ResizeSwapChain() {
    resizePending_ = false;
    RECT client{};
    GetClientRect(window_, &client);
    const UINT width = static_cast<UINT>(client.right - client.left);
    const UINT height = static_cast<UINT>(client.bottom - client.top);
    if (width == 0 || height == 0 || !swapChain_) {
        return;
    }
    renderTarget_.Reset();
    services_.Device().Context()->OMSetRenderTargets(0, nullptr, nullptr);
    if (FAILED(swapChain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0))) {
        return;
    }
    Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
    if (SUCCEEDED(swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuffer)))) {
        services_.Device().Device()->CreateRenderTargetView(backBuffer.Get(), nullptr, &renderTarget_);
    }
}

float ControlPanel::DpiScale() const {
    return window_ ? static_cast<float>(GetDpiForWindow(window_)) / 96.0f : 1.0f;
}

void ControlPanel::ApplyDpi() {
    dpiPending_ = false;
    const float scale = DpiScale();
    fonts_ = ui::BuildFonts(scale);
    ui::ApplyTheme(scale);
    ImGui_ImplDX11_InvalidateDeviceObjects();  // l'atlas de polices est recree au prochain NewFrame
    if (views_) {
        views_->SetStyle(fonts_, scale);
    }
}

void ControlPanel::Show() {
    if (!window_) {
        return;
    }
    closeRequested_ = false;
    ShowWindow(window_, IsIconic(window_) ? SW_RESTORE : SW_SHOWNORMAL);
    // Au premier ShowWindow du processus, Windows peut imposer l'etat du lanceur (STARTUPINFO, raccourci
    // "Reduite"...) : l'utilisateur a demande la fenetre, on la restaure.
    if (IsIconic(window_)) {
        ShowWindow(window_, SW_RESTORE);
    }
    SetForegroundWindow(window_);
}

bool ControlPanel::IsVisible() const {
    return window_ && IsWindowVisible(window_) && !IsIconic(window_);
}

bool ControlPanel::RenderFrame(float deltaSeconds) {
    if (!window_ || !imguiReady_ || inFrame_) {
        return true;
    }
    inFrame_ = true;
    if (dpiPending_) {
        ApplyDpi();
    }
    if (resizePending_) {
        ResizeSwapChain();
    }
    if (!renderTarget_) {
        inFrame_ = false;
        return true;
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    views_->Draw(deltaSeconds);
    ImGui::Render();

    ID3D11DeviceContext* context = services_.Device().Context();
    ID3D11RenderTargetView* target = renderTarget_.Get();
    context->OMSetRenderTargets(1, &target, nullptr);
    const float clear[4] = {ui::color::kBackground.x, ui::color::kBackground.y, ui::color::kBackground.z, 1.0f};
    context->ClearRenderTargetView(target, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    const HRESULT hr = swapChain_->Present(0, 0);
    inFrame_ = false;
    return !IsDeviceLost(hr);
}

LRESULT CALLBACK ControlPanel::StaticWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    if (auto* panel = reinterpret_cast<ControlPanel*>(GetWindowLongPtrW(window, GWLP_USERDATA))) {
        return panel->WindowProc(window, message, wParam, lParam);
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT ControlPanel::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (imguiReady_ && ImGui::GetCurrentContext() && ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam)) {
        return TRUE;
    }
    switch (message) {
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) {
            resizePending_ = true;
            // Pendant un redimensionnement, la boucle principale est bloquee par la boucle modale de
            // Windows : on redessine ici pour garder une interface fluide.
            if (inSizeMove_ && !inFrame_) {
                RenderFrame(1.0f / 60.0f);
            }
        }
        return 0;
    case WM_ENTERSIZEMOVE:
        inSizeMove_ = true;
        return 0;
    case WM_EXITSIZEMOVE:
        inSizeMove_ = false;
        return 0;
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        const float scale = DpiScale();
        info->ptMinTrackSize.x = static_cast<LONG>(kMinWidth * scale);
        info->ptMinTrackSize.y = static_cast<LONG>(kMinHeight * scale);
        return 0;
    }
    case WM_DPICHANGED: {
        const auto* suggested = reinterpret_cast<const RECT*>(lParam);
        SetWindowPos(window, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                     suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
        dpiPending_ = true;
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_CLOSE:
        // L'application continue dans la zone de notification ; la fenetre est detruite par Application.
        closeRequested_ = true;
        ShowWindow(window, SW_HIDE);
        return 0;
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

}  // namespace lwe
