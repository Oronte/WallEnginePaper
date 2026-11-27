#pragma once

#include <windows.h>

namespace lwe {

struct GlobalPointerState {
    POINT cursor{};            // pixels physiques, coordonnees de l'ecran virtuel
    bool valid = false;        // faux sur le bureau securise (UAC, Ctrl+Alt+Suppr)
    bool primaryDown = false;  // bouton principal (tient compte de l'inversion gauche/droite)
    bool overDesktop = false;  // le clic vise le bureau, pas une autre application
};

// Les icones du bureau (SHELLDLL_DefView) recoivent tous les evenements souris : nos surfaces n'en
// voient aucun. On echantillonne donc l'etat global du pointeur une fois par image, sans hook
// ni capture, ce qui fonctionne meme quand le bureau n'a pas le focus.
class InputForwarder {
public:
    void Update();
    const GlobalPointerState& State() const noexcept { return state_; }

private:
    GlobalPointerState state_;
};

}  // namespace lwe
