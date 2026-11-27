#include "desktop/DesktopHost.h"

#include <shobjidl.h>
#include <wrl/client.h>

#include <atomic>
#include <cwchar>
#include <iterator>
#include <string>

#include "core/Log.h"
#include "core/StringUtil.h"
#include "core/Win32Util.h"

namespace lwe {
namespace {

// Message non documente de Progman : cree la couche WorkerW utilisee pour les transitions de papier peint.
constexpr UINT kSpawnWorkerMessage = 0x052C;
constexpr int kLocateAttempts = 20;
constexpr DWORD kLocateRetryDelayMs = 50;

std::atomic<bool> g_legacyLayout{false};
std::atomic<HWND> g_legacyWorker{nullptr};

bool ClassNameIs(HWND window, const wchar_t* expected) noexcept {
    wchar_t name[64] = {};
    if (GetClassNameW(window, name, static_cast<int>(std::size(name))) == 0) {
        return false;
    }
    return std::wcscmp(name, expected) == 0;
}

struct LegacySearch {
    HWND iconHost = nullptr;
    HWND wallpaperWorker = nullptr;
};

BOOL CALLBACK FindLegacyWorker(HWND top, LPARAM param) {
    auto* search = reinterpret_cast<LegacySearch*>(param);
    if (FindWindowExW(top, nullptr, L"SHELLDLL_DefView", nullptr)) {
        search->iconHost = top;
        // Le WorkerW du papier peint est le frere situe juste apres la fenetre qui porte les icones.
        search->wallpaperWorker = FindWindowExW(nullptr, top, L"WorkerW", nullptr);
        return FALSE;
    }
    return TRUE;
}

// Diaporama ou Windows a la une : reappliquer l'image courante casserait ces modes.
bool WallpaperIsManagedByWindows(const std::wstring& currentPath) {
    const std::wstring lower = ToLower(currentPath);
    if (lower.find(L"irisservice") != std::wstring::npos || lower.find(L"spotlight") != std::wstring::npos) {
        return true;
    }
    Microsoft::WRL::ComPtr<IDesktopWallpaper> wallpaper;
    if (FAILED(CoCreateInstance(__uuidof(DesktopWallpaper), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&wallpaper)))) {
        return false;
    }
    DESKTOP_SLIDESHOW_STATE state{};
    return SUCCEEDED(wallpaper->GetStatus(&state)) && (state & DSS_SLIDESHOW) != 0;
}

// En mode Legacy, le WorkerW conserve la derniere image dessinee par nos surfaces : on demande a
// l'Explorateur de redessiner le papier peint en reappliquant celui qui est deja configure.
void ReapplySystemWallpaper() {
    wchar_t current[MAX_PATH * 2] = {};
    if (!SystemParametersInfoW(SPI_GETDESKWALLPAPER, static_cast<UINT>(std::size(current)), current, 0)) {
        return;
    }
    if (current[0] != L'\0' && WallpaperIsManagedByWindows(current)) {
        return;
    }
    SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, current, SPIF_SENDCHANGE);
}

}  // namespace

bool DesktopHost::Attach() {
    progman_ = defView_ = workerw_ = parent_ = nullptr;
    layout_ = DesktopLayout::Unknown;

    progman_ = FindWindowW(L"Progman", nullptr);
    if (!progman_) {
        log::Error(L"Progman introuvable : l'Explorateur Windows est-il demarre ?");
        return false;
    }

    for (int attempt = 0; attempt < kLocateAttempts; ++attempt) {
        DWORD_PTR result = 0;
        // Variante 24H2 (0xD, 1) puis variante historique (0, 0) : les deux sont idempotentes.
        SendMessageTimeoutW(progman_, kSpawnWorkerMessage, 0xD, 0x1, SMTO_NORMAL, 1000, &result);
        SendMessageTimeoutW(progman_, kSpawnWorkerMessage, 0, 0, SMTO_NORMAL, 1000, &result);
        if (Locate(false)) {
            break;
        }
        Sleep(kLocateRetryDelayMs);
    }
    if (!parent_ && !Locate(true)) {
        log::Error(L"Couche WorkerW du bureau introuvable");
        return false;
    }

    g_legacyLayout = layout_ == DesktopLayout::Legacy;
    g_legacyWorker = layout_ == DesktopLayout::Legacy ? workerw_ : nullptr;

    log::Info(L"Bureau accroche : mode {} (Progman={:#x}, DefView={:#x}, WorkerW={:#x})",
              layout_ == DesktopLayout::Raised ? L"24H2+ (WorkerW enfant de Progman)" : L"classique (WorkerW separe)",
              HwndValue(progman_), HwndValue(defView_), HwndValue(workerw_));
    return true;
}

