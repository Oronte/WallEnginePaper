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
| `--no-autopause` | ignore les pauses plein écran / maximisé / batterie pour cette session (mesures) |
| `--audio-debug` | force la capture audio et résume le signal chaque seconde dans le journal |
| `--next-every <s>` | passe au fond suivant toutes les *s* secondes (tests des transitions ; modifie la config) |

`tools/bench.sh [secondes] [fonds...]` lance chaque fond avec `--no-autopause --exit-after` et affiche la ligne
`Regime etabli` du journal (FPS et CPU par seconde, mesurés après 5 s pour exclure le chargement). Mesures :
`docs/bench-*.txt`.

## Interface

- **Bibliothèque** : grille de miniatures (rendues par le moteur), recherche, filtres par type, import de
  fichiers ou de dossiers `project.json`, clic droit pour appliquer à un écran précis. Panneau latéral :
  aperçu en direct (interactif à la souris), choix de l'écran cible, réglages en temps réel (vitesse, teinte,
  filtre couleur, luminosité, sensibilité audio, parallaxe + paramètres `@param` du shader), réinitialisation.
- **Écrans** : schéma des écrans à l'échelle avec leur fond, mode « un fond par écran » ou « étendu ».
- **Playlist** : rotation automatique (intervalle, ordre aléatoire, changement au démarrage, sélection des fonds,
  transition propre à la playlist).
- **Automatisation** : planification (plages horaires, lever / coucher du soleil, thème clair / sombre) et profils
  (choix manuel, profil sur batterie, règles par application au premier plan, éditeur de profils).
- **Paramètres** : FPS (15/30/60/144 ou libre), qualité de rendu, transitions, raccourcis clavier, accessibilité,
  volume et sourdine, audio réactif, règles de pause automatique, démarrage avec Windows, dossiers de
  bibliothèque, dépannage, à propos.

## Contrôle et confort

- **Raccourcis clavier globaux** (`RegisterHotKey`, actifs même fenêtre fermée), modifiables dans *Paramètres ›
  Raccourcis clavier* (clic puis combinaison ; Échap annule, Retour arrière efface) :

  | Action | Par défaut |
  |---|---|
  | Fond suivant | Ctrl + Alt + → |
  | Fond précédent | Ctrl + Alt + ← |
  | Pause / reprise | Ctrl + Alt + P |
  | Muet | Ctrl + Alt + M |
  | Afficher / masquer la fenêtre | Ctrl + Alt + W |

  Conflits signalés : même combinaison pour deux actions, ou combinaison déjà prise par une autre application.
  Une combinaison sans Ctrl / Alt / Maj / Win n'est acceptée que pour F13–F24, Pause et les touches multimédia.
- **Fond précédent** : historique par écran (20 fonds), sinon ordre de rotation à rebours.
- **Planification** (onglet *Automatisation*, prioritaire sur la playlist) :
  - plages horaires (pas de 15 min, une plage peut passer minuit, hors plage le fond reste celui choisi) ;
  - lever et coucher du soleil calculés localement (algorithme du NOAA, précision ~1 min) à partir d'une latitude
    et d'une longitude saisies à la main, avec décalages ; soleil de minuit et nuit polaire gérés ;
  - thème clair / sombre de Windows (`AppsUseLightTheme`, suivi en direct via `WM_SETTINGCHANGE`).
  Le fond n'est imposé qu'au changement de créneau : un choix manuel reste en place jusqu'au créneau suivant.
- **Profils** : *Gaming* (30 FPS, qualité 75 %, muet, pause si maximisé), *Travail* (30 FPS, volume 30 %),
  *Batterie* (15 FPS, qualité 50 %, ralenti plutôt qu'arrêté sur batterie), plus des profils personnalisés
  (FPS, qualité, règles de pause, volume, muet). Priorité : règle de l'application au premier plan (ex.
  `game.exe`), puis profil « sur batterie », puis profil choisi ; bascule après ~1 s de stabilité.
- **Accessibilité** : « Afficher les animations dans Windows » est respecté (`SPI_GETCLIENTAREAANIMATION`, suivi en
  direct). Mode réduit (automatique, toujours ou jamais) : 15 FPS au plus, ni parallaxe ni transition.
- **Zone de notification** : Fond précédent, Planification (case à cocher), sous-menu Profil.

## Transitions

