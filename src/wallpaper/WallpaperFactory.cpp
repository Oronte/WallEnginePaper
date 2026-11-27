#include "wallpaper/WallpaperFactory.h"

#include <format>

#include "core/StringUtil.h"
#include "wallpaper/scene/SceneBackend.h"
#include "wallpaper/shader/ShaderBackend.h"
#include "wallpaper/video/VideoBackend.h"
#if defined(LWE_HAS_WEBVIEW2)
#include "wallpaper/web/WebBackend.h"
#endif

namespace lwe {

bool IsBackendAvailable(WallpaperType type) noexcept {
    switch (type) {
    case WallpaperType::Shader:
    case WallpaperType::Scene:
    case WallpaperType::Video:
        return true;
    case WallpaperType::Web:
#if defined(LWE_HAS_WEBVIEW2)
        return WebBackend::RuntimeAvailable();
#else
        return false;
#endif
    }
    return false;
}

std::unique_ptr<IWallpaperBackend> CreateWallpaperBackend(WallpaperType type, std::wstring& error) {
    switch (type) {
    case WallpaperType::Shader:
        return std::make_unique<ShaderBackend>();
    case WallpaperType::Scene:
        return std::make_unique<SceneBackend>();
    case WallpaperType::Video:
        return std::make_unique<VideoBackend>();
    case WallpaperType::Web:
#if defined(LWE_HAS_WEBVIEW2)
        if (WebBackend::RuntimeAvailable()) {
            return std::make_unique<WebBackend>();
        }
        error = L"le runtime Microsoft Edge WebView2 n'est pas installé";
        return nullptr;
#else
        break;
#endif
    }
    error = std::format(L"le type de fond « {} » n'est pas disponible dans cette version",
                        Utf8ToWide(ToString(type)));
    return nullptr;
}

}  // namespace lwe
