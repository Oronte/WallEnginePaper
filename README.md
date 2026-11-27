# WallEnginePaper

Moteur de fonds d'écran animés **natif et 100 % local** pour Windows 10/11 : C++20, Win32, Direct3D 11,
interface Dear ImGui. Aucun réseau, aucun compte, aucune télémétrie. Un seul `.exe` (CRT statique) + `assets/`.

## Lancer

L'exécutable : `build\vs2026\Release\WallEnginePaper.exe` (garder le dossier `assets` à côté).

- Lancement manuel : ouvre la fenêtre WallEnginePaper.
- Démarrage de Windows : activé par défaut (`HKCU\Software\Microsoft\Windows\CurrentVersion\Run`, option
  `--minimized`). L'application démarre alors discrètement dans la zone de notification. Désactivable dans
  *Paramètres › Démarrage*.
- Fermer la fenêtre laisse l'application tourner dans la zone de notification (fenêtre et ressources GPU de
  l'interface libérées). Clic sur l'icône : rouvrir ; clic droit : Pause, Muet, Fond suivant, Étendre, FPS,
  Quitter.
- Relancer l'exe alors qu'il tourne déjà rouvre simplement sa fenêtre.

| Option | Effet |
|---|---|
| `--minimized` | démarre sans ouvrir la fenêtre |
| `--wallpaper <fichier>` | force un fond sur tous les écrans pour cette session |
| `--exit-after <s>` | s'arrête proprement après *s* secondes (tests) |
| `--quit` | ferme l'instance en cours |

## Interface

- **Bibliothèque** : grille de miniatures (rendues par le moteur), recherche, filtres par type, import de
  fichiers ou de dossiers `project.json`, clic droit pour appliquer à un écran précis. Panneau latéral :
  aperçu en direct (interactif à la souris), choix de l'écran cible, réglages en temps réel (vitesse, teinte,
  filtre couleur, luminosité, sensibilité audio, parallaxe + paramètres `@param` du shader), réinitialisation.
- **Écrans** : schéma des écrans à l'échelle avec leur fond, mode « un fond par écran » ou « étendu ».
- **Playlist** : rotation automatique (intervalle, ordre aléatoire, changement au démarrage, sélection des fonds).
- **Paramètres** : FPS (15/30/60/144 ou libre), qualité de rendu, volume et sourdine, règles de pause
  automatique, démarrage avec Windows, dossiers de bibliothèque, dépannage, à propos.

## Types de fonds

| Type | Fichiers | Moteur | Réglages |
|---|---|---|---|
| Shader | `.hlsl` | HLSL temps réel style Shadertoy | vitesse, teinte, couleur, luminosité, parallaxe, `@param` |
| Vidéo | `.mp4`, `.webm`, `.mkv`, `.mov`, `.avi`, `.wmv` | Media Foundation (Media Engine), décodage **GPU** (D3D11VA), trames copiées en VRAM | vitesse de lecture, ajustement, parallaxe, teinte, couleur, luminosité |
| Image | `.png`, `.jpg`, `.bmp`, `.webp`, `.gif` (animé) | WIC ; GIF recomposés image par image | ajustement, parallaxe (1 par défaut), vitesse des GIF… |
| Web | `index.html` (+ `project.json`) | Microsoft WebView2 | section `properties` du `project.json` |