Au changement de fond (manuel, playlist, « fond suivant », planification), l'ancien et le nouveau fond sont rendus
chacun dans une `RenderTexture` puis composés par un shader de transition : **fondu**, **dissolve** (bruit
fractal avec liseré lumineux), **balayage** (direction tirée au hasard, bord adouci), **zoom et fondu**, ou
**aléatoire**. Durée de 0 à 3 s (0 = coupe nette). Réglage global dans *Paramètres › Transitions*, réglage
propre dans *Playlist*.

- L'ancien fond n'est libéré qu'à la fin de la transition, ainsi que les deux textures intermédiaires (aucune
  VRAM conservée au repos).
- Réappliquer le même fond (réglage réinitialisé, nouvel essai) ne déclenche jamais de transition : une vidéo n'est
  donc jamais décodée deux fois inutilement (et un même fichier vidéo reste partagé entre écrans).
- Un fond web s'affiche lui-même (WebView2) : passage en coupe nette s'il est l'ancien ou le nouveau fond.
- Une surface mise en pause pendant une transition la termine immédiatement.

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
                                        // audioBands {bass, mid, treble}, bpm, beat, params, volume, muted, paused
  wep.on('params', p => { /* réglages du project.json */ });
  wep.on('mouse',  m => { /* ... */ });
  wep.on('audio',  bands => { /* spectre 64 bandes ; wep.audioBands.bass… à jour */ });
  wep.on('beat',   e => { /* e.bpm, e.count, e.time (performance.now()) */ });
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
  audio/                       AudioCapture (WASAPI loopback), SpectrumAnalyzer (FFT 64 bandes, zones),
                               BeatDetector (beats, tempo)
  core/                        Log, chemins (%APPDATA%\WallEnginePaper), UTF-8/16, RAII Win32
  config/                      AppConfig + JSON, ConfigManager (sauvegarde atomique différée)
  desktop/                     DesktopHost (WorkerW classique + 24H2), écrans, MonitorContext (surface)
  input/InputForwarder.*       Curseur global sans hook
  library/Library.*            Catalogue local (assets, fonds importés, dossiers, project.json)
  render/                      Device D3D11 partagé, FrameLimiter, RenderTexture, TextureLoader (WIC),
                               TransitionRenderer (shader de transition)
  system/Autostart.*           Clé Run de l'utilisateur
  system/SystemInfo.*          Thème clair/sombre, animations Windows, couleur d'accent, application au premier plan
  control/                     SunTimes (NOAA), Scheduler, Profiles (réglages effectifs), HotkeyManager
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
  Au-dessus d'environ 1 kHz les bandes sont logarithmiques ; en dessous, une bande par bin de FFT. Graves :
  bandes 0 à 9 (~20–260 Hz), médiums 10 à 48 (~260 Hz–3,9 kHz), aigus 49 à 63.
- **Gain automatique** (activé par défaut) : une source calme est remontée jusqu'à +18 dB (enveloppe RMS lente,
  ~4 s), jamais atténuée, gelée pendant les silences pour éviter un sursaut à la reprise.
- **Validation** : sinusoïdes (100 Hz, 1 kHz, 8 kHz), bruit rose et kick à 120 BPM joués réellement et relus en
  loopback avec `--audio-debug` : bonne bande, bruit rose plat (~0,5 sur les trois zones), retour à 0 en moins
  d'une seconde de silence, latence estimée < 50 ms (fenêtre de 43 ms + sondage de 10 ms). Une capture
  interrompue (périphérique désactivé, format de mixage modifié) est rouverte automatiquement.
- **Graves / médiums / aigus** : trois zones par fréquence (~20–250 Hz, 250 Hz–4 kHz, 4–16 kHz), mélange du
  maximum et de la moyenne des bandes, lissées par une attaque et un relâchement réglables (10 ms / 200 ms).