bool DesktopHost::Locate(bool allowProgmanFallback) {
    defView_ = FindWindowExW(progman_, nullptr, L"SHELLDLL_DefView", nullptr);
    if (defView_) {
        // Icones toujours dans Progman : soit disposition 24H2 (WorkerW enfant de Progman), soit
        // ancienne disposition dont le WorkerW n'est pas encore cree (on reessaie).
        HWND worker = FindWindowExW(progman_, nullptr, L"WorkerW", nullptr);
        if (worker || allowProgmanFallback) {
            layout_ = DesktopLayout::Raised;
            parent_ = progman_;
            workerw_ = worker;
            return true;
        }
        return false;
    }

    LegacySearch search;
    EnumWindows(FindLegacyWorker, reinterpret_cast<LPARAM>(&search));
    if (search.wallpaperWorker) {
        layout_ = DesktopLayout::Legacy;
        parent_ = search.wallpaperWorker;
        workerw_ = search.wallpaperWorker;
        defView_ = FindWindowExW(search.iconHost, nullptr, L"SHELLDLL_DefView", nullptr);
        return true;
    }
    return false;
}

bool DesktopHost::IsAttached() const noexcept {
    return parent_ && IsWindow(parent_) && progman_ && IsWindow(progman_);
}

RECT DesktopHost::ToParentCoordinates(const RECT& screenRect) const {
    RECT rect = screenRect;
    MapWindowPoints(HWND_DESKTOP, parent_, reinterpret_cast<POINT*>(&rect), 2);
    return rect;
}

HWND DesktopHost::CreateSurfaceWindow(const RECT& screenRect, const wchar_t* className, HINSTANCE instance) const {
    if (!IsAttached()) {
        return nullptr;
    }
    const RECT rect = ToParentCoordinates(screenRect);
    const bool raised = layout_ == DesktopLayout::Raised;

    // 24H2 : Progman est compose par DirectComposition ; ses enfants doivent etre "layered"
    // (opacite 255) pour etre pris en compte par le compositeur.
    DWORD exStyle = WS_EX_NOACTIVATE;
    if (raised) {
        exStyle |= WS_EX_LAYERED;
    }

    HWND window = CreateWindowExW(exStyle, className, L"WallEnginePaper",
                                  WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, rect.left, rect.top,
                                  rect.right - rect.left, rect.bottom - rect.top, parent_, nullptr, instance, nullptr);
    if (!window) {
        log::Error(L"CreateWindowEx (surface) a echoue : erreur {}", GetLastError());
        return nullptr;
    }
    if (raised) {
        SetLayeredWindowAttributes(window, 0, 255, LWA_ALPHA);
        // Juste sous les icones...
        SetWindowPos(window, defView_, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        // ...et au-dessus du WorkerW qui peint le papier peint statique.
        if (workerw_) {
            SetWindowPos(workerw_, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
    }
    return window;
}

void DesktopHost::RestoreDesktop() const {
    if (layout_ == DesktopLayout::Legacy) {
        if (workerw_ && IsWindow(workerw_)) {
            RedrawWindow(workerw_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
        }
        ReapplySystemWallpaper();
    } else if (progman_ && IsWindow(progman_)) {
        RedrawWindow(progman_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    }
    log::Info(L"Bureau Windows restaure");
}

void DesktopHost::EmergencyRestore() noexcept {
    if (!g_legacyLayout) {
        // 24H2+ : nos fenetres meurent avec le processus et Progman se recompose seul.
        return;
    }
    if (HWND worker = g_legacyWorker; worker && IsWindow(worker)) {
        RedrawWindow(worker, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    }
    ReapplySystemWallpaper();
}

bool DesktopHost::IsLayered(HWND window) noexcept {
    return (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_LAYERED) != 0;
}

void DesktopHost::SetLayered(HWND window, bool layered) noexcept {
    LONG_PTR style = GetWindowLongPtrW(window, GWL_EXSTYLE);
    style = layered ? (style | WS_EX_LAYERED) : (style & ~static_cast<LONG_PTR>(WS_EX_LAYERED));
    SetWindowLongPtrW(window, GWL_EXSTYLE, style);
    if (layered) {
        SetLayeredWindowAttributes(window, 0, 255, LWA_ALPHA);
    }
}

bool DesktopHost::IsShellDesktopWindow(HWND root) noexcept {
    return root && (ClassNameIs(root, L"Progman") || ClassNameIs(root, L"WorkerW"));
}

}  // namespace lwe
