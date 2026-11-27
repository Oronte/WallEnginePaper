#pragma once

#include <windows.h>
#include <d3d11.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "config/AppConfig.h"

namespace lwe {

class RenderDevice;

inline constexpr size_t kAudioBandCount = 64;

// Spectre audio normalise [0, 1] (64 bandes logarithmiques) + niveau global + analyse rythmique.
struct AudioSpectrum {
    std::array<float, kAudioBandCount> bands{};
    float level = 0.0f;
    // Zones de frequences (~20-250 Hz, 250 Hz-4 kHz, 4-16 kHz) lissees (attaque / relachement reglables).
    float bass = 0.0f;
    float mid = 0.0f;
    float treble = 0.0f;
    float beat = 0.0f;       // 1 au beat, decroit jusqu'au suivant
    float bpm = 0.0f;        // tempo estime (0 = inconnu)
    float beatTime = 1e6f;   // secondes depuis le dernier beat
    uint32_t beatCount = 0;  // incremente a chaque beat (detection de front)
};

// Etat du pointeur vu depuis une surface donnee (calcule par MonitorContext a partir de InputForwarder).
struct MouseState {
    float x = 0.0f;  // pixels du rendu, origine en bas a gauche (convention Shadertoy)
    float y = 0.0f;
    float clickX = 0.0f;  // position du dernier clic gauche sur le bureau
    float clickY = 0.0f;
    bool inside = false;   // curseur sur cet ecran
    bool pressed = false;  // bouton principal enfonce sur le bureau (pas sur une autre fenetre)
    float normX = 0.0f;    // [-1, 1], 0 = centre de l'ecran, y vers le haut
    float normY = 0.0f;
    float parallaxX = 0.0f;  // normX/normY lisses (inertie) pour la parallaxe
    float parallaxY = 0.0f;
};

struct FrameContext {
    double time = 0.0;  // secondes depuis le demarrage (horloge murale, hors pause)
    float deltaTime = 0.0f;
    uint64_t frameIndex = 0;
    float frameRate = 60.0f;  // FPS cible
    uint32_t width = 0;       // taille du rendu (pixels du back buffer)
    uint32_t height = 0;
    MouseState mouse;
    const AudioSpectrum* audio = nullptr;
};

enum class ParamKind : uint8_t { Float, Bool, Color, Choice };

// Parametre expose a l'interface (slider, case a cocher, selecteur de couleur).
struct ParamDescriptor {
    std::string name;
    std::string label;
    ParamKind kind = ParamKind::Float;
    ParamValue defaultValue = 0.0f;
    float minValue = 0.0f;
    float maxValue = 1.0f;
    std::vector<std::string> options;  // ParamKind::Choice : libelles, valeur = index (float)
};

struct BackendInitContext {
    RenderDevice& device;
    HWND surfaceWindow;  // fenetre enfant du bureau (WebView2 s'y attache)
    uint32_t width;
    uint32_t height;
    const WallpaperDesc& desc;
    float volume = 0.5f;  // sortie audio des fonds video / web
    bool muted = false;
};

// Contrat commun a tous les types de fonds d'ecran. Un backend appartient a une seule surface
// (un ecran, ou l'ensemble des ecrans en mode Span) et vit sur le thread UI/rendu.
class IWallpaperBackend {
public:
    virtual ~IWallpaperBackend() = default;

    virtual WallpaperType Type() const noexcept = 0;

    virtual bool Initialize(const BackendInitContext& context, std::wstring& error) = 0;
    virtual void Resize(uint32_t width, uint32_t height) = 0;

    // Dessine une image dans `target` (back buffer de la swap chain de la surface).
    // Retourne false si rien n'a change depuis l'image precedente (image fixe, video sans nouvelle
    // trame...) : la surface saute alors le Present et l'ecran garde la derniere image, sans aucun cout.
    // Le premier appel apres Initialize/Resize doit toujours dessiner.
    virtual bool Render(ID3D11RenderTargetView* target, const FrameContext& frame) = 0;

    // false : le backend affiche lui-meme son contenu (ex. WebView2) ; la surface ne fait pas de Present.
    virtual bool PresentsToSwapChain() const noexcept { return true; }

    virtual std::vector<ParamDescriptor> Parameters() const = 0;
    virtual ParamMap ParameterValues() const = 0;
    virtual void SetParameter(std::string_view name, const ParamValue& value) = 0;

    virtual void OnPauseChanged(bool /*paused*/) {}
    virtual void ReleaseNonCriticalResources() {}

    // Appele ~1 fois par seconde hors pause : rechargement a chaud, taches de fond legeres.
    virtual void MaintenanceTick() {}

    // Volume / sourdine globaux (Parametres > Audio), pour les fonds qui produisent du son.
    virtual void SetAudioOutput(float /*volume*/, bool /*muted*/) {}

    // Echec asynchrone (fichier video illisible, runtime WebView2 absent...) : l'application remplace
    // alors le fond par le shader de secours.
    virtual bool HasFailed(std::wstring& /*error*/) const { return false; }
};

}  // namespace lwe