- **Beat et tempo** : flux spectral « SuperFlux » (hausse du spectre en dB par rapport au maximum des bins voisins
  de l'analyse précédente, graves pondérés ×1, médiums ×0,4, aigus ×0,1) ; un beat est un pic local au-dessus d'un
  seuil adaptatif (moyenne + 1,5 σ sur 1,5 s, 2 × la moyenne, 30 % des attaques fortes des 3 dernières secondes,
  divisés par la sensibilité). Tempo : autocorrélation de la nouveauté sur 6 s, 2 fois par seconde, pondérée
  autour de 120 BPM (évite les erreurs d'octave), interpolation parabolique, changement confirmé deux fois ;
  oublié après 5 s sans beat. L'intensité vaut 1 au beat et décroît sur un quart de période.
  Mesuré en loopback réel : kick à 120 BPM → 119,8 BPM, kick à 128 BPM → 127,9 BPM (en ~3 s), bruit rose → aucun beat.
- **Utilisation** : shaders (`iBass`, `iMid`, `iTreble`, `iBeat`, `iBpm`, `iBeatTime`, `iBeatCount`,
  `AudioBand(i)`, `AudioSample(x)`, `iAudioLevel`), fonds web (`wep.audioBands`, `wep.bpm`,
  `wep.on('beat', …)`, `wep.on('audio', …)`), aperçu de l'interface. Réglages : *Paramètres › Audio*
  (activation, sensibilité, gain automatique, sensibilité du beat, attaque / relâchement, spectre en direct,
  vumètre graves / médiums / aigus avec pastille de beat et tempo).
- **Coût** : la capture ne tourne que si quelque chose est dessiné ; en pause ou en jeu plein écran, aucun
  thread audio.

## Tests

```powershell
cmake --build --preset vs2026-release --target WallEnginePaperTests
ctest --test-dir build/vs2026 -C Release
```

`tests/SpectrumAnalyzerTest.cpp` : sinusoïdes pures (100 Hz à 12 kHz) dans la bonne bande, silence nul,
retombée, niveau proportionnel au volume, gain automatique (source calme remontée, son fort intact, silence),
zones graves / médiums / aigus, relâchement réglable.
`tests/BeatDetectorTest.cpp` : clics à 90/120/128/140 BPM et kick + charleston à 100/120/128 BPM (tempo à ±2 BPM,
nombre de beats, charleston ignoré), décroissance de l'intensité, silence, bruit rose et son tenu sans beat, oubli
du tempo, sensibilité. `LWE_BEAT_TRACE=1` affiche la nouveauté et le seuil image par image.
`tests/ConfigTest.cpp` : aller-retour JSON des nouveaux réglages, ancien `config.json` (valeurs par défaut),
valeurs aberrantes bornées, transition aléatoire toujours concrète.
`tests/ControlTest.cpp` : lever / coucher du soleil (Paris été / hiver, New York, Sydney, Tromsø polaire),
plages horaires (passage de minuit, trou, fin exclue), modes soleil et thème, format et lecture des raccourcis,
correspondance d'exécutable, priorité des profils, réglages effectifs (profil, animations réduites).

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

**Repos.** Quand aucune surface n'a rien présenté depuis ~0,5 s (image fixe, vidéo entre deux trames lentes) et
que la souris ne bouge pas, la boucle ne se réveille plus que 10 fois par seconde au lieu de 60 : une image fixe
passe de ~1 à 3 % d'un cœur à ~0,3 à 0,9 %. Le moindre mouvement de souris (parallaxe), un réglage ou un
changement de fond rétablit aussitôt la cadence normale.

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
`iParallax`, `iMouseInside`, `iAudioLevel`, `iAudioSpectrum[16]` (via `AudioBand(i)`, `AudioSample(x)`).

| Uniforme audio | Contenu |
|---|---|
| `iBass`, `iMid`, `iTreble` | zones ~20–250 Hz, 250 Hz–4 kHz, 4–16 kHz, lissées, [0, 1] (aussi `AudioBass()`…) |
| `iBeat` | 1 au beat, décroît jusqu'au suivant |
| `iBpm` | tempo estimé (0 si inconnu) |
| `iBeatTime` | secondes depuis le dernier beat |
| `iBeatCount` | nombre de beats depuis l'affichage du fond |

 Chaque `@param` (16 max) devient une macro et un réglage dans l'interface. Alias GLSL
fournis (`vec2/3/4`, `fract`, `mix`, `mod`…). Sauvegarder le fichier le recompile à chaud ; une erreur garde
la version précédente et part dans le journal.

## Configuration

`%APPDATA%\WallEnginePaper\config.json` (reprise automatique de l'ancien `%APPDATA%\LocalWallpaperEngine`).
Fonds importés : `%APPDATA%\WallEnginePaper\library`. Journal : `%APPDATA%\WallEnginePaper\log.txt`.
