#pragma once

#include <windows.h>

namespace lwe {

enum class DesktopLayout {
    Unknown,
    // Windows 10 -> 11 23H2 : apres 0x052C, les icones (SHELLDLL_DefView) migrent dans un WorkerW
    // de premier niveau et un second WorkerW, juste derriere, recoit le papier peint.
    Legacy,
    // Windows 11 24H2+ ("raised desktop") : SHELLDLL_DefView et le WorkerW du papier peint sont
    // tous deux enfants de Progman. Nos surfaces s'inserent entre les deux dans l'ordre Z.
    Raised,
};

// Accroche des fenetres de rendu derriere les icones du bureau et restaure le bureau a la fin.
class DesktopHost {
public:
    // Localise (ou fait creer par l'Explorateur) la couche du papier peint. Idempotent.
    bool Attach();
    bool IsAttached() const noexcept;

    DesktopLayout Layout() const noexcept { return layout_; }
    HWND Parent() const noexcept { return parent_; }

    // Cree une fenetre enfant couvrant `screenRect` (coordonnees ecran physiques) dans la couche du
    // papier peint, correctement placee dans l'ordre Z. Retourne nullptr en cas d'echec.
    HWND CreateSurfaceWindow(const RECT& screenRect, const wchar_t* className, HINSTANCE instance) const;

    // Force l'Explorateur a repeindre le papier peint natif (aucun residu noir/fige apres nous).
    // A appeler une fois toutes les surfaces detruites.
    void RestoreDesktop() const;

    // Version minimale, sans etat, utilisable depuis un filtre d'exception non geree.
    static void EmergencyRestore() noexcept;

    static bool IsLayered(HWND window) noexcept;
    static void SetLayered(HWND window, bool layered) noexcept;

    // Vrai si `root` est une fenetre racine du bureau (Progman / WorkerW).
    static bool IsShellDesktopWindow(HWND root) noexcept;

private:
    bool Locate(bool allowProgmanFallback);
    RECT ToParentCoordinates(const RECT& screenRect) const;

    HWND progman_ = nullptr;
    HWND defView_ = nullptr;  // SHELLDLL_DefView : la vue qui contient les icones
    HWND workerw_ = nullptr;  // WorkerW du papier peint
    HWND parent_ = nullptr;   // parent effectif de nos surfaces
    DesktopLayout layout_ = DesktopLayout::Unknown;
};

}  // namespace lwe
