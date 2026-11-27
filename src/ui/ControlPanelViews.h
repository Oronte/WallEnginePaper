#pragma once

#include <windows.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "app/AppServices.h"
#include "render/RenderTexture.h"
#include "render/TextureLoader.h"
#include "ui/UiStyle.h"
#include "wallpaper/IWallpaperBackend.h"

namespace lwe {

struct LibraryItem;

// Contenu de la fenetre de configuration (mode immediat Dear ImGui) : en-tete, Bibliotheque, Ecrans,
// Playlist, Parametres. Possede les miniatures et l'apercu en direct (rendus hors ecran sur le device partage).
class ControlPanelViews {
public:
    ControlPanelViews(IAppServices& services, HWND owner);
    ~ControlPanelViews();
    ControlPanelViews(const ControlPanelViews&) = delete;
    ControlPanelViews& operator=(const ControlPanelViews&) = delete;

    void SetStyle(const ui::Fonts& fonts, float scale);
    void Draw(float deltaSeconds);

private:
    enum class Tab { Library, Screens, Playlist, Settings };
    enum class Filter { All, Shader, Video, Web, Scene };

    struct Thumbnail {
        RenderTexture rendered;  // shaders : une image rendue a t = 4 s
        LoadedTexture image;     // images et apercus de project.json
        bool attempted = false;
    };

    void DrawHeader();
    void DrawLibrary(float deltaSeconds);
    void DrawLibraryToolbar();
    void DrawLibraryGrid(const std::vector<const LibraryItem*>& items);
    void DrawCardContextMenu(const LibraryItem& item);
    void DrawDetails(float deltaSeconds);
    void DrawParameters(const LibraryItem& item);
    void DrawScreens();
    void DrawPlaylist();
    void DrawSettings();
    void DrawToast(float deltaSeconds);

    std::vector<const LibraryItem*> FilteredItems() const;
    const LibraryItem* SelectedItem() const;
    void Apply(const LibraryItem& item, const std::string& monitorId);
    std::string MonitorLabel(const std::string& monitorId) const;
    std::vector<std::string> ScreensShowing(const LibraryItem& item) const;

    ImTextureID ThumbnailFor(const LibraryItem& item);
    void GeneratePendingThumbnail();
    Thumbnail BuildThumbnail(const LibraryItem& item);
    void InvalidateThumbnail(const std::string& id);
    void RetireAllThumbnails();
    void DrawPlaceholder(ImDrawList* drawList, ImVec2 min, ImVec2 max, WallpaperType type, float rounding,
                         ImDrawFlags flags) const;

    void EnsurePreview(const LibraryItem& item);
    void RenderPreview(float deltaSeconds, const MouseState& mouse);
    bool DrawParameter(const ParamDescriptor& desc, ParamValue& value);

    void Toast(std::string message);

    IAppServices& services_;
    HWND owner_;
    ui::Fonts fonts_;
    float scale_ = 1.0f;

    Tab tab_ = Tab::Library;
    Filter filter_ = Filter::All;
    char search_[128] = {};
    std::string selectedId_;
    std::string targetMonitor_;    // "" = tous les ecrans
    std::string selectedMonitor_;  // onglet Ecrans
    float renderScaleEdit_ = -1.0f;

    LoadedTexture logo_;
    std::map<std::string, Thumbnail> thumbnails_;
    std::vector<std::string> pendingThumbnails_;
    // Miniatures remplacees pendant l'image en cours : liberees au debut de l'image suivante, une fois
    // que les listes de dessin ImGui qui pouvaient les referencer ont ete soumises.
    std::vector<Thumbnail> retiredThumbnails_;

    std::string previewId_;
    std::unique_ptr<IWallpaperBackend> previewBackend_;
    RenderTexture previewTarget_;
    std::wstring previewError_;
    double previewTime_ = 0.0;
    uint64_t previewFrame_ = 0;
    MouseState previewMouse_;
    AudioSpectrum silence_;       // miniatures : instantane sans son
    AudioSpectrum previewAudio_;  // apercu en direct : spectre courant

    std::string toast_;
    float toastTime_ = 0.0f;

    // Instantane par image (evite de reinterroger l'application pour chaque carte).
    std::vector<SurfaceStatusInfo> surfaces_;
    std::vector<MonitorEntry> monitors_;
};

}  // namespace lwe