- **Vidéo** : boucle continue ; une même vidéo affichée sur plusieurs écrans (ou dans l'aperçu) n'est décodée
  et entendue qu'**une fois**. En pause prolongée, le décodeur est libéré et la lecture reprend au même endroit.
  Ajustement « remplir », « adapter » ou « étirer ». Les codecs disponibles sont ceux de Windows (H.264 et
  HEVC/VP9/AV1 selon les extensions installées).
- **Image / vidéo** : rien n'est redessiné tant que l'image ne change pas (nouvelle trame, mouvement de
  parallaxe, réglage) : une image fixe ne coûte quasiment rien.
- **Web, 100 % local** : le dossier du fond est servi sur un domaine virtuel (`https://wallpaper.example/`) ;
  toute requête ou navigation vers l'extérieur est bloquée (et journalisée), les services réseau du navigateur
  (mises à jour de composants, synchronisation, SmartScreen…) sont coupés. Profil : `%LOCALAPPDATA%\WallEnginePaper\WebView2`.
  Les icônes du bureau captant la souris, les mouvements et clics sont rejoués dans la page (`pointermove`,
  `mousemove`, `mousedown`, `mouseup`, `click`) et exposés via l'API :

  ```js
  const wep = window.wallEnginePaper;   // mouse {x, y, nx, ny, inside}, audio (Float32Array 64), audioLevel,
                                        // params, volume, muted, paused
  wep.on('params', p => { /* réglages du project.json */ });
  wep.on('mouse',  m => { /* ... */ });
  wep.on('audio',  bands => { /* spectre 64 bandes */ });
  wep.on('pause',  paused => { /* ... */ });
  ```

  `project.json` d'un fond web (exemple complet : `assets/web/constellation`) :

  ```json
  { "title": "Constellation", "type": "web", "file": "index.html", "preview": "preview.png",
    "properties": { "count": { "type": "slider", "label": "Étoiles", "value": 220, "min": 40, "max": 600 },
                    "color": { "type": "color", "value": [0.55, 0.42, 1.0] },
                    "attract": { "type": "bool", "value": true } } }
  ```

En cas d'échec (codec manquant, fichier illisible, runtime WebView2 absent), le fond est réessayé une fois puis
remplacé par le shader de secours, avec une notification.

## Compilation

Prérequis : Visual Studio 2022 (17.8+) ou 2026 avec « Développement Desktop C++ », CMake ≥ 3.26.
Les dépendances (nlohmann/json, Dear ImGui, SDK WebView2) sont téléchargées une fois par CMake, empreinte
SHA-256 vérifiée. Le Release produit aussi `WallEnginePaper.pdb` (même binaire) pour lire les rapports de plantage.

```powershell
cmake --preset vs2022            # ou vs2026
cmake --build --preset vs2022-release
```

## Arborescence

```
CMakeLists.txt, CMakePresets.json, cmake/      Build, dépendances FetchContent, version
res/                                         app.ico (multi-tailles), logo.png, manifeste, ressources
assets/shaders/                              aurora.hlsl, plasma_spectrum.hlsl, starfield.hlsl
src/
  main.cpp
  app/AppServices.h            Interface lue/pilotée par l'UI (découplage UI ⇄ moteur)
  app/Application.*            Boucle cadencée (fonds + UI), surfaces, pauses, playlist, tray
  audio/                       AudioCapture (WASAPI loopback), SpectrumAnalyzer (FFT 64 bandes)
  core/                        Log, chemins (%APPDATA%\WallEnginePaper), UTF-8/16, RAII Win32
  config/                      AppConfig + JSON, ConfigManager (sauvegarde atomique différée)
  desktop/                     DesktopHost (WorkerW classique + 24H2), écrans, MonitorContext (surface)
  input/InputForwarder.*       Curseur global sans hook
  library/Library.*            Catalogue local (assets, fonds importés, dossiers, project.json)
  render/                      Device D3D11 partagé, FrameLimiter, RenderTexture, TextureLoader (WIC)
  system/Autostart.*           Clé Run de l'utilisateur
  watchdog/PerformanceWatchdog.*  Plein écran / maximisé par écran, batterie, mode présentation
  wallpaper/IWallpaperBackend.h, WallpaperFactory.*   Contrat commun, fabrique
  wallpaper/shader/            ShaderBackend (HLSL style Shadertoy)
  wallpaper/media/             MediaCompositor : ajustement, parallaxe, couleur (images et vidéos)
  wallpaper/scene/             SceneBackend (images), GifAnimation (GIF animés)
  wallpaper/video/             VideoPlayer (Media Engine partagé), VideoBackend, VideoThumbnail
  wallpaper/web/               WebBackend (WebView2 verrouillé en local + pont JS)
  ui/                          ControlPanel (fenêtre + ImGui), ControlPanelViews, UiStyle, TrayIcon, FileDialogs
assets/web/constellation/      Fond web d'exemple (Canvas, réglages, souris)
tests/                         Tests (ctest)
```

## Fonds réactifs au son

- **Capture** : WASAPI « loopback » sur la sortie audio par défaut : uniquement ce que le PC envoie aux
  haut-parleurs (jamais le micro), analysé localement. Thread dédié, suit le changement de sortie (casque…).
- **Analyse** : FFT de 2048 échantillons (fenêtre de Hann), nouvelle analyse tous les 512 échantillons
  (~94/s), 64 bandes normalisées en dB (compensation +3 dB/octave), attaque rapide, relâchement lent.
  Au-dessus d'environ 1 kHz les bandes sont logarithmiques ; en dessous, une bande par bin de FFT. Basses ≈
  bandes 0 à 7.
- **Utilisation** : shaders (`AudioBand(i)`, `AudioSample(x)`, `AudioBass/Mid/Treble()`, `iAudioLevel`),
  fonds web (`wallEnginePaper.on('audio', …)`), aperçu de l'interface. Réglages : *Paramètres › Audio*
  (activation, sensibilité, spectre en direct).
- **Coût** : la capture ne tourne que si quelque chose est dessiné ; en pause ou en jeu plein écran, aucun
  thread audio.

## Tests

```powershell
cmake --build --preset vs2026-release --target WallEnginePaperTests
ctest --test-dir build/vs2026 -C Release
```

`tests/SpectrumAnalyzerTest.cpp` : sinusoïdes pures (100 Hz à 12 kHz) dans la bonne bande, silence nul,
retombée et niveau proportionnel au volume.

## Points d'architecture

**Accroche au bureau.** `DesktopHost` envoie `0x052C` à `Progman` puis détecte la disposition : *classique*
(WorkerW de premier niveau derrière les icônes) ou *24H2+* (WorkerW enfant de Progman : nos surfaces sont
insérées sous `SHELLDLL_DefView`, avec `WS_EX_LAYERED`). À la fermeture, le papier peint natif est repeint ; un
filtre d'exception fait de même en cas de plantage. Redémarrage de l'Explorateur et changements d'écrans gérés.

**Pauses.** Pause globale (manuelle, écran éteint, session verrouillée, batterie) et pause **par écran** :
toutes les 500 ms, `PerformanceWatchdog` repère une application plein écran ou borderless (ou maximisée, en
option) sur chaque écran ; seul l'écran recouvert cesse d'être dessiné. Quand plus rien n'est dessiné, le thread
dort (0 % CPU) et la mémoire temporaire du pilote est rendue (`IDXGIDevice3::Trim`).

**Coût.** Device créé avec `PREVENT_INTERNAL_THREADING_OPTIMIZATIONS`, swap chains flip avec objet d'attente de
latence, priorité CPU basse et priorité GPU abaissée, shaders compilés une seule fois pour N écrans.
Mesuré (RTX 3080, 2 écrans 1080p, 60 FPS) : ~2,3 % d'un cœur et ~47 Mo fenêtre fermée ; ~3 % et ~56 Mo
fenêtre ouverte.

## Écrire un shader

```hlsl
// @title Mon fond
// @description Affichée dans la bibliothèque.
// @param float intensity 1.0 0.0 2.0 "Intensité"
// @param color baseColor 0.2 0.5 1.0 "Couleur"
// @param bool  showBars  true        "Afficher le spectre"

void mainImage(out float4 fragColor, in float2 fragCoord)
{
    float2 uv = fragCoord / iResolution.xy;          // origine en bas à gauche (Shadertoy)
    float3 col = baseColor.rgb * intensity * (0.5 + AudioBass());
    fragColor = float4(col, 1.0);
}
```

Uniformes : `iResolution`, `iTime`, `iTimeDelta`, `iFrame`, `iFrameRate`, `iDate`, `iMouse`, `iMouseNorm`,
`iParallax`, `iMouseInside`, `iAudioLevel`, `iAudioSpectrum[16]` (via `AudioBand(i)`, `AudioSample(x)`,
`AudioBass/Mid/Treble()`). Chaque `@param` (16 max) devient une macro et un réglage dans l'interface. Alias GLSL
fournis (`vec2/3/4`, `fract`, `mix`, `mod`…). Sauvegarder le fichier le recompile à chaud ; une erreur garde
la version précédente et part dans le journal.

## Configuration

`%APPDATA%\WallEnginePaper\config.json` (reprise automatique de l'ancien `%APPDATA%\LocalWallpaperEngine`).
Fonds importés : `%APPDATA%\WallEnginePaper\library`. Journal : `%APPDATA%\WallEnginePaper\log.txt`.
