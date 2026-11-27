#include "wallpaper/web/WebBackend.h"

#include <WebView2EnvironmentOptions.h>
#include <wrl/event.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <fstream>
#include <functional>
#include <iterator>

#include <nlohmann/json.hpp>

#include "core/Log.h"
#include "core/Paths.h"
#include "core/StringUtil.h"
#include "core/Win32Util.h"

namespace lwe {
namespace {

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

constexpr wchar_t kVirtualHost[] = L"wallpaper.example";
constexpr wchar_t kOrigin[] = L"https://wallpaper.example/";

// Coupe tout ce que le navigateur fait de lui-meme sur le reseau (mises a jour de composants,
// synchronisation, rapports, pings) et autorise la lecture audio/video sans geste utilisateur.
constexpr wchar_t kBrowserArguments[] =
    L"--disable-background-networking --disable-component-update --disable-sync --disable-domain-reliability "
    L"--no-pings --disable-breakpad --autoplay-policy=no-user-gesture-required";

constexpr wchar_t kBridgeScript[] = LR"js(
(() => {
  if (window.wallEnginePaper) return;
  const listeners = {};
  const api = {
    mouse: { x: 0, y: 0, nx: 0, ny: 0, inside: false },
    audio: new Float32Array(64),
    audioLevel: 0,
    params: {},
    volume: 1,
    muted: false,
    paused: false,
    on(type, callback) { (listeners[type] = listeners[type] || []).push(callback); },
  };
  Object.defineProperty(window, 'wallEnginePaper', { value: api, writable: false });
  const emit = (type, value) => (listeners[type] || []).forEach(cb => { try { cb(value); } catch (e) { console.error(e); } });
  const applyVolume = () => document.querySelectorAll('audio, video').forEach(m => { m.volume = api.volume; m.muted = api.muted; });
  const fire = (target, type, init, Ctor) => target.dispatchEvent(new Ctor(type, init));
  window.chrome.webview.addEventListener('message', event => {
    const m = event.data;
    if (m.type === 'mouse') {
      Object.assign(api.mouse, { x: m.x, y: m.y, nx: m.nx, ny: m.ny, inside: m.inside });
      const target = document.elementFromPoint(m.x, m.y) || document.body || document.documentElement;
      if (target) {
        const init = { clientX: m.x, clientY: m.y, screenX: m.x, screenY: m.y, pageX: m.x, pageY: m.y,
                       bubbles: true, cancelable: true, view: window, pointerType: 'mouse', isPrimary: true,
                       button: 0, buttons: m.pressed ? 1 : 0 };
        if (m.moved) { fire(target, 'pointermove', init, PointerEvent); fire(target, 'mousemove', init, MouseEvent); }
        if (m.down) { fire(target, 'pointerdown', init, PointerEvent); fire(target, 'mousedown', init, MouseEvent); }
        if (m.up) { fire(target, 'pointerup', init, PointerEvent); fire(target, 'mouseup', init, MouseEvent); fire(target, 'click', init, MouseEvent); }
      }
      emit('mouse', api.mouse);
    } else if (m.type === 'audio') {
      api.audio.set(m.bands); api.audioLevel = m.level; emit('audio', api.audio);
    } else if (m.type === 'params') {
      Object.assign(api.params, m.values); emit('params', api.params);
    } else if (m.type === 'volume') {
      api.volume = m.volume; api.muted = m.muted; applyVolume();
    } else if (m.type === 'pause') {
      api.paused = m.paused; emit('pause', m.paused);
    }
  });
  document.addEventListener('DOMContentLoaded', applyVolume);
})();
)js";

bool IsAllowedUri(const std::wstring& uri) {
    return uri.starts_with(kOrigin) || uri.starts_with(L"data:") || uri.starts_with(L"blob:") ||
           uri.starts_with(L"about:");
}

std::wstring UrlEncodePath(const std::filesystem::path& relative) {
    const std::string utf8 = WideToUtf8(relative.generic_wstring());
    std::wstring encoded;
    for (const unsigned char c : utf8) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || c == '/') {
            encoded += static_cast<wchar_t>(c);
        } else {
            encoded += std::format(L"%{:02X}", static_cast<unsigned>(c));
        }
    }
    return encoded;
}

std::filesystem::path WebViewDataDir() {
    wchar_t local[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH) > 0) {
        return std::filesystem::path(local) / kProductName / L"WebView2";
    }
    return AppDataDir() / L"WebView2";
}

// Un seul environnement WebView2 (donc un seul processus navigateur) partage par tous les fonds web :
// les ecrans demarrent en meme temps sans se concurrencer pour creer le profil.
using EnvironmentCallback = std::function<void(HRESULT, ICoreWebView2Environment*)>;
struct SharedEnvironment {
    ComPtr<ICoreWebView2Environment> environment;
    bool creating = false;
    std::vector<EnvironmentCallback> waiters;
    int liveBackends = 0;
};

SharedEnvironment& Shared() {
    static SharedEnvironment shared;
    return shared;
}

void AcquireEnvironment(EnvironmentCallback callback) {
    SharedEnvironment& shared = Shared();
    if (shared.environment) {
        callback(S_OK, shared.environment.Get());
        return;
    }
    shared.waiters.push_back(std::move(callback));
    if (shared.creating) {
        return;
    }
    shared.creating = true;
    auto options = Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
    options->put_AdditionalBrowserArguments(kBrowserArguments);
    const std::filesystem::path dataDir = WebViewDataDir();
    std::error_code ec;
    std::filesystem::create_directories(dataDir, ec);
    const auto complete = [](HRESULT result, ICoreWebView2Environment* environment) {
        SharedEnvironment& state = Shared();
        state.creating = false;
        if (SUCCEEDED(result) && state.liveBackends > 0) {
            state.environment = environment;
        }
        std::vector<EnvironmentCallback> waiters = std::move(state.waiters);
        state.waiters.clear();
        for (auto& waiter : waiters) {
            waiter(result, environment);
        }
    };
    const HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, dataDir.c_str(), options.Get(),
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [complete](HRESULT result, ICoreWebView2Environment* environment) -> HRESULT {
                complete(result, environment);
                return S_OK;
            })
            .Get());
    if (FAILED(hr)) {
        complete(hr, nullptr);
    }
}

nlohmann::json ParamToJson(const ParamValue& value) {
    if (const auto* f = std::get_if<float>(&value)) {
        return *f;
    }
    if (const auto* b = std::get_if<bool>(&value)) {
        return *b;
    }
    const Color& c = std::get<Color>(value);
    return nlohmann::json::array({c[0], c[1], c[2], c[3]});
}

}  // namespace

bool WebBackend::RuntimeAvailable() {
    static const bool available = [] {
        LPWSTR version = nullptr;
        const HRESULT hr = GetAvailableCoreWebView2BrowserVersionString(nullptr, &version);
        const bool ok = SUCCEEDED(hr) && version != nullptr;
        CoTaskMemFree(version);
        return ok;
    }();
    return available;
}

WebBackend::~WebBackend() {
    *alive_ = false;
    if (controller_) {
        controller_->Close();
    }
    webview_.Reset();
    controller_.Reset();
    environment_.Reset();
    // Dernier fond web detruit : on libere l'environnement (et le processus navigateur) avant la fin de COM.
    if (registered_ && --Shared().liveBackends == 0) {
        Shared().environment.Reset();
    }
}

void WebBackend::Fail(const std::wstring& message) {
    failed_ = true;
    error_ = message;
    log::Error(L"Fond web : {}", message);
}

bool WebBackend::Initialize(const BackendInitContext& context, std::wstring& error) {
    parent_ = context.surfaceWindow;
    entry_ = context.desc.source;
    folder_ = entry_.parent_path();
    volume_ = context.volume;
    muted_ = context.muted;
    if (!parent_) {
        error = L"aucune fenetre hote pour WebView2";
        return false;
    }
    if (!RuntimeAvailable()) {
        error = L"runtime Microsoft Edge WebView2 introuvable";
        return false;
    }
    LoadProperties();
    for (const auto& [name, value] : context.desc.params) {
        SetParameter(name, value);
    }

    ++Shared().liveBackends;
    registered_ = true;
    std::weak_ptr<bool> alive = alive_;
    AcquireEnvironment([this, alive](HRESULT result, ICoreWebView2Environment* environment) {
        if (!alive.expired()) {
            OnEnvironmentCreated(result, environment);
        }
    });
    return true;  // la suite est asynchrone (messages de la boucle principale)
}

HRESULT WebBackend::OnEnvironmentCreated(HRESULT result, ICoreWebView2Environment* environment) {
    if (FAILED(result) || !environment) {
        Fail(L"creation de l'environnement WebView2 : " + HResultToString(result));
        return S_OK;
    }
    environment_ = environment;
    std::weak_ptr<bool> alive = alive_;
    const HRESULT hr = environment_->CreateCoreWebView2Controller(
        parent_, Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                     [this, alive](HRESULT r, ICoreWebView2Controller* controller) -> HRESULT {
                         return alive.expired() ? S_OK : OnControllerCreated(r, controller);
                     })
                     .Get());
    if (FAILED(hr)) {
        Fail(L"creation du controleur WebView2 : " + HResultToString(hr));
    }
    return S_OK;
}

HRESULT WebBackend::OnControllerCreated(HRESULT result, ICoreWebView2Controller* controller) {
    if (FAILED(result) || !controller) {
        Fail(L"creation du controleur WebView2 : " + HResultToString(result));
        return S_OK;
    }
    controller_ = controller;
    controller_->get_CoreWebView2(&webview_);

    ComPtr<ICoreWebView2Controller2> controller2;
    if (SUCCEEDED(controller_.As(&controller2))) {
        controller2->put_DefaultBackgroundColor(COREWEBVIEW2_COLOR{255, 0, 0, 0});
    }
    ComPtr<ICoreWebView2Settings> settings;
    if (SUCCEEDED(webview_->get_Settings(&settings))) {
        settings->put_AreDefaultContextMenusEnabled(FALSE);
        settings->put_AreDevToolsEnabled(FALSE);
        settings->put_IsStatusBarEnabled(FALSE);
        settings->put_IsZoomControlEnabled(FALSE);
        settings->put_AreDefaultScriptDialogsEnabled(FALSE);
        settings->put_IsWebMessageEnabled(TRUE);
        ComPtr<ICoreWebView2Settings3> settings3;
        if (SUCCEEDED(settings.As(&settings3))) {
            settings3->put_AreBrowserAcceleratorKeysEnabled(FALSE);
        }
        ComPtr<ICoreWebView2Settings8> settings8;
        if (SUCCEEDED(settings.As(&settings8))) {
            settings8->put_IsReputationCheckingRequired(FALSE);  // pas de SmartScreen (requete reseau)
        }
    }

    ComPtr<ICoreWebView2_3> webview3;
    if (!webview_ || FAILED(webview_.As(&webview3))) {
        Fail(L"runtime WebView2 trop ancien (ICoreWebView2_3 indisponible)");
        return S_OK;
    }
    if (const HRESULT mapped = webview3->SetVirtualHostNameToFolderMapping(
            kVirtualHost, folder_.c_str(), COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
        FAILED(mapped)) {
        Fail(std::format(L"mappage du dossier local {} impossible : {}", folder_.wstring(), HResultToString(mapped)));
        return S_OK;
    }

    // Pare-feu : seules les ressources du dossier du fond sont servies.
    EventRegistrationToken token{};
    webview_->AddWebResourceRequestedFilter(L"*", COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
    std::weak_ptr<bool> alive = alive_;
    webview_->add_WebResourceRequested(
        Callback<ICoreWebView2WebResourceRequestedEventHandler>(
            [this, alive](ICoreWebView2*, ICoreWebView2WebResourceRequestedEventArgs* args) -> HRESULT {
                if (alive.expired() || !environment_) {
                    return S_OK;
                }
                ComPtr<ICoreWebView2WebResourceRequest> request;
                LPWSTR uri = nullptr;
                if (FAILED(args->get_Request(&request)) || FAILED(request->get_Uri(&uri)) || !uri) {
                    return S_OK;
                }
                const std::wstring requested(uri);
                CoTaskMemFree(uri);
                if (!IsAllowedUri(requested)) {
                    ComPtr<ICoreWebView2WebResourceResponse> response;
                    if (SUCCEEDED(environment_->CreateWebResourceResponse(nullptr, 403, L"Blocked", L"", &response))) {
                        args->put_Response(response.Get());
                    }
                    log::Warn(L"Fond web : requete externe bloquee ({})", requested.substr(0, 120));
                }
                return S_OK;
            })
            .Get(),
        &token);
    webview_->add_NavigationStarting(
        Callback<ICoreWebView2NavigationStartingEventHandler>(
            [](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                LPWSTR uri = nullptr;
                if (SUCCEEDED(args->get_Uri(&uri)) && uri) {
                    if (!IsAllowedUri(uri)) {
                        args->put_Cancel(TRUE);
                    }
                    CoTaskMemFree(uri);
                }
                return S_OK;
            })
            .Get(),
        &token);
    webview_->add_NewWindowRequested(
        Callback<ICoreWebView2NewWindowRequestedEventHandler>(
            [](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                args->put_Handled(TRUE);  // aucune fenetre surgissante
                return S_OK;
            })
            .Get(),
        &token);
    webview_->add_NavigationCompleted(
        Callback<ICoreWebView2NavigationCompletedEventHandler>(
            [this, alive](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                if (alive.expired()) {
                    return S_OK;
                }
                BOOL success = FALSE;
                args->get_IsSuccess(&success);
                if (!success) {
                    COREWEBVIEW2_WEB_ERROR_STATUS status{};
                    args->get_WebErrorStatus(&status);
                    log::Warn(L"Fond web : chargement incomplet (statut {})", static_cast<int>(status));
                }
                ready_ = true;
                SendParams();
                SendVolume();
                Post(std::format(L"{{\"type\":\"pause\",\"paused\":{}}}", paused_));
                return S_OK;
            })
            .Get(),
        &token);

    webview_->AddScriptToExecuteOnDocumentCreated(kBridgeScript, nullptr);
    UpdateBounds();
    SendVolume();
    ApplyVisibility();

    const std::wstring url = std::wstring(kOrigin) + UrlEncodePath(entry_.lexically_relative(folder_));
    webview_->Navigate(url.c_str());
    log::Info(L"Fond web charge : {}", entry_.wstring());
    return S_OK;
}

void WebBackend::LoadProperties() {
    properties_.clear();
    std::ifstream stream(folder_ / L"project.json", std::ios::binary);
    if (!stream) {
        return;
    }
    const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    const auto json = nlohmann::json::parse(text, nullptr, false, true);
    if (json.is_discarded() || !json.contains("properties") || !json["properties"].is_object()) {
        return;
    }
    for (const auto& [name, spec] : json["properties"].items()) {
        if (!spec.is_object()) {
            continue;
        }
        Property property;
        property.desc.name = name;
        property.desc.label = spec.value("label", spec.value("text", name));
        const std::string type = spec.value("type", "slider");
        const auto& raw = spec.contains("value") ? spec["value"] : nlohmann::json();
        if (type == "bool" || type == "checkbox") {
            property.desc.kind = ParamKind::Bool;
            property.desc.defaultValue = raw.is_boolean() ? raw.get<bool>() : false;
        } else if (type == "color") {
            property.desc.kind = ParamKind::Color;
            Color color{1.0f, 1.0f, 1.0f, 1.0f};
            if (raw.is_array()) {
                for (size_t i = 0; i < std::min<size_t>(4, raw.size()); ++i) {
                    if (raw[i].is_number()) {
                        color[i] = raw[i].get<float>();
                    }
                }
            }
            property.desc.defaultValue = color;
        } else {
            property.desc.kind = ParamKind::Float;
            property.desc.defaultValue = raw.is_number() ? raw.get<float>() : 0.0f;
            property.desc.minValue = spec.value("min", 0.0f);
            property.desc.maxValue = spec.value("max", 1.0f);
        }
        property.value = property.desc.defaultValue;
        properties_.push_back(std::move(property));
    }
}

void WebBackend::Post(const std::wstring& json) {
    if (webview_ && ready_) {
        webview_->PostWebMessageAsJson(json.c_str());
    }
}

void WebBackend::SendParams() {
    nlohmann::json values = nlohmann::json::object();
    for (const auto& property : properties_) {
        values[property.desc.name] = ParamToJson(property.value);
    }
    const nlohmann::json message{{"type", "params"}, {"values", values}};
    Post(Utf8ToWide(message.dump()));
}

void WebBackend::SendVolume() {
    if (webview_) {
        ComPtr<ICoreWebView2_8> webview8;
        if (SUCCEEDED(webview_.As(&webview8))) {
            webview8->put_IsMuted(muted_ ? TRUE : FALSE);
        }
    }
    Post(std::format(L"{{\"type\":\"volume\",\"volume\":{:.3f},\"muted\":{}}}", volume_, muted_));
}

void WebBackend::UpdateBounds() {
    if (controller_ && parent_) {
        RECT client{};
        GetClientRect(parent_, &client);
        controller_->put_Bounds(client);
    }
}

void WebBackend::ApplyVisibility() {
    if (!controller_) {
        return;
    }
    controller_->put_IsVisible(paused_ ? FALSE : TRUE);
    ComPtr<ICoreWebView2_3> webview3;
    if (webview_ && SUCCEEDED(webview_.As(&webview3))) {
        if (paused_) {
            // Masquee puis suspendue : scripts et rendu geles, memoire reduite (jeu plein ecran...).
            webview3->TrySuspend(Callback<ICoreWebView2TrySuspendCompletedHandler>(
                                     [](HRESULT, BOOL) -> HRESULT { return S_OK; })
                                     .Get());
        } else {
            webview3->Resume();
        }
    }
}

void WebBackend::Resize(uint32_t, uint32_t) {
    UpdateBounds();
}

bool WebBackend::Render(ID3D11RenderTargetView*, const FrameContext& frame) {
    if (!ready_ || !parent_) {
        return false;
    }
    // Souris (coordonnees CSS) : WebView2 applique la mise a l'echelle DPI de l'ecran.
    RECT client{};
    GetClientRect(parent_, &client);
    const float dpiScale = static_cast<float>(GetDpiForWindow(parent_)) / 96.0f;
    const float x = (frame.mouse.normX + 1.0f) * 0.5f * static_cast<float>(client.right) / dpiScale;
    const float y = (1.0f - frame.mouse.normY) * 0.5f * static_cast<float>(client.bottom) / dpiScale;
    const bool moved = std::fabs(x - lastX_) > 0.25f || std::fabs(y - lastY_) > 0.25f;
    const bool down = frame.mouse.pressed && !lastDown_;
    const bool up = !frame.mouse.pressed && lastDown_;
    if (moved || down || up || frame.mouse.inside != lastInside_) {
        Post(std::format(L"{{\"type\":\"mouse\",\"x\":{:.1f},\"y\":{:.1f},\"nx\":{:.4f},\"ny\":{:.4f},\"inside\":{},"
                         L"\"pressed\":{},\"moved\":{},\"down\":{},\"up\":{}}}",
                         x, y, frame.mouse.normX, frame.mouse.normY, frame.mouse.inside, frame.mouse.pressed, moved,
                         down, up));
        lastX_ = x;
        lastY_ = y;
        lastDown_ = frame.mouse.pressed;
        lastInside_ = frame.mouse.inside;
    }
    // Spectre audio : envoye seulement s'il y a du son (aucun message inutile en silence).
    if (frame.audio && frame.audio->level > 0.001f && frame.frameIndex % 2 == 0) {
        std::wstring bands;
        for (size_t i = 0; i < kAudioBandCount; ++i) {
            bands += std::format(L"{}{:.3f}", i ? L"," : L"", frame.audio->bands[i]);
        }
        Post(std::format(L"{{\"type\":\"audio\",\"level\":{:.3f},\"bands\":[{}]}}", frame.audio->level, bands));
    }
    return false;  // WebView2 compose lui-meme son contenu
}

std::vector<ParamDescriptor> WebBackend::Parameters() const {
    std::vector<ParamDescriptor> result;
    for (const auto& property : properties_) {
        result.push_back(property.desc);
    }
    return result;
}

ParamMap WebBackend::ParameterValues() const {
    ParamMap values;
    for (const auto& property : properties_) {
        values[property.desc.name] = property.value;
    }
    return values;
}

void WebBackend::SetParameter(std::string_view name, const ParamValue& value) {
    for (auto& property : properties_) {
        if (property.desc.name != name) {
            continue;
        }
        if (property.desc.kind == ParamKind::Float && std::holds_alternative<float>(value)) {
            property.value = std::clamp(std::get<float>(value), property.desc.minValue, property.desc.maxValue);
        } else if (property.desc.kind == ParamKind::Bool && std::holds_alternative<bool>(value)) {
            property.value = value;
        } else if (property.desc.kind == ParamKind::Color && std::holds_alternative<Color>(value)) {
            property.value = value;
        }
    }
    SendParams();
}

void WebBackend::OnPauseChanged(bool paused) {
    paused_ = paused;
    Post(std::format(L"{{\"type\":\"pause\",\"paused\":{}}}", paused_));
    ApplyVisibility();
}

void WebBackend::SetAudioOutput(float volume, bool muted) {
    volume_ = volume;
    muted_ = muted;
    SendVolume();
}

bool WebBackend::HasFailed(std::wstring& error) const {
    if (failed_) {
        error = error_;
    }
    return failed_;
}

}  // namespace lwe
