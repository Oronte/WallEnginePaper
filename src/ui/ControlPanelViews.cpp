#define IMGUI_DEFINE_MATH_OPERATORS
#include "ui/ControlPanelViews.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>

#include <imgui.h>

#include "config/ConfigManager.h"
#include "control/Scheduler.h"
#include "core/Paths.h"
#include "core/StringUtil.h"
#include "library/Library.h"
#include "lwe_version.h"
#include "render/RenderDevice.h"
#include "resource.h"
#include "ui/FileDialogs.h"
#include "wallpaper/WallpaperFactory.h"
#include "wallpaper/video/VideoThumbnail.h"

namespace lwe {
namespace {

using namespace ui;

constexpr uint32_t kThumbWidth = 384;
constexpr uint32_t kThumbHeight = 216;
constexpr uint32_t kPreviewWidth = 768;
constexpr uint32_t kPreviewHeight = 432;
constexpr float kToastDuration = 3.0f;

std::string U8(const std::wstring& text) {
    return WideToUtf8(text);
}

std::string WithIcon(const char* glyph, const char* label) {
    return std::string(glyph) + "  " + label;
}

ImTextureID ToTextureId(ID3D11ShaderResourceView* view) {
    return (ImTextureID)(intptr_t)view;
}

bool HasTexture(ImTextureID id) {
    return id != ImTextureID{};
}

const char* TypeLabel(WallpaperType type) {
    switch (type) {
    case WallpaperType::Shader: return "Shader";
    case WallpaperType::Video: return "Vidéo";
    case WallpaperType::Web: return "Web";
    case WallpaperType::Scene: return "Image";
    }
    return "";
}

const char* TypeIcon(WallpaperType type) {
    switch (type) {
    case WallpaperType::Shader: return icon::kCode;
    case WallpaperType::Video: return icon::kVideo;
    case WallpaperType::Web: return icon::kGlobe;
    case WallpaperType::Scene: return icon::kPicture;
    }
    return icon::kPicture;
}

ImVec4 TypeColor(WallpaperType type) {
    switch (type) {
    case WallpaperType::Shader: return color::kAccentHover;
    case WallpaperType::Video: return color::kAccent2;
    case WallpaperType::Web: return color::kSuccess;
    case WallpaperType::Scene: return color::kWarning;
    }
    return color::kTextDim;
}

size_t Utf8Length(unsigned char lead) {
    if ((lead & 0x80) == 0) return 1;
    if ((lead & 0xE0) == 0xC0) return 2;
    if ((lead & 0xF0) == 0xE0) return 3;
    return 4;
}

// Tronque proprement (sans couper un caractere UTF-8) et ajoute des points de suspension.
std::string Ellipsize(const std::string& text, float maxWidth) {
    if (ImGui::CalcTextSize(text.c_str()).x <= maxWidth) {
        return text;
    }
    const char* ellipsis = "…";
    const float ellipsisWidth = ImGui::CalcTextSize(ellipsis).x;
    std::string result;
    for (size_t i = 0; i < text.size();) {
        const size_t length = std::min(Utf8Length(static_cast<unsigned char>(text[i])), text.size() - i);
        const std::string candidate = result + text.substr(i, length);
        if (ImGui::CalcTextSize(candidate.c_str()).x + ellipsisWidth > maxWidth) {
            break;
        }
        result = candidate;
        i += length;
    }
    return result + ellipsis;
}

std::string PathKey(const std::filesystem::path& path) {
    return WideToUtf8(ToLower(path.lexically_normal().wstring()));
}

bool PillButton(const char* label, bool active, ImVec2 size = ImVec2(0, 0)) {
    ImGui::PushStyleColor(ImGuiCol_Button, active ? color::kAccent : color::kFrame);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, active ? color::kAccentHover : color::kFrameHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, color::kAccentActive);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 999.0f);
    const bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    return pressed;
}

bool AccentButton(const char* label, ImVec2 size = ImVec2(0, 0)) {
    ImGui::PushStyleColor(ImGuiCol_Button, color::kAccent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, color::kAccentHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, color::kAccentActive);
    const bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);
    return pressed;
}

void DimText(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, color::kTextDim);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

void DrawPill(ImDrawList* drawList, ImVec2 position, const char* text, ImU32 background, ImU32 foreground,
              float padX, float padY) {
    const ImVec2 size = ImGui::CalcTextSize(text) + ImVec2(padX * 2, padY * 2);
    drawList->AddRectFilled(position, position + size, background, size.y * 0.5f);
    drawList->AddText(position + ImVec2(padX, padY), foreground, text);
}

}  // namespace

ControlPanelViews::ControlPanelViews(IAppServices& services, HWND owner) : services_(services), owner_(owner) {
    logo_ = LoadTextureFromResource(services_.Device().Device(), IDR_LOGO_PNG, 256);
}

ControlPanelViews::~ControlPanelViews() {
    if (capturingHotkey_ >= 0) {
        services_.SuspendHotkeys(false);  // fenetre fermee pendant une saisie
    }
}

void ControlPanelViews::SetStyle(const ui::Fonts& fonts, float scale) {
    fonts_ = fonts;
    scale_ = scale;
}

void ControlPanelViews::Toast(std::string message) {
    toast_ = std::move(message);
    toastTime_ = kToastDuration;
}

// --------------------------------------------------------------------------------------------- //
//  Cadre general
// --------------------------------------------------------------------------------------------- //

void ControlPanelViews::Draw(float deltaSeconds) {
    // Les listes de dessin ImGui referencent des textures jusqu'a ImGui_ImplDX11_RenderDrawData, apres ce
    // Draw(). Une texture ne doit donc jamais etre liberee pendant la construction de l'image : celles qui
    // ont ete remplacees ou videes a l'image precedente ne sont relachees qu'ici, une fois cette image soumise.
    retiredThumbnails_.clear();
    GeneratePendingThumbnail();  // avant tout dessin : aucune reference vers une texture remplacee

    surfaces_ = services_.Surfaces();
    monitors_ = services_.Monitors();
    if (selectedId_.empty() && !surfaces_.empty()) {
        if (const LibraryItem* current = services_.GetLibrary().FindBySource(surfaces_.front().source)) {
            selectedId_ = current->id;
        }
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##root", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();

    DrawHeader();

    const float padding = 20.0f * scale_;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padding, padding));
    ImGui::BeginChild("##body", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding,
                      tab_ == Tab::Library ? ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
                                           : ImGuiWindowFlags_None);
    ImGui::PopStyleVar();
    switch (tab_) {
    case Tab::Library: DrawLibrary(deltaSeconds); break;
    case Tab::Screens: DrawScreens(); break;
    case Tab::Playlist: DrawPlaylist(); break;
    case Tab::Automation: DrawAutomation(); break;
    case Tab::Settings: DrawSettings(); break;
    }
    ImGui::EndChild();
    ImGui::End();

    DrawToast(deltaSeconds);
}

void ControlPanelViews::RetireAllThumbnails() {
    for (auto& [id, thumbnail] : thumbnails_) {
        retiredThumbnails_.push_back(std::move(thumbnail));
    }
    thumbnails_.clear();
    pendingThumbnails_.clear();
}

void ControlPanelViews::DrawHeader() {
    const float s = scale_;
    const float height = 70.0f * s;
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetWindowWidth();

    drawList->AddRectFilled(origin, origin + ImVec2(width, height), ToU32(color::kPanel));
    drawList->AddRectFilledMultiColor(origin + ImVec2(0, height - 2 * s), origin + ImVec2(width, height),
                                      ToU32(color::kAccent, 0.7f), ToU32(color::kAccent2, 0.7f),
                                      ToU32(color::kAccent2, 0.0f), ToU32(color::kAccent, 0.0f));

    // Logo + nom
    const float logoSize = 46.0f * s;
    const ImVec2 logoPos(origin.x + 18 * s, origin.y + (height - logoSize) * 0.5f);
    if (logo_) {
        drawList->AddImageRounded(ToTextureId(logo_.view.Get()), logoPos, logoPos + ImVec2(logoSize, logoSize),
                                  ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 10 * s);
    }
    ImGui::PushFont(fonts_.heading);
    const ImVec2 titleSize = ImGui::CalcTextSize("WallEnginePaper");
    drawList->AddText(ImVec2(logoPos.x + logoSize + 12 * s, origin.y + (height - titleSize.y) * 0.5f),
                      ToU32(color::kText), "WallEnginePaper");
    ImGui::PopFont();

    // Onglets
    struct TabDef {
        Tab tab;
        const char* glyph;
        const char* label;
    };
    static constexpr TabDef kTabs[] = {
        {Tab::Library, icon::kLibrary, "Bibliothèque"},
        {Tab::Screens, icon::kMonitor, "Écrans"},
        {Tab::Playlist, icon::kPlaylist, "Playlist"},
        {Tab::Automation, icon::kTimer, "Automatisation"},
        {Tab::Settings, icon::kSettings, "Paramètres"},
    };
    const float tabHeight = ImGui::GetFrameHeight() + 4 * s;
    float x = logoPos.x + logoSize + 12 * s + titleSize.x + 36 * s;
    for (const auto& def : kTabs) {
        ImGui::SetCursorScreenPos(ImVec2(x, origin.y + (height - tabHeight) * 0.5f));
        const std::string label = WithIcon(def.glyph, def.label);
        const bool active = tab_ == def.tab;
        ImGui::PushStyleColor(ImGuiCol_Button, active ? ToU32(color::kAccent, 0.22f) : ToU32(color::kPanel, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ToU32(color::kAccent, 0.32f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ToU32(color::kAccent, 0.45f));
        ImGui::PushStyleColor(ImGuiCol_Text, active ? color::kText : color::kTextDim);
        if (ImGui::Button(label.c_str(), ImVec2(0, tabHeight))) {
            tab_ = def.tab;
        }
        ImGui::PopStyleColor(4);
        x += ImGui::GetItemRectSize().x + 6 * s;
    }

    // Etat + commandes rapides (alignes a droite)
    const bool manualPause = services_.IsManuallyPaused();
    const bool muted = services_.Config().Data().audio.muted;
    const std::string pauseLabel = manualPause ? WithIcon(icon::kPlay, "Reprendre") : WithIcon(icon::kPause, "Pause");
    const std::string nextLabel = WithIcon(icon::kNext, "Fond suivant");
    const std::string muteLabel = std::string(muted ? icon::kMute : icon::kVolume) + "##mute";
    const ImGuiStyle& style = ImGui::GetStyle();
    const float pauseWidth = ImGui::CalcTextSize(pauseLabel.c_str()).x + style.FramePadding.x * 2;
    const float nextWidth = ImGui::CalcTextSize(nextLabel.c_str()).x + style.FramePadding.x * 2;
    const float muteWidth = ImGui::GetFrameHeight();
    std::string status = U8(services_.StatusText());
    if (services_.IsRendering()) {
        status += std::format(" · {:.0f} FPS", services_.MeasuredFps());
    }
    ImGui::PushFont(fonts_.caption);
    const ImVec2 statusSize = ImGui::CalcTextSize(status.c_str());
    ImGui::PopFont();

    float right = origin.x + width - 18 * s;
    const float buttonY = origin.y + (height - ImGui::GetFrameHeight()) * 0.5f;
    right -= pauseWidth;
    ImGui::SetCursorScreenPos(ImVec2(right, buttonY));
    if (manualPause ? AccentButton(pauseLabel.c_str()) : ImGui::Button(pauseLabel.c_str())) {
        services_.SetManualPause(!manualPause);
    }
    right -= nextWidth + 8 * s;
    ImGui::SetCursorScreenPos(ImVec2(right, buttonY));
    if (ImGui::Button(nextLabel.c_str())) {
        services_.NextWallpaper();
        Toast("Fond suivant");
    }
    right -= muteWidth + 8 * s;
    ImGui::SetCursorScreenPos(ImVec2(right, buttonY));
    if (muted ? AccentButton(muteLabel.c_str(), ImVec2(muteWidth, 0))
              : ImGui::Button(muteLabel.c_str(), ImVec2(muteWidth, 0))) {
        services_.SetMuted(!muted);
        Toast(muted ? "Son réactivé" : "Son coupé");
    }
    ImGui::SetItemTooltip(muted ? "Réactiver le son des fonds" : "Couper le son des fonds");

    const float statusX = right - statusSize.x - 30 * s;
    if (statusX > x + 10 * s) {
        const float statusY = origin.y + (height - statusSize.y) * 0.5f;
        const ImVec4 dotColor = services_.IsRendering() ? color::kSuccess : color::kWarning;
        drawList->AddCircleFilled(ImVec2(statusX + 5 * s, origin.y + height * 0.5f), 4.5f * s, ToU32(dotColor));
        ImGui::PushFont(fonts_.caption);
        drawList->AddText(ImVec2(statusX + 16 * s, statusY), ToU32(color::kTextDim), status.c_str());
        ImGui::PopFont();
    }

    ImGui::SetCursorScreenPos(origin + ImVec2(0, height));
}

void ControlPanelViews::DrawToast(float deltaSeconds) {
    if (toastTime_ <= 0.0f) {
        return;
    }
    toastTime_ -= deltaSeconds;
    const float alpha = std::clamp(toastTime_ / 0.35f, 0.0f, 1.0f);
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    const ImVec2 padding(16 * scale_, 10 * scale_);
    const ImVec2 size = ImGui::CalcTextSize(toast_.c_str()) + padding * 2;
    const ImVec2 position(viewport->Pos.x + (viewport->Size.x - size.x) * 0.5f,
                          viewport->Pos.y + viewport->Size.y - size.y - 28 * scale_);
    drawList->AddRectFilled(position, position + size, ToU32(color::kPanelRaised, alpha), 10 * scale_);
    drawList->AddRect(position, position + size, ToU32(color::kAccent, 0.7f * alpha), 10 * scale_, 0, 1.5f * scale_);
    drawList->AddText(position + padding, ToU32(color::kText, alpha), toast_.c_str());
}

// --------------------------------------------------------------------------------------------- //
//  Bibliotheque
// --------------------------------------------------------------------------------------------- //

std::vector<const LibraryItem*> ControlPanelViews::FilteredItems() const {
    const std::wstring query = ToLower(Utf8ToWide(search_));
    std::vector<const LibraryItem*> result;
    for (const auto& item : services_.GetLibrary().Items()) {
        const bool typeMatches = filter_ == Filter::All ||
                                 (filter_ == Filter::Shader && item.type == WallpaperType::Shader) ||
                                 (filter_ == Filter::Video && item.type == WallpaperType::Video) ||
                                 (filter_ == Filter::Web && item.type == WallpaperType::Web) ||
                                 (filter_ == Filter::Scene && item.type == WallpaperType::Scene);
        if (!typeMatches) {
            continue;
        }
        if (!query.empty() && ToLower(item.title).find(query) == std::wstring::npos &&
            ToLower(item.description).find(query) == std::wstring::npos) {
            continue;
        }
        result.push_back(&item);
    }
    return result;
}

const LibraryItem* ControlPanelViews::SelectedItem() const {
    return selectedId_.empty() ? nullptr : services_.GetLibrary().FindById(selectedId_);
}

std::string ControlPanelViews::MonitorLabel(const std::string& monitorId) const {
    if (monitorId.empty() || monitorId == kSpanMonitorId) {
        return "tous les écrans";
    }
    for (const auto& monitor : monitors_) {
        if (monitor.info.id == monitorId) {
            return std::format("l'écran {}", monitor.number);
        }
    }
    return "l'écran";
}

std::vector<std::string> ControlPanelViews::ScreensShowing(const LibraryItem& item) const {
    std::vector<std::string> labels;
    const std::string key = PathKey(item.source);
    for (const auto& surface : surfaces_) {
        if (surface.source.empty() || PathKey(surface.source) != key) {
            continue;
        }
        if (surface.monitorId == kSpanMonitorId) {
            labels.emplace_back("Tous");
            continue;
        }
        for (const auto& monitor : monitors_) {
            if (monitor.info.id == surface.monitorId) {
                labels.push_back(std::to_string(monitor.number));
            }
        }
    }
    return labels;
}

void ControlPanelViews::Apply(const LibraryItem& item, const std::string& monitorId) {
    services_.ApplyWallpaper(item, monitorId);
    Toast(std::format("« {} » appliqué à {}", U8(item.title), MonitorLabel(monitorId)));
}

void ControlPanelViews::DrawLibrary(float deltaSeconds) {
    const float s = scale_;
    const float detailsWidth = 400.0f * s;
    const float spacing = 18.0f * s;
    const ImVec2 available = ImGui::GetContentRegionAvail();

    ImGui::BeginChild("##library_main", ImVec2(available.x - detailsWidth - spacing, available.y));
    DrawLibraryToolbar();
    const std::vector<const LibraryItem*> items = FilteredItems();
    ImGui::BeginChild("##library_grid");
    if (services_.GetLibrary().Items().empty()) {
        DimText("La bibliothèque est vide. Importez un fond (.hlsl, vidéo, page web, image) ou ajoutez un dossier "
                "dans les Paramètres.");
    } else if (items.empty()) {
        DimText("Aucun fond ne correspond à la recherche.");
    } else {
        DrawLibraryGrid(items);
    }
    ImGui::EndChild();
    ImGui::EndChild();

    ImGui::SameLine(0, spacing);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, color::kPanel);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18 * s, 18 * s));
    ImGui::BeginChild("##library_details", ImVec2(detailsWidth, available.y), ImGuiChildFlags_AlwaysUseWindowPadding);
    DrawDetails(deltaSeconds);
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

void ControlPanelViews::DrawLibraryToolbar() {
    const float s = scale_;
    const Library& library = services_.GetLibrary();
    const auto countOf = [&](WallpaperType type) {
        return std::count_if(library.Items().begin(), library.Items().end(),
                             [&](const LibraryItem& item) { return item.type == type; });
    };

    const float rightEdge = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    const std::string hint = WithIcon(icon::kSearch, "Rechercher un fond…");
    ImGui::SetNextItemWidth(std::min(300.0f * s, ImGui::GetContentRegionAvail().x * 0.35f));
    ImGui::InputTextWithHint("##search", hint.c_str(), search_, sizeof(search_));

    struct FilterDef {
        Filter filter;
        const char* label;
        long long count;
    };
    const FilterDef filters[] = {
        {Filter::All, "Tous", static_cast<long long>(library.Items().size())},
        {Filter::Shader, "Shaders", countOf(WallpaperType::Shader)},
        {Filter::Video, "Vidéos", countOf(WallpaperType::Video)},
        {Filter::Web, "Web", countOf(WallpaperType::Web)},
        {Filter::Scene, "Images", countOf(WallpaperType::Scene)},
    };
    for (const auto& def : filters) {
        ImGui::SameLine(0, 8 * s);
        const std::string label = std::format("{}  {}##filter", def.label, def.count);
        if (PillButton(label.c_str(), filter_ == def.filter)) {
            filter_ = def.filter;
        }
    }

    const std::string importLabel = WithIcon(icon::kImport, "Importer…");
    const std::string refreshLabel = std::string(icon::kRefresh) + "##refresh";
    const ImGuiStyle& style = ImGui::GetStyle();
    const float buttonsWidth = ImGui::CalcTextSize(importLabel.c_str()).x + ImGui::GetFrameHeight() +
                               style.FramePadding.x * 2 + style.ItemSpacing.x;
    ImGui::SameLine();
    if (ImGui::GetCursorPosX() + buttonsWidth < rightEdge) {
        ImGui::SetCursorPosX(rightEdge - buttonsWidth);
    } else {
        ImGui::NewLine();
    }
    if (AccentButton(importLabel.c_str())) {
        if (const auto file = PickWallpaperFile(owner_)) {
            std::wstring error;
            if (services_.ImportIntoLibrary(*file, error)) {
                Toast("Fond importé dans la bibliothèque");
            } else {
                Toast("Import impossible : " + U8(error));
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(refreshLabel.c_str(), ImVec2(ImGui::GetFrameHeight(), 0))) {
        services_.RescanLibrary();
        RetireAllThumbnails();
        previewId_.clear();
        Toast("Bibliothèque actualisée");
    }
    ImGui::SetItemTooltip("Actualiser la bibliothèque");
    ImGui::Dummy(ImVec2(0, 4 * s));
}

void ControlPanelViews::DrawCardContextMenu(const LibraryItem& item) {
    if (!ImGui::BeginPopupContextItem("##card_menu")) {
        return;
    }
    ImGui::BeginDisabled(!item.supported);
    if (services_.Config().Data().layout == LayoutMode::Span) {
        if (ImGui::MenuItem("Appliquer (étendu sur tous les écrans)")) {
            Apply(item, "");
        }
    } else {
        if (ImGui::MenuItem("Appliquer à tous les écrans")) {
            Apply(item, "");
        }
        for (const auto& monitor : monitors_) {
            const std::string label = "Appliquer à " + U8(monitor.label);
            if (ImGui::MenuItem(label.c_str())) {
                Apply(item, monitor.info.id);
            }
        }
    }
    ImGui::EndDisabled();
    ImGui::Separator();
    if (ImGui::MenuItem("Ouvrir l'emplacement du fichier")) {
        OpenInShell(item.source.parent_path());
    }
    ImGui::EndPopup();
}

void ControlPanelViews::DrawLibraryGrid(const std::vector<const LibraryItem*>& items) {
    const float s = scale_;
    const float spacing = 14.0f * s;
    const float minCardWidth = 212.0f * s;
    const float rounding = 10.0f * s;
    const float availableWidth = ImGui::GetContentRegionAvail().x;
    const int columns = std::max(1, static_cast<int>((availableWidth + spacing) / (minCardWidth + spacing)));
    const float cardWidth = std::floor((availableWidth - spacing * (columns - 1)) / columns);
    const float imageHeight = std::floor(cardWidth * 9.0f / 16.0f);
    const float cardHeight = imageHeight + 60.0f * s;
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    for (size_t i = 0; i < items.size(); ++i) {
        const LibraryItem& item = *items[i];
        if (i % columns != 0) {
            ImGui::SameLine(0, spacing);
        } else if (i > 0) {
            ImGui::Dummy(ImVec2(0, spacing - ImGui::GetStyle().ItemSpacing.y));
        }
        ImGui::PushID(item.id.c_str());
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        const ImVec2 p1 = p0 + ImVec2(cardWidth, cardHeight);
        const ImVec2 imageMax(p1.x, p0.y + imageHeight);

        ImGui::InvisibleButton("##card", ImVec2(cardWidth, cardHeight));
        const bool hovered = ImGui::IsItemHovered();
        const bool visible = ImGui::IsItemVisible();
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            selectedId_ = item.id;
        }
        if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && item.supported) {
            Apply(item, services_.Config().Data().layout == LayoutMode::Span ? std::string() : targetMonitor_);
        }
        if (!item.description.empty() && ImGui::BeginItemTooltip()) {
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 22.0f);
            ImGui::TextUnformatted(U8(item.description).c_str());
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
        DrawCardContextMenu(item);

        if (visible) {
            drawList->AddRectFilled(p0, p1, ToU32(hovered ? color::kPanelRaised : color::kPanel), rounding);
            const ImTextureID texture = ThumbnailFor(item);
            if (HasTexture(texture)) {
                drawList->AddImageRounded(texture, p0, imageMax, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, rounding,
                                          ImDrawFlags_RoundCornersTop);
            } else {
                DrawPlaceholder(drawList, p0, imageMax, item.type, rounding, ImDrawFlags_RoundCornersTop);
            }

            ImGui::PushFont(fonts_.caption);
            if (!item.supported) {
                drawList->AddRectFilled(p0, imageMax, IM_COL32(0, 0, 0, 120), rounding, ImDrawFlags_RoundCornersTop);
                const char* soon = "Bientôt";
                const float soonWidth = ImGui::CalcTextSize(soon).x + 16 * s;
                DrawPill(drawList, ImVec2(imageMax.x - soonWidth - 8 * s, p0.y + 8 * s), soon,
                         ToU32(color::kWarning, 0.9f), IM_COL32(20, 16, 8, 255), 8 * s, 3 * s);
            }
            float badgeX = p0.x + 8 * s;
            for (const auto& screen : ScreensShowing(item)) {
                const std::string text = std::string(icon::kMonitor) + " " + screen;
                DrawPill(drawList, ImVec2(badgeX, p0.y + 8 * s), text.c_str(), ToU32(color::kAccent, 0.92f),
                         IM_COL32_WHITE, 8 * s, 3 * s);
                badgeX += ImGui::CalcTextSize(text.c_str()).x + 22 * s;
            }
            ImGui::PopFont();

            const std::string title = Ellipsize(U8(item.title), cardWidth - 24 * s);
            drawList->AddText(ImVec2(p0.x + 12 * s, imageMax.y + 9 * s), ToU32(color::kText), title.c_str());
            ImGui::PushFont(fonts_.caption);
            const std::string typeText = std::string(TypeIcon(item.type)) + "  " + TypeLabel(item.type);
            drawList->AddText(ImVec2(p0.x + 12 * s, imageMax.y + 33 * s), ToU32(TypeColor(item.type), 0.9f),
                              typeText.c_str());
            if (item.userImported) {
                const char* imported = "Importé";
                drawList->AddText(ImVec2(p1.x - ImGui::CalcTextSize(imported).x - 12 * s, imageMax.y + 33 * s),
                                  ToU32(color::kTextDim), imported);
            }
            ImGui::PopFont();

            if (item.id == selectedId_) {
                drawList->AddRect(p0, p1, ToU32(color::kAccent), rounding, 0, 2.5f * s);
            } else if (hovered) {
                drawList->AddRect(p0, p1, ToU32(color::kBorder), rounding, 0, 1.0f * s);
            }
        }
        ImGui::PopID();
    }
}

void ControlPanelViews::DrawPlaceholder(ImDrawList* drawList, ImVec2 min, ImVec2 max, WallpaperType type,
                                        float rounding, ImDrawFlags flags) const {
    drawList->AddRectFilled(min, max, ToU32(color::kFrame), rounding, flags);
    const char* glyph = TypeIcon(type);
    const float size = std::min(fonts_.glyphs->FontSize, (max.y - min.y) * 0.45f);
    const ImVec2 textSize = fonts_.glyphs->CalcTextSizeA(size, FLT_MAX, 0.0f, glyph);
    const ImVec2 center = (min + max) * 0.5f;
    drawList->AddText(fonts_.glyphs, size, center - textSize * 0.5f, ToU32(TypeColor(type), 0.55f), glyph);
}

// --------------------------------------------------------------------------------------------- //
//  Panneau de details : apercu en direct, application, reglages
// --------------------------------------------------------------------------------------------- //

void ControlPanelViews::EnsurePreview(const LibraryItem& item) {
    if (previewId_ == item.id) {
        return;
    }
    previewId_ = item.id;
    previewBackend_.reset();
    previewError_.clear();
    previewTime_ = 0.0;
    previewFrame_ = 0;
    // Un fond web a besoin d'une vraie fenetre (WebView2) : pas d'apercu hors ecran.
    if (!item.supported || item.type == WallpaperType::Web) {
        return;
    }
    RenderDevice& device = services_.Device();
    if (!previewTarget_ && !previewTarget_.Create(device.Device(), kPreviewWidth, kPreviewHeight)) {
        previewError_ = L"texture d'aperçu indisponible";
        return;
    }
    std::wstring error;
    auto backend = CreateWallpaperBackend(item.type, error);
    const WallpaperDesc desc = services_.Config().Resolve(item.Ref());
    const AudioSettings& audio = services_.Config().Data().audio;
    const BackendInitContext init{device, owner_, kPreviewWidth, kPreviewHeight, desc, audio.volume, audio.muted};
    if (backend && backend->Initialize(init, error)) {
        previewBackend_ = std::move(backend);
    } else {
        previewError_ = error;
    }
}

void ControlPanelViews::RenderPreview(float deltaSeconds, const MouseState& mouse) {
    if (!previewBackend_ || !previewTarget_) {
        return;
    }
    previewTime_ += deltaSeconds;
    FrameContext frame;
    frame.time = previewTime_;
    frame.deltaTime = deltaSeconds;
    frame.frameIndex = previewFrame_++;
    frame.frameRate = 60.0f;
    frame.width = kPreviewWidth;
    frame.height = kPreviewHeight;
    frame.mouse = mouse;
    services_.CurrentAudio(previewAudio_);  // l'apercu reagit a la musique comme le vrai fond
    frame.audio = &previewAudio_;
    previewBackend_->Render(previewTarget_.rtv.Get(), frame);
}

bool ControlPanelViews::DrawParameter(const ParamDescriptor& desc, ParamValue& value) {
    if (desc.kind == ParamKind::Bool) {
        bool enabled = false;
        if (const auto* b = std::get_if<bool>(&value)) {
            enabled = *b;
        } else if (const auto* f = std::get_if<float>(&value)) {
            enabled = *f > 0.5f;
        }
        if (ImGui::Checkbox(desc.label.c_str(), &enabled)) {
            value = enabled;
            return true;
        }
        return false;
    }

    ImGui::PushFont(fonts_.caption);
    ImGui::PushStyleColor(ImGuiCol_Text, color::kTextDim);
    ImGui::TextUnformatted(desc.label.c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::SetNextItemWidth(-FLT_MIN);

    if (desc.kind == ParamKind::Choice) {
        int index = 0;
        if (const auto* f = std::get_if<float>(&value)) {
            index = static_cast<int>(*f + 0.5f);
        }
        index = std::clamp(index, 0, std::max(0, static_cast<int>(desc.options.size()) - 1));
        bool changed = false;
        const char* current = desc.options.empty() ? "" : desc.options[static_cast<size_t>(index)].c_str();
        if (ImGui::BeginCombo("##choice", current)) {
            for (size_t i = 0; i < desc.options.size(); ++i) {
                if (ImGui::Selectable(desc.options[i].c_str(), static_cast<int>(i) == index)) {
                    value = static_cast<float>(i);
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        return changed;
    }

    if (desc.kind == ParamKind::Color) {
        Color colorValue = std::get_if<Color>(&value) ? std::get<Color>(value) : std::get<Color>(desc.defaultValue);
        if (ImGui::ColorEdit3("##color", colorValue.data(),
                              ImGuiColorEditFlags_DisplayHex | ImGuiColorEditFlags_PickerHueWheel)) {
            colorValue[3] = 1.0f;
            value = colorValue;
            return true;
        }
        return false;
    }

    float number = 0.0f;
    if (const auto* f = std::get_if<float>(&value)) {
        number = *f;
    } else if (const auto* fd = std::get_if<float>(&desc.defaultValue)) {
        number = *fd;
    }
    const char* format = (desc.maxValue - desc.minValue) >= 20.0f ? "%.0f" : "%.2f";
    if (ImGui::SliderFloat("##value", &number, desc.minValue, desc.maxValue, format)) {
        value = number;
        return true;
    }
    return false;
}

void ControlPanelViews::DrawParameters(const LibraryItem& item) {
    // Source des reglages : l'apercu, ou a defaut le fond affiche (fonds web, sans apercu hors ecran).
    IWallpaperBackend* backend = previewBackend_ ? previewBackend_.get() : services_.LiveBackendFor(item);
    if (!backend) {
        if (!previewError_.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, color::kWarning);
            ImGui::TextWrapped("%s  %s", icon::kWarning, U8(previewError_).c_str());
            ImGui::PopStyleColor();
        } else if (item.type == WallpaperType::Web && item.supported) {
            DimText("Les réglages de ce fond web apparaissent ici une fois qu'il est affiché sur un écran.");
        }
        return;
    }
    const std::vector<ParamDescriptor> descriptors = backend->Parameters();
    if (descriptors.empty()) {
        return;
    }
    ImGui::SeparatorText("Réglages");
    const ParamMap values = backend->ParameterValues();
    for (const auto& desc : descriptors) {
        ImGui::PushID(desc.name.c_str());
        ParamValue value = desc.defaultValue;
        if (const auto it = values.find(desc.name); it != values.end()) {
            value = it->second;
        }
        if (DrawParameter(desc, value)) {
            if (previewBackend_) {
                previewBackend_->SetParameter(desc.name, value);
            }
            services_.SetWallpaperParameter(item, desc.name, value);
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            InvalidateThumbnail(item.id);
        }
        ImGui::PopID();
    }
    ImGui::Spacing();
    const std::string resetLabel = WithIcon(icon::kReset, "Réinitialiser les réglages");
    if (ImGui::Button(resetLabel.c_str(), ImVec2(-FLT_MIN, 0))) {
        services_.ResetWallpaperParameters(item);
        previewId_.clear();
        InvalidateThumbnail(item.id);
        Toast("Réglages réinitialisés");
    }
}

void ControlPanelViews::DrawDetails(float deltaSeconds) {
    const float s = scale_;
    const LibraryItem* item = SelectedItem();
    if (!item) {
        DimText("Sélectionnez un fond pour voir son aperçu en direct et ses réglages.");
        return;
    }
    EnsurePreview(*item);

    // Apercu interactif : la souris au-dessus de l'apercu pilote iMouse / la parallaxe.
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = std::floor(width * 9.0f / 16.0f);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 p1 = p0 + ImVec2(width, height);
    ImGui::InvisibleButton("##preview", ImVec2(width, height));
    const bool hovered = ImGui::IsItemHovered();
    if (hovered) {
        const ImVec2 local = ImGui::GetIO().MousePos - p0;
        const float u = std::clamp(local.x / width, 0.0f, 1.0f);
        const float v = std::clamp(local.y / height, 0.0f, 1.0f);
        previewMouse_.inside = true;
        previewMouse_.x = u * kPreviewWidth;
        previewMouse_.y = (1.0f - v) * kPreviewHeight;
        previewMouse_.normX = u * 2.0f - 1.0f;
        previewMouse_.normY = 1.0f - v * 2.0f;
        const bool down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
        if (down && !previewMouse_.pressed) {
            previewMouse_.clickX = previewMouse_.x;
            previewMouse_.clickY = previewMouse_.y;
        }
        previewMouse_.pressed = down;
    } else {
        previewMouse_.inside = false;
        previewMouse_.pressed = false;
        previewMouse_.normX *= 0.9f;
        previewMouse_.normY *= 0.9f;
    }
    const float blend = 1.0f - std::exp(-deltaSeconds * 6.0f);
    previewMouse_.parallaxX += (previewMouse_.normX - previewMouse_.parallaxX) * blend;
    previewMouse_.parallaxY += (previewMouse_.normY - previewMouse_.parallaxY) * blend;
    RenderPreview(deltaSeconds, previewMouse_);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const float rounding = 10 * s;
    if (previewBackend_) {
        drawList->AddImageRounded(ToTextureId(previewTarget_.srv.Get()), p0, p1, ImVec2(0, 0), ImVec2(1, 1),
                                  IM_COL32_WHITE, rounding);
        ImGui::PushFont(fonts_.caption);
        DrawPill(drawList, p0 + ImVec2(8 * s, height - 26 * s), "●  Aperçu en direct", IM_COL32(0, 0, 0, 150),
                 ToU32(color::kSuccess), 8 * s, 3 * s);
        ImGui::PopFont();
    } else if (const ImTextureID texture = ThumbnailFor(*item); HasTexture(texture)) {
        drawList->AddImageRounded(texture, p0, p1, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, rounding);
    } else {
        DrawPlaceholder(drawList, p0, p1, item->type, rounding, ImDrawFlags_RoundCornersAll);
    }

    ImGui::Spacing();
    ImGui::PushFont(fonts_.heading);
    ImGui::TextWrapped("%s", U8(item->title).c_str());
    ImGui::PopFont();
    ImGui::PushFont(fonts_.caption);
    ImGui::PushStyleColor(ImGuiCol_Text, TypeColor(item->type));
    ImGui::Text("%s  %s", TypeIcon(item->type), TypeLabel(item->type));
    ImGui::PopStyleColor();
    if (!item->author.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("·  %s", U8(item->author).c_str());
    }
    ImGui::PopFont();
    if (!item->description.empty()) {
        DimText(U8(item->description).c_str());
    }
    ImGui::Spacing();

    if (!item->supported) {
        ImGui::PushStyleColor(ImGuiCol_Text, color::kWarning);
        if (item->type == WallpaperType::Web) {
            ImGui::TextWrapped("%s  Les fonds web nécessitent le runtime Microsoft Edge WebView2, introuvable sur ce PC.",
                               icon::kWarning);
        } else {
            ImGui::TextWrapped("%s  Ce type de fond n'est pas disponible dans cette version.", icon::kWarning);
        }
        ImGui::PopStyleColor();
        ImGui::Spacing();
    }

    // Ecran cible
    const bool span = services_.Config().Data().layout == LayoutMode::Span;
    if (span) {
        targetMonitor_.clear();
        DimText("Mode étendu : le fond couvre tous les écrans.");
    } else {
        ImGui::PushFont(fonts_.caption);
        ImGui::TextDisabled("Afficher sur");
        ImGui::PopFont();
        std::string current = "Tous les écrans";
        for (const auto& monitor : monitors_) {
            if (monitor.info.id == targetMonitor_) {
                current = U8(monitor.label);
            }
        }
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##target", current.c_str())) {
            if (ImGui::Selectable("Tous les écrans", targetMonitor_.empty())) {
                targetMonitor_.clear();
            }
            for (const auto& monitor : monitors_) {
                if (ImGui::Selectable(U8(monitor.label).c_str(), monitor.info.id == targetMonitor_)) {
                    targetMonitor_ = monitor.info.id;
                }
            }
            ImGui::EndCombo();
        }
    }

    ImGui::BeginDisabled(!item->supported);
    const std::string applyLabel = WithIcon(icon::kCheck, "Appliquer le fond");
    if (AccentButton(applyLabel.c_str(), ImVec2(-FLT_MIN, 40 * s))) {
        Apply(*item, targetMonitor_);
    }
    ImGui::EndDisabled();

    const std::vector<std::string> screens = ScreensShowing(*item);
    if (!screens.empty()) {
        std::string shown;
        for (const auto& screen : screens) {
            shown += (shown.empty() ? "" : ", ") + (screen == "Tous" ? std::string("tous les écrans") : "écran " + screen);
        }
        ImGui::PushFont(fonts_.caption);
        ImGui::PushStyleColor(ImGuiCol_Text, color::kSuccess);
        ImGui::TextWrapped("%s  Affiché sur : %s", icon::kCheck, shown.c_str());
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }

    ImGui::Spacing();
    DrawParameters(*item);
}

// --------------------------------------------------------------------------------------------- //
//  Miniatures
// --------------------------------------------------------------------------------------------- //

ImTextureID ControlPanelViews::ThumbnailFor(const LibraryItem& item) {
    Thumbnail& thumbnail = thumbnails_[item.id];
    if (!thumbnail.attempted &&
        std::find(pendingThumbnails_.begin(), pendingThumbnails_.end(), item.id) == pendingThumbnails_.end()) {
        pendingThumbnails_.push_back(item.id);
    }
    if (thumbnail.rendered) {
        return ToTextureId(thumbnail.rendered.srv.Get());
    }
    if (thumbnail.image) {
        return ToTextureId(thumbnail.image.view.Get());
    }
    return ImTextureID{};
}

void ControlPanelViews::InvalidateThumbnail(const std::string& id) {
    if (const auto it = thumbnails_.find(id); it != thumbnails_.end()) {
        it->second.attempted = false;
    }
}

// Une miniature par image au plus : la compilation d'un shader inconnu prend quelques centaines de ms,
// on etale donc ce cout plutot que de figer l'interface a l'ouverture.
void ControlPanelViews::GeneratePendingThumbnail() {
    while (!pendingThumbnails_.empty()) {
        const std::string id = pendingThumbnails_.front();
        pendingThumbnails_.erase(pendingThumbnails_.begin());
        const LibraryItem* item = services_.GetLibrary().FindById(id);
        if (!item) {
            continue;
        }
        Thumbnail& slot = thumbnails_[id];
        if (slot.attempted) {
            continue;
        }
        // Nouvelle miniature construite a part ; l'ancienne est mise de cote (et non detruite) car
        // l'image precedente a pu la referencer.
        Thumbnail fresh = BuildThumbnail(*item);
        fresh.attempted = true;
        retiredThumbnails_.push_back(std::move(slot));
        slot = std::move(fresh);
        return;
    }
}

ControlPanelViews::Thumbnail ControlPanelViews::BuildThumbnail(const LibraryItem& item) {
    Thumbnail thumbnail;
    RenderDevice& device = services_.Device();
    if (!item.previewImage.empty()) {
        thumbnail.image = LoadTextureFromFile(device.Device(), item.previewImage, 512);
        if (thumbnail.image) {
            return thumbnail;
        }
    }
    if (item.type == WallpaperType::Scene) {
        thumbnail.image = LoadTextureFromFile(device.Device(), item.source, 512);
        return thumbnail;
    }
    if (item.type == WallpaperType::Video) {
        thumbnail.image = LoadVideoThumbnail(device.Device(), item.source, 512);
        return thumbnail;
    }
    if (!item.supported || item.type != WallpaperType::Shader ||
        !thumbnail.rendered.Create(device.Device(), kThumbWidth, kThumbHeight)) {
        return thumbnail;
    }
    std::wstring error;
    auto backend = CreateWallpaperBackend(item.type, error);
    const WallpaperDesc desc = services_.Config().Resolve(item.Ref());
    const BackendInitContext init{device, owner_, kThumbWidth, kThumbHeight, desc};
    if (!backend || !backend->Initialize(init, error)) {
        thumbnail.rendered.Reset();
        return thumbnail;
    }
    FrameContext frame;
    frame.time = 4.0;
    frame.deltaTime = 4.0f;  // instantane a t = 4 s : les animations ont eu le temps de demarrer
    frame.frameRate = 60.0f;
    frame.width = kThumbWidth;
    frame.height = kThumbHeight;
    frame.mouse.x = kThumbWidth * 0.5f;
    frame.mouse.y = kThumbHeight * 0.5f;
    frame.audio = &silence_;
    backend->Render(thumbnail.rendered.rtv.Get(), frame);
    return thumbnail;
}

// --------------------------------------------------------------------------------------------- //
//  Ecrans
// --------------------------------------------------------------------------------------------- //

void ControlPanelViews::DrawScreens() {
    const float s = scale_;
    const AppConfig& config = services_.Config().Data();
    const bool span = config.layout == LayoutMode::Span;

    ImGui::PushFont(fonts_.heading);
    ImGui::TextUnformatted("Disposition des écrans");
    ImGui::PopFont();
    DimText("Choisissez un fond différent pour chaque écran, ou un seul fond étendu sur l'ensemble de vos écrans.");
    ImGui::Spacing();
    if (PillButton(WithIcon(icon::kMonitor, "Un fond par écran").c_str(), !span)) {
        services_.SetLayout(LayoutMode::PerMonitor);
    }
    ImGui::SameLine();
    if (PillButton(WithIcon(icon::kSpan, "Étendre sur tous les écrans").c_str(), span)) {
        services_.SetLayout(LayoutMode::Span);
    }
    ImGui::Spacing();
    ImGui::Spacing();

    if (monitors_.empty()) {
        DimText("Aucun écran détecté.");
        return;
    }
    if (selectedMonitor_.empty() ||
        std::none_of(monitors_.begin(), monitors_.end(),
                     [&](const MonitorEntry& m) { return m.info.id == selectedMonitor_; })) {
        selectedMonitor_ = monitors_.front().info.id;
    }

    // Schema des ecrans, a l'echelle, avec le fond de chacun.
    RECT unionRect = monitors_.front().info.bounds;
    for (const auto& monitor : monitors_) {
        UnionRect(&unionRect, &unionRect, &monitor.info.bounds);
    }
    const float unionWidth = static_cast<float>(unionRect.right - unionRect.left);
    const float unionHeight = static_cast<float>(unionRect.bottom - unionRect.top);
    const float canvasWidth = ImGui::GetContentRegionAvail().x;
    const float canvasHeight = 320.0f * s;
    const float margin = 28.0f * s;
    const float scale = std::min((canvasWidth - margin * 2) / unionWidth, (canvasHeight - margin * 2) / unionHeight);
    const ImVec2 canvas = ImGui::GetCursorScreenPos();
    const ImVec2 offset(canvas.x + (canvasWidth - unionWidth * scale) * 0.5f,
                        canvas.y + (canvasHeight - unionHeight * scale) * 0.5f);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(canvas, canvas + ImVec2(canvasWidth, canvasHeight), ToU32(color::kPanel), 12 * s);

    const Library& library = services_.GetLibrary();
    for (const auto& monitor : monitors_) {
        const RECT& b = monitor.info.bounds;
        const float gap = 4.0f * s;
        const ImVec2 a(offset.x + (b.left - unionRect.left) * scale + gap, offset.y + (b.top - unionRect.top) * scale + gap);
        const ImVec2 z(offset.x + (b.right - unionRect.left) * scale - gap,
                       offset.y + (b.bottom - unionRect.top) * scale - gap);
        ImGui::SetCursorScreenPos(a);
        ImGui::PushID(monitor.info.id.c_str());
        ImGui::InvisibleButton("##monitor", z - a);
        const bool hovered = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked()) {
            selectedMonitor_ = monitor.info.id;
        }
        ImGui::PopID();

        const SurfaceStatusInfo* surface = nullptr;
        for (const auto& candidate : surfaces_) {
            if (candidate.monitorId == monitor.info.id || (span && candidate.monitorId == kSpanMonitorId)) {
                surface = &candidate;
            }
        }
        ImVec2 uv0(0, 0);
        ImVec2 uv1(1, 1);
        if (span) {
            uv0 = ImVec2((b.left - unionRect.left) / unionWidth, (b.top - unionRect.top) / unionHeight);
            uv1 = ImVec2((b.right - unionRect.left) / unionWidth, (b.bottom - unionRect.top) / unionHeight);
        }
        const LibraryItem* item = surface ? library.FindBySource(surface->source) : nullptr;
        const ImTextureID texture = item ? ThumbnailFor(*item) : ImTextureID{};
        if (HasTexture(texture)) {
            drawList->AddImageRounded(texture, a, z, uv0, uv1, IM_COL32_WHITE, 8 * s);
        } else {
            drawList->AddRectFilled(a, z, ToU32(color::kFrame), 8 * s);
        }
        drawList->AddRectFilled(a, ImVec2(z.x, a.y + 36 * s), IM_COL32(0, 0, 0, 130), 8 * s, ImDrawFlags_RoundCornersTop);
        ImGui::PushFont(fonts_.heading);
        drawList->AddText(a + ImVec2(10 * s, 5 * s), IM_COL32_WHITE, std::to_string(monitor.number).c_str());
        ImGui::PopFont();
        ImGui::PushFont(fonts_.caption);
        const std::string name = Ellipsize(U8(monitor.info.friendlyName), z.x - a.x - 48 * s);
        drawList->AddText(a + ImVec2(34 * s, 10 * s), ToU32(color::kText, 0.85f), name.c_str());
        if (surface && surface->paused) {
            DrawPill(drawList, ImVec2(a.x + 10 * s, z.y - 28 * s), surface->autoPaused ? "En pause (plein écran)" : "En pause",
                     ToU32(color::kWarning, 0.9f), IM_COL32(20, 16, 8, 255), 8 * s, 3 * s);
        }
        ImGui::PopFont();
        const bool selected = monitor.info.id == selectedMonitor_;
        drawList->AddRect(a, z, selected ? ToU32(color::kAccent) : ToU32(hovered ? color::kAccent2 : color::kBorder, hovered ? 0.8f : 1.0f),
                          8 * s, 0, (selected ? 3.0f : 1.5f) * s);
    }
    ImGui::SetCursorScreenPos(canvas + ImVec2(0, canvasHeight + 16 * s));
    ImGui::Dummy(ImVec2(canvasWidth, 0));

    // Details de l'ecran selectionne
    const MonitorEntry* selected = nullptr;
    for (const auto& monitor : monitors_) {
        if (monitor.info.id == selectedMonitor_) {
            selected = &monitor;
        }
    }
    if (!selected) {
        return;
    }
    const SurfaceStatusInfo* surface = nullptr;
    for (const auto& candidate : surfaces_) {
        if (candidate.monitorId == selected->info.id || (span && candidate.monitorId == kSpanMonitorId)) {
            surface = &candidate;
        }
    }
    ImGui::PushStyleColor(ImGuiCol_ChildBg, color::kPanel);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18 * s, 16 * s));
    ImGui::BeginChild("##monitor_details", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeY);
    ImGui::PushFont(fonts_.heading);
    ImGui::TextUnformatted(U8(selected->label).c_str());
    ImGui::PopFont();
    const MonitorInfo& info = selected->info;
    ImGui::TextDisabled("%d × %d px  ·  %u ppp (%.0f %%)  ·  position (%ld, %ld)%s", info.Width(), info.Height(),
                        info.dpi, info.dpi / 96.0f * 100.0f, info.bounds.left, info.bounds.top,
                        info.primary ? "  ·  écran principal" : "");
    const LibraryItem* item = surface ? library.FindBySource(surface->source) : nullptr;
    const std::string current = item ? U8(item->title) : (surface ? std::string("Shader de secours") : std::string("—"));
    ImGui::Text("Fond actuel : %s", current.c_str());
    if (surface && surface->paused) {
        ImGui::PushStyleColor(ImGuiCol_Text, color::kWarning);
        ImGui::TextUnformatted(surface->autoPaused ? "En pause : une application plein écran recouvre cet écran."
                                                   : "En pause");
        ImGui::PopStyleColor();
    }
    ImGui::Spacing();
    const std::string changeLabel = WithIcon(icon::kLibrary, span ? "Changer le fond étendu" : "Changer le fond de cet écran");
    if (AccentButton(changeLabel.c_str())) {
        targetMonitor_ = span ? std::string() : selected->info.id;
        tab_ = Tab::Library;
        Toast(span ? std::string("Choisissez le fond à étendre sur tous les écrans")
                   : std::format("Choisissez un fond pour l'écran {}", selected->number));
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

// --------------------------------------------------------------------------------------------- //
//  Playlist
// --------------------------------------------------------------------------------------------- //

void ControlPanelViews::DrawPlaylist() {
    const float s = scale_;
    AppConfig& config = services_.Config().Data();
    PlaylistSettings& playlist = config.playlist;
    bool changed = false;
    bool timingChanged = false;

    ImGui::BeginChild("##playlist", ImVec2(std::min(820.0f * s, ImGui::GetContentRegionAvail().x), 0));
    ImGui::PushFont(fonts_.heading);
    ImGui::TextUnformatted("Playlist");
    ImGui::PopFont();
    DimText("Fait défiler automatiquement vos fonds d'écran préférés, sur chaque écran.");
    ImGui::Spacing();

    if (ImGui::Checkbox("Activer la rotation automatique", &playlist.enabled)) {
        changed = timingChanged = true;
    }
    ImGui::BeginDisabled(!playlist.enabled);
    int interval = static_cast<int>(playlist.intervalMinutes);
    ImGui::SetNextItemWidth(340 * s);
    if (ImGui::SliderInt("Intervalle", &interval, 1, 240, "%d min")) {
        playlist.intervalMinutes = static_cast<uint32_t>(interval);
        changed = timingChanged = true;
    }
    changed |= ImGui::Checkbox(WithIcon(icon::kShuffle, "Ordre aléatoire").c_str(), &playlist.shuffle);
    changed |= ImGui::Checkbox("Changer de fond à chaque démarrage", &playlist.advanceOnStartup);
    changed |= ImGui::Checkbox("Transition propre à la playlist", &playlist.customTransition);
    if (playlist.customTransition) {
        ImGui::Indent();
        changed |= DrawTransitionSettings(playlist.transition, "playlist");
        ImGui::Unindent();
    } else {
        DimText("La transition réglée dans Paramètres est utilisée.");
    }
    if (playlist.enabled) {
        const double minutes = services_.MinutesUntilPlaylistChange();
        ImGui::PushStyleColor(ImGuiCol_Text, color::kAccent2);
        ImGui::Text("%s  Prochain changement dans %.0f min", icon::kTimer, std::ceil(std::max(0.0, minutes)));
        ImGui::PopStyleColor();
    }
    ImGui::EndDisabled();
    ImGui::Spacing();
    if (ImGui::Button(WithIcon(icon::kNext, "Fond suivant maintenant").c_str())) {
        services_.NextWallpaper();
        services_.RestartPlaylistTimer();
        Toast("Fond suivant");
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Fonds inclus");
    DimText("Aucune sélection = toute la bibliothèque est utilisée.");
    std::vector<std::string> selectedKeys;
    for (const auto& path : playlist.items) {
        selectedKeys.push_back(WallpaperSettingsKey(path));
    }
    if (ImGui::SmallButton("Tout sélectionner")) {
        playlist.items.clear();
        for (const auto& item : services_.GetLibrary().Items()) {
            if (item.supported) {
                playlist.items.push_back(item.Ref().source);
            }
        }
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Tout désélectionner")) {
        playlist.items.clear();
        changed = true;
    }
    for (const auto& item : services_.GetLibrary().Items()) {
        if (!item.supported) {
            continue;
        }
        bool included = std::find(selectedKeys.begin(), selectedKeys.end(), item.id) != selectedKeys.end();
        const std::string label = U8(item.title) + "##" + item.id;
        if (ImGui::Checkbox(label.c_str(), &included)) {
            if (included) {
                playlist.items.push_back(item.Ref().source);
            } else {
                std::erase_if(playlist.items,
                              [&](const std::filesystem::path& path) { return WallpaperSettingsKey(path) == item.id; });
            }
            changed = true;
        }
    }
    ImGui::EndChild();

    if (changed) {
        services_.Config().MarkDirty();
    }
    if (timingChanged) {
        services_.RestartPlaylistTimer();
    }
}

// --------------------------------------------------------------------------------------------- //
//  Raccourcis clavier
// --------------------------------------------------------------------------------------------- //

bool ControlPanelViews::OnKeyDown(unsigned int virtualKey) {
    if (capturingHotkey_ < 0) {
        return false;
    }
    switch (virtualKey) {
    case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL:
    case VK_MENU: case VK_LMENU: case VK_RMENU:
    case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT:
    case VK_LWIN: case VK_RWIN:
        return true;  // on attend la touche principale
    default:
        break;
    }
    HotkeySettings& hotkeys = services_.Config().Data().hotkeys;
    const size_t action = static_cast<size_t>(capturingHotkey_);
    if (virtualKey == VK_ESCAPE) {
        capturingHotkey_ = -1;
        services_.SuspendHotkeys(false);
        return true;
    }
    if (virtualKey == VK_BACK) {
        hotkeys.keys[action] = Hotkey{};
    } else {
        Hotkey hotkey;
        hotkey.key = virtualKey;
        if (GetKeyState(VK_CONTROL) < 0) hotkey.modifiers |= MOD_CONTROL;
        if (GetKeyState(VK_MENU) < 0) hotkey.modifiers |= MOD_ALT;
        if (GetKeyState(VK_SHIFT) < 0) hotkey.modifiers |= MOD_SHIFT;
        if (GetKeyState(VK_LWIN) < 0 || GetKeyState(VK_RWIN) < 0) hotkey.modifiers |= MOD_WIN;
        // Sans modificateur, seules les touches qui ne servent pas a taper du texte sont acceptees.
        const bool standalone = (virtualKey >= VK_F13 && virtualKey <= VK_F24) || virtualKey == VK_PAUSE ||
                                (virtualKey >= VK_VOLUME_MUTE && virtualKey <= VK_MEDIA_PLAY_PAUSE);
        if (hotkey.modifiers == 0 && !standalone) {
            Toast("Ajoutez Ctrl, Alt, Maj ou Win à la combinaison");
            return true;
        }
        hotkeys.keys[action] = hotkey;
    }
    capturingHotkey_ = -1;
    services_.ApplyHotkeys();  // reenregistre tout (fin de la suspension)
    return true;
}

void ControlPanelViews::DrawHotkeys() {
    const float s = scale_;
    HotkeySettings& hotkeys = services_.Config().Data().hotkeys;
    static constexpr const char* kNames[kHotkeyActionCount] = {"Fond suivant", "Fond précédent", "Pause / reprise",
                                                               "Muet", "Afficher / masquer la fenêtre"};
    if (ImGui::Checkbox("Raccourcis clavier globaux", &hotkeys.enabled)) {
        services_.ApplyHotkeys();
    }
    ImGui::BeginDisabled(!hotkeys.enabled);
    for (size_t i = 0; i < kHotkeyActionCount; ++i) {
        ImGui::PushID(static_cast<int>(i));
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(kNames[i]);
        ImGui::SameLine(250 * s);
        const bool capturing = capturingHotkey_ == static_cast<int>(i);
        const std::string label = capturing ? std::string("Appuyez sur une combinaison…")
                                            : FormatHotkey(hotkeys.keys[i], true);
        if (capturing) {
            ImGui::PushStyleColor(ImGuiCol_Button, ToU32(color::kAccent, 0.45f));
        }
        if (ImGui::Button(label.c_str(), ImVec2(290 * s, 0))) {
            if (capturingHotkey_ < 0) {
                capturingHotkey_ = static_cast<int>(i);
                services_.SuspendHotkeys(true);  // sinon la combinaison deja prise n'arriverait jamais ici
            }
        }
        if (capturing) {
            ImGui::PopStyleColor();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Effacer") && !hotkeys.keys[i].Empty()) {
            hotkeys.keys[i] = Hotkey{};
            services_.ApplyHotkeys();
        }
        // Etat : doublon interne, combinaison prise par une autre application, actif.
        ImGui::SameLine();
        std::string duplicateOf;
        for (size_t j = 0; j < kHotkeyActionCount; ++j) {
            if (j != i && !hotkeys.keys[i].Empty() && hotkeys.keys[j] == hotkeys.keys[i]) {
                duplicateOf = kNames[j];
            }
        }
        if (!capturing && !duplicateOf.empty()) {
            ImGui::TextColored(color::kWarning, "%s  même raccourci que « %s »", icon::kWarning, duplicateOf.c_str());
        } else if (!capturing && services_.HotkeyState(static_cast<HotkeyAction>(i)) == HotkeyStatus::Conflict) {
            ImGui::TextColored(color::kWarning, "%s  déjà utilisé par une autre application", icon::kWarning);
        } else if (!capturing && services_.HotkeyState(static_cast<HotkeyAction>(i)) == HotkeyStatus::Registered) {
            ImGui::TextDisabled("%s", icon::kCheck);
        }
        ImGui::PopID();
    }
    if (ImGui::SmallButton("Raccourcis par défaut")) {
        hotkeys.keys = HotkeySettings::DefaultHotkeys();
        services_.ApplyHotkeys();
    }
    ImGui::EndDisabled();
    DimText(capturingHotkey_ >= 0 ? "Échap : annuler · Retour arrière : aucun raccourci"
                                  : "Cliquez sur un raccourci pour le modifier. Ils fonctionnent même fenêtre fermée.");
}

// --------------------------------------------------------------------------------------------- //
//  Automatisation : planification et profils
// --------------------------------------------------------------------------------------------- //

bool ControlPanelViews::WallpaperCombo(const char* id, WallpaperRef& ref) {
    const std::string key = ref.Empty() ? std::string() : WallpaperSettingsKey(ref.source);
    std::string preview = ref.Empty() ? "Aucun" : U8(ref.source.filename().wstring());
    for (const auto& item : services_.GetLibrary().Items()) {
        if (item.id == key) {
            preview = U8(item.title);
        }
    }
    bool changed = false;
    ImGui::SetNextItemWidth(300 * scale_);
    if (ImGui::BeginCombo(id, preview.c_str())) {
        if (ImGui::Selectable("Aucun", ref.Empty())) {
            ref = WallpaperRef{};
            changed = true;
        }
        for (const auto& item : services_.GetLibrary().Items()) {
            if (!item.supported) {
                continue;
            }
            const std::string label = U8(item.title) + "##" + item.id;
            if (ImGui::Selectable(label.c_str(), item.id == key)) {
                ref = item.Ref();
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

void ControlPanelViews::DrawAutomation() {
    ImGui::BeginChild("##automation", ImVec2(std::min(880.0f * scale_, ImGui::GetContentRegionAvail().x), 0));
    DrawSchedule();
    ImGui::Spacing();
    ImGui::Spacing();
    DrawProfiles();
    ImGui::EndChild();
}

void ControlPanelViews::DrawSchedule() {
    const float s = scale_;
    ScheduleSettings& schedule = services_.Config().Data().schedule;
    bool changed = false;
    ImGui::PushFont(fonts_.heading);
    ImGui::TextUnformatted("Planification");
    ImGui::PopFont();
    DimText("Change de fond selon l'heure, le soleil ou le thème de Windows (prioritaire sur la playlist).");
    ImGui::Spacing();
    changed |= ImGui::Checkbox("Activer la planification", &schedule.enabled);
    ImGui::SameLine();
    ImGui::TextDisabled("%s", U8(services_.ScheduleStatus()).c_str());
    ImGui::BeginDisabled(!schedule.enabled);
    int mode = static_cast<int>(schedule.mode);
    changed |= ImGui::RadioButton("Plages horaires", &mode, 0);
    ImGui::SameLine();
    changed |= ImGui::RadioButton("Lever et coucher du soleil", &mode, 1);
    ImGui::SameLine();
    changed |= ImGui::RadioButton("Thème clair / sombre de Windows", &mode, 2);
    schedule.mode = static_cast<ScheduleMode>(mode);
    ImGui::Spacing();

    switch (schedule.mode) {
    case ScheduleMode::TimeSlots: {
        int removed = -1;
        for (size_t i = 0; i < schedule.slots.size(); ++i) {
            TimeSlot& slot = schedule.slots[i];
            ImGui::PushID(static_cast<int>(i));
            // Pas de 15 minutes ; la fin peut valoir 24:00.
            int start = static_cast<int>(slot.startMinutes / 15);
            int end = static_cast<int>(slot.endMinutes / 15);
            ImGui::SetNextItemWidth(120 * s);
            if (ImGui::SliderInt("##start", &start, 0, 95, FormatMinutes(start * 15.0).c_str())) {
                slot.startMinutes = static_cast<uint32_t>(start * 15);
            }
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::SameLine();
            ImGui::TextUnformatted("→");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120 * s);
            if (ImGui::SliderInt("##end", &end, 0, 96, FormatMinutes(end * 15.0).c_str())) {
                slot.endMinutes = static_cast<uint32_t>(end * 15);
            }
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::SameLine();
            changed |= WallpaperCombo("##slot_wallpaper", slot.wallpaper);
            ImGui::SameLine();
            if (ImGui::SmallButton(icon::kDelete)) {
                removed = static_cast<int>(i);
            }
            ImGui::PopID();
        }
        if (removed >= 0) {
            schedule.slots.erase(schedule.slots.begin() + removed);
            changed = true;
        }
        if (ImGui::Button(WithIcon(icon::kAdd, "Ajouter une plage").c_str())) {
            TimeSlot slot;
            if (!schedule.slots.empty()) {
                slot.startMinutes = schedule.slots.back().endMinutes % 1440;
                slot.endMinutes = (slot.startMinutes + 12 * 60) % 1440;
            }
            schedule.slots.push_back(slot);
            changed = true;
        }
        DimText("Une plage dont la fin précède le début passe minuit (ex. 22:00 → 06:00). Hors de toute plage, "
                "le fond reste celui choisi.");
        break;
    }
    case ScheduleMode::Sun:
        ImGui::SetNextItemWidth(160 * s);
        ImGui::InputDouble("Latitude", &schedule.latitude, 0.0, 0.0, "%.4f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SameLine();
        ImGui::SetNextItemWidth(160 * s);
        ImGui::InputDouble("Longitude", &schedule.longitude, 0.0, 0.0, "%.4f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        DimText("En degrés décimaux, nord et est positifs (ex. Paris 48.8566 / 2.3522). Calcul local (NOAA), "
                "aucune géolocalisation.");
        ImGui::SetNextItemWidth(200 * s);
        ImGui::SliderInt("Décalage du lever", &schedule.sunriseOffsetMinutes, -120, 120, "%d min");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SetNextItemWidth(200 * s);
        ImGui::SliderInt("Décalage du coucher", &schedule.sunsetOffsetMinutes, -120, 120, "%d min");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Fond de jour");
        ImGui::SameLine(150 * s);
        changed |= WallpaperCombo("##day", schedule.dayWallpaper);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Fond de nuit");
        ImGui::SameLine(150 * s);
        changed |= WallpaperCombo("##night", schedule.nightWallpaper);
        break;
    case ScheduleMode::Theme:
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Thème clair");
        ImGui::SameLine(150 * s);
        changed |= WallpaperCombo("##light", schedule.lightWallpaper);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Thème sombre");
        ImGui::SameLine(150 * s);
        changed |= WallpaperCombo("##dark", schedule.darkWallpaper);
        DimText("Suit Paramètres Windows › Personnalisation › Couleurs › Mode des applications, en direct.");
        break;
    }
    ImGui::EndDisabled();
    if (changed) {
        services_.RefreshAutomation();
    }
}

void ControlPanelViews::DrawProfiles() {
    const float s = scale_;
    ProfileSettings& settings = services_.Config().Data().profiles;
    bool changed = false;
    ImGui::PushFont(fonts_.heading);
    ImGui::TextUnformatted("Profils");
    ImGui::PopFont();
    DimText("Un profil regroupe images par seconde, qualité, règles de pause et volume.");
    ImGui::Spacing();

    const auto profileCombo = [&](const char* id, std::string& name, const char* none) {
        bool picked = false;
        ImGui::SetNextItemWidth(220 * s);
        if (ImGui::BeginCombo(id, name.empty() ? none : name.c_str())) {
            if (ImGui::Selectable(none, name.empty())) {
                name.clear();
                picked = true;
            }
            for (const auto& profile : settings.profiles) {
                if (ImGui::Selectable(profile.name.c_str(), profile.name == name)) {
                    name = profile.name;
                    picked = true;
                }
            }
            ImGui::EndCombo();
        }
        return picked;
    };

    changed |= profileCombo("Profil choisi##active_profile", settings.active, "Aucun (réglages de base)");
    const std::string& applied = services_.Effective().profile;
    ImGui::SameLine();
    ImGui::TextDisabled("Appliqué : %s", applied.empty() ? "aucun" : applied.c_str());
    changed |= profileCombo("Sur batterie##battery_profile", settings.batteryProfile, "Aucun");

    ImGui::Spacing();
    changed |= ImGui::Checkbox("Bascule automatique selon l'application au premier plan", &settings.autoSwitch);
    ImGui::BeginDisabled(!settings.autoSwitch);
    int removedRule = -1;
    for (size_t i = 0; i < settings.rules.size(); ++i) {
        ProfileRule& rule = settings.rules[i];
        ImGui::PushID(static_cast<int>(i) + 1000);
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%s", rule.executable.c_str());
        ImGui::SameLine(260 * s);
        ImGui::TextDisabled("→");
        ImGui::SameLine();
        changed |= profileCombo("##rule_profile", rule.profile, "Aucun");
        ImGui::SameLine();
        if (ImGui::SmallButton(icon::kDelete)) {
            removedRule = static_cast<int>(i);
        }
        ImGui::PopID();
    }
    if (removedRule >= 0) {
        settings.rules.erase(settings.rules.begin() + removedRule);
        changed = true;
    }
    ImGui::SetNextItemWidth(220 * s);
    ImGui::InputTextWithHint("##rule_exe", "jeu.exe", ruleExecutable_, sizeof(ruleExecutable_));
    ImGui::SameLine();
    if (ImGui::Button(WithIcon(icon::kAdd, "Ajouter").c_str()) && ruleExecutable_[0] != '\0') {
        settings.rules.push_back(ProfileRule{ruleExecutable_, settings.profiles.empty() ? "" : settings.profiles.front().name});
        ruleExecutable_[0] = '\0';
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button(WithIcon(icon::kFolderOpen, "Choisir un .exe…").c_str())) {
        if (const auto exe = PickExecutable(owner_)) {
            settings.rules.push_back(
                ProfileRule{U8(exe->filename().wstring()), settings.profiles.empty() ? "" : settings.profiles.front().name});
            changed = true;
        }
    }
    ImGui::EndDisabled();
    DimText("Priorité : application au premier plan, puis batterie, puis profil choisi. Bascule après ~1 s.");

    ImGui::Spacing();
    int removedProfile = -1;
    for (size_t i = 0; i < settings.profiles.size(); ++i) {
        Profile& profile = settings.profiles[i];
        ImGui::PushID(static_cast<int>(i) + 2000);
        const std::string header = profile.name + "###profile";
        if (ImGui::CollapsingHeader(header.c_str())) {
            ImGui::Indent();
            char name[64] = {};
            profile.name.copy(name, sizeof(name) - 1);
            ImGui::SetNextItemWidth(220 * s);
            if (ImGui::InputText("Nom", name, sizeof(name), ImGuiInputTextFlags_EnterReturnsTrue) && name[0] != '\0') {
                const std::string oldName = profile.name;
                profile.name = name;
                // Les references au profil suivent le renommage.
                for (auto& rule : settings.rules) {
                    if (rule.profile == oldName) rule.profile = profile.name;
                }
                if (settings.active == oldName) settings.active = profile.name;
                if (settings.batteryProfile == oldName) settings.batteryProfile = profile.name;
                changed = true;
            }
            int fps = static_cast<int>(profile.fpsLimit);
            ImGui::SetNextItemWidth(220 * s);
            if (ImGui::SliderInt("Images par seconde", &fps, 5, 144)) {
                profile.fpsLimit = static_cast<uint32_t>(fps);
            }
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            float quality = profile.renderScale * 100.0f;
            ImGui::SetNextItemWidth(220 * s);
            if (ImGui::SliderFloat("Qualité de rendu", &quality, 25.0f, 100.0f, "%.0f %%")) {
                profile.renderScale = quality / 100.0f;
            }
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            changed |= ImGui::Checkbox("Pause en plein écran", &profile.pauseOnFullscreen);
            ImGui::SameLine();
            changed |= ImGui::Checkbox("Pause si fenêtre maximisée", &profile.pauseOnMaximized);
            ImGui::SameLine();
            changed |= ImGui::Checkbox("Pause sur batterie", &profile.pauseOnBattery);
            float volume = profile.volume * 100.0f;
            ImGui::SetNextItemWidth(220 * s);
            if (ImGui::SliderFloat("Volume", &volume, 0.0f, 100.0f, "%.0f %%")) {
                profile.volume = volume / 100.0f;
            }
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::SameLine();
            changed |= ImGui::Checkbox("Muet", &profile.muted);
            if (ImGui::SmallButton(WithIcon(icon::kDelete, "Supprimer ce profil").c_str())) {
                removedProfile = static_cast<int>(i);
            }
            ImGui::Unindent();
        }
        ImGui::PopID();
    }
    if (removedProfile >= 0) {
        settings.profiles.erase(settings.profiles.begin() + removedProfile);
        changed = true;
    }
    if (ImGui::Button(WithIcon(icon::kAdd, "Nouveau profil").c_str())) {
        Profile profile;
        for (int n = 1;; ++n) {
            profile.name = std::format("Profil {}", n);
            if (!FindProfile(settings, profile.name)) {
                break;
            }
        }
        settings.profiles.push_back(profile);
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button(WithIcon(icon::kReset, "Restaurer Gaming, Travail, Batterie").c_str())) {
        for (const auto& builtin : ProfileSettings::DefaultProfiles()) {
            if (const auto it = std::find_if(settings.profiles.begin(), settings.profiles.end(),
                                             [&](const Profile& p) { return p.name == builtin.name; });
                it != settings.profiles.end()) {
                *it = builtin;
            } else {
                settings.profiles.push_back(builtin);
            }
        }
        changed = true;
    }
    if (changed) {
        services_.RefreshAutomation();
    }
}

// --------------------------------------------------------------------------------------------- //
//  Parametres
// --------------------------------------------------------------------------------------------- //

bool ControlPanelViews::DrawTransitionSettings(TransitionSettings& transition, const char* id) {
    static constexpr const char* kNames[kTransitionTypeCount] = {"Fondu", "Dissolve", "Balayage", "Zoom et fondu",
                                                                 "Aléatoire"};
    const float s = scale_;
    bool changed = false;
    int type = static_cast<int>(transition.type);
    ImGui::SetNextItemWidth(200 * s);
    if (ImGui::Combo(std::format("Effet##transition_type_{}", id).c_str(), &type, kNames, kTransitionTypeCount)) {
        transition.type = static_cast<TransitionType>(std::clamp(type, 0, kTransitionTypeCount - 1));
        changed = true;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(220 * s);
    const char* format = transition.seconds <= 0.0f ? "Coupe nette" : "%.1f s";
    if (ImGui::SliderFloat(std::format("Durée##transition_seconds_{}", id).c_str(), &transition.seconds, 0.0f, 3.0f,
                           format)) {
        transition.seconds = std::clamp(std::round(transition.seconds * 10.0f) / 10.0f, 0.0f, 3.0f);
        changed = true;
    }
    return changed;
}

void ControlPanelViews::DrawSettings() {
    const float s = scale_;
    AppConfig& config = services_.Config().Data();
    bool dirty = false;
    const auto section = [&](const char* glyph, const char* title) {
        ImGui::Spacing();
        ImGui::PushFont(fonts_.heading);
        ImGui::Text("%s  %s", glyph, title);
        ImGui::PopFont();
        ImGui::Separator();
        ImGui::Spacing();
    };

    ImGui::BeginChild("##settings", ImVec2(std::min(860.0f * s, ImGui::GetContentRegionAvail().x), 0));

    section(icon::kSpeed, "Performances");
    ImGui::TextUnformatted("Images par seconde");
    for (const uint32_t fps : {15u, 30u, 60u, 144u}) {
        const std::string label = std::format("{} FPS", fps);
        if (PillButton(label.c_str(), config.fpsLimit == fps)) {
            services_.SetFpsLimit(fps);
        }
        ImGui::SameLine();
    }
    int customFps = static_cast<int>(config.fpsLimit);
    ImGui::SetNextItemWidth(220 * s);
    if (ImGui::SliderInt("##fps_custom", &customFps, 5, 240, "%d FPS")) {
        services_.SetFpsLimit(static_cast<uint32_t>(customFps));
    }
    ImGui::Spacing();
    ImGui::TextUnformatted("Qualité de rendu");
    if (!ImGui::IsAnyItemActive() || renderScaleEdit_ < 0.0f) {
        renderScaleEdit_ = config.renderScale * 100.0f;
    }
    ImGui::SetNextItemWidth(340 * s);
    ImGui::SliderFloat("##render_scale", &renderScaleEdit_, 25.0f, 100.0f, "%.0f %%");
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        services_.SetRenderScale(renderScaleEdit_ / 100.0f);
        Toast(std::format("Qualité de rendu : {:.0f} %", renderScaleEdit_));
    }
    DimText("Sous 100 %, le fond est calculé en plus basse résolution puis agrandi par Windows : moins de travail "
            "pour le GPU, idéal sur écran 4K ou PC portable.");
    ImGui::TextDisabled("GPU : %s  ·  rendu actuel : %.0f FPS", U8(services_.Device().AdapterName()).c_str(),
                        services_.MeasuredFps());

    section(icon::kNext, "Transitions");
    dirty |= DrawTransitionSettings(config.transition, "global");
    DimText("Au changement de fond (manuel, playlist, planification), l'ancien et le nouveau sont mélangés. Les "
            "fonds web passent en coupe nette.");

    section(icon::kTimer, "Raccourcis clavier");
    DrawHotkeys();

    section(icon::kInfo, "Accessibilité");
    {
        static constexpr const char* kModes[] = {"Automatique (suivre Windows)", "Toujours", "Jamais"};
        int mode = static_cast<int>(config.reducedMotion);
        ImGui::SetNextItemWidth(260 * s);
        if (ImGui::Combo("Animations réduites", &mode, kModes, 3)) {
            config.reducedMotion = static_cast<ReducedMotion>(mode);
            services_.RefreshAutomation();
        }
        ImGui::TextDisabled("Windows : animations %s · mode réduit %s",
                            services_.SystemAnimationsEnabled() ? "activées" : "désactivées",
                            services_.Effective().reducedMotion ? "actif" : "inactif");
        DimText("Mode réduit : 15 images/s au plus, ni parallaxe ni transition. En automatique, il suit le "
                "réglage « Afficher les animations dans Windows ».");
    }

    section(icon::kGlobe, "Intégration système (fonds web)");
    {
        bool integrationChanged = ImGui::Checkbox("Morceau en cours (titre, artiste, pochette)", &config.integration.media);
        integrationChanged |= ImGui::Checkbox("Statistiques système (CPU, RAM, GPU, batterie)", &config.integration.systemStats);
        dirty |= integrationChanged;
        DimText("Lus localement par Windows (contrôles multimédia, compteurs de performance), uniquement quand un fond "
                "web s'y abonne (wep.on('media'), wep.on('system')). Rien ne quitte le PC.");
    }

    section(icon::kVolume, "Audio");
    bool muted = config.audio.muted;
    if (ImGui::Checkbox("Couper le son des fonds d'écran", &muted)) {
        services_.SetMuted(muted);
    }
    ImGui::BeginDisabled(muted);
    float volume = config.audio.volume * 100.0f;
    ImGui::SetNextItemWidth(340 * s);
    if (ImGui::SliderFloat("Volume##audio_volume", &volume, 0.0f, 100.0f, "%.0f %%")) {
        services_.SetVolume(volume / 100.0f);
    }
    ImGui::EndDisabled();
    DimText("S'applique aux fonds vidéo et web. Une même vidéo affichée sur plusieurs écrans n'est jouée qu'une fois.");

    ImGui::Spacing();
    bool reactive = config.audio.reactive;
    if (ImGui::Checkbox("Fonds réactifs au son joué par le PC", &reactive)) {
        services_.SetAudioReactive(reactive);
    }
    ImGui::BeginDisabled(!reactive);
    float sensitivity = config.audio.sensitivity;
    ImGui::SetNextItemWidth(340 * s);
    if (ImGui::SliderFloat("Sensibilité##audio_sensitivity", &sensitivity, 0.0f, 4.0f, "%.2f")) {
        services_.SetAudioSensitivity(sensitivity);
    }
    bool autoGain = config.audio.autoGain;
    if (ImGui::Checkbox("Gain automatique (remonte les sources calmes)", &autoGain)) {
        services_.SetAudioAutoGain(autoGain);
    }
    // Spectre en direct (64 bandes) : retour immediat sur la capture.
    AudioSpectrum spectrum;
    services_.CurrentAudio(spectrum);
    const float width = std::min(520.0f * s, ImGui::GetContentRegionAvail().x);
    const float height = 64.0f * s;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(origin, origin + ImVec2(width, height), ToU32(color::kPanel), 8 * s);
    const float barWidth = width / static_cast<float>(kAudioBandCount);
    for (size_t i = 0; i < kAudioBandCount; ++i) {
        const float value = std::clamp(spectrum.bands[i], 0.0f, 1.0f);
        const float x0 = origin.x + barWidth * static_cast<float>(i) + 1.0f;
        const float y0 = origin.y + height - 4 * s - (height - 8 * s) * value;
        const float t = static_cast<float>(i) / static_cast<float>(kAudioBandCount - 1);
        const ImVec4 barColor(color::kAccent.x + (color::kAccent2.x - color::kAccent.x) * t,
                              color::kAccent.y + (color::kAccent2.y - color::kAccent.y) * t,
                              color::kAccent.z + (color::kAccent2.z - color::kAccent.z) * t, 1.0f);
        drawList->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + barWidth - 2.0f, origin.y + height - 4 * s), ToU32(barColor), 2.0f);
    }
    ImGui::Dummy(ImVec2(width, height));

    // Vumetre graves / mediums / aigus + indicateur de beat et tempo.
    {
        const float meterHeight = 12.0f * s;
        const float labelWidth = 78.0f * s;
        const float meterWidth = std::max(80.0f * s, width - labelWidth - 110.0f * s);
        const ImVec2 meterOrigin = ImGui::GetCursorScreenPos();
        const char* names[3] = {"Graves", "Médiums", "Aigus"};
        const float values[3] = {spectrum.bass, spectrum.mid, spectrum.treble};
        const ImVec4 colors[3] = {color::kAccent, ImVec4((color::kAccent.x + color::kAccent2.x) * 0.5f,
                                                         (color::kAccent.y + color::kAccent2.y) * 0.5f,
                                                         (color::kAccent.z + color::kAccent2.z) * 0.5f, 1.0f),
                                  color::kAccent2};
        for (int i = 0; i < 3; ++i) {
            const float y = meterOrigin.y + static_cast<float>(i) * (meterHeight + 6.0f * s);
            drawList->AddText(ImVec2(meterOrigin.x, y - 2.0f * s), ToU32(color::kTextDim), names[i]);
            const ImVec2 barMin(meterOrigin.x + labelWidth, y);
            drawList->AddRectFilled(barMin, barMin + ImVec2(meterWidth, meterHeight), ToU32(color::kPanel), 4 * s);
            drawList->AddRectFilled(barMin,
                                    barMin + ImVec2(meterWidth * std::clamp(values[i], 0.0f, 1.0f), meterHeight),
                                    ToU32(colors[i]), 4 * s);
        }
        // Pastille qui s'allume a chaque beat, tempo a cote.
        const float blockHeight = 3.0f * meterHeight + 12.0f * s;
        const ImVec2 dot(meterOrigin.x + labelWidth + meterWidth + 28.0f * s, meterOrigin.y + blockHeight * 0.5f);
        const float pulse = std::clamp(spectrum.beat, 0.0f, 1.0f);
        drawList->AddCircleFilled(dot, 14.0f * s, ToU32(color::kPanel));
        drawList->AddCircleFilled(dot, (6.0f + 8.0f * pulse) * s,
                                  ToU32(ImVec4(color::kAccent2.x, color::kAccent2.y, color::kAccent2.z, 0.25f + 0.75f * pulse)));
        const std::string tempo = spectrum.bpm > 0.0f ? std::format("{:.0f} BPM", spectrum.bpm) : std::string("— BPM");
        drawList->AddText(ImVec2(dot.x + 22.0f * s, dot.y - ImGui::GetTextLineHeight() * 0.5f), ToU32(color::kText),
                          tempo.c_str());
        ImGui::Dummy(ImVec2(width, blockHeight));
    }

    float beatSensitivity = config.audio.beatSensitivity;
    ImGui::SetNextItemWidth(340 * s);
    if (ImGui::SliderFloat("Sensibilité du beat##beat_sensitivity", &beatSensitivity, 0.25f, 2.0f, "%.2f")) {
        services_.SetAudioBeatSensitivity(beatSensitivity);
    }
    float attack = config.audio.attackMs;
    float release = config.audio.releaseMs;
    ImGui::SetNextItemWidth(165 * s);
    bool smoothingChanged = ImGui::SliderFloat("##audio_attack", &attack, 0.0f, 200.0f, "Attaque %.0f ms");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(165 * s);
    smoothingChanged |= ImGui::SliderFloat("Lissage des zones##audio_release", &release, 20.0f, 1500.0f,
                                           "Relâchement %.0f ms", ImGuiSliderFlags_Logarithmic);
    if (smoothingChanged) {
        services_.SetAudioSmoothing(attack, release);
    }
    const std::wstring device = services_.AudioDeviceName();
    ImGui::TextDisabled("%s", device.empty() ? (reactive ? "Aucune sortie audio capturée pour l'instant"
                                                         : "Capture désactivée")
                                             : ("Capture de : " + U8(device)).c_str());
    ImGui::EndDisabled();
    DimText("Seul le son envoyé aux haut-parleurs est analysé (jamais le micro), sur ce PC uniquement. "
            "La capture s'arrête dès que les fonds sont en pause.");

    section(icon::kPause, "Pause automatique");
    dirty |= ImGui::Checkbox("Quand un jeu ou une application est en plein écran sur l'écran", &config.pause.onFullscreenApp);
    dirty |= ImGui::Checkbox("Quand une fenêtre maximisée recouvre l'écran", &config.pause.onMaximizedApp);
    dirty |= ImGui::Checkbox("Sur batterie", &config.pause.onBattery);
    dirty |= ImGui::Checkbox("Quand l'écran est éteint", &config.pause.onDisplayOff);
    dirty |= ImGui::Checkbox("Quand la session est verrouillée", &config.pause.onSessionLocked);
    dirty |= ImGui::Checkbox("Libérer la mémoire GPU pendant les pauses", &config.pause.releaseGpuResources);
    DimText("Un écran recouvert par une application plein écran n'est plus dessiné du tout : le GPU reste au jeu.");

    section(icon::kPower, "Démarrage");
    bool startWithWindows = config.startWithWindows;
    if (ImGui::Checkbox("Lancer WallEnginePaper au démarrage de Windows", &startWithWindows)) {
        services_.SetStartWithWindows(startWithWindows);
        Toast(startWithWindows ? "Lancement au démarrage activé" : "Lancement au démarrage désactivé");
    }
    DimText("Au démarrage, WallEnginePaper s'ouvre discrètement dans la zone de notification, sans afficher cette "
            "fenêtre.");

    section(icon::kFolder, "Bibliothèque");
    DimText("Dossiers analysés en plus des fonds intégrés et importés :");
    std::vector<std::filesystem::path> folders = config.libraryFolders;
    bool foldersChanged = false;
    if (folders.empty()) {
        ImGui::TextDisabled("Aucun dossier ajouté.");
    }
    for (size_t i = 0; i < folders.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::SmallButton(icon::kDelete)) {
            folders.erase(folders.begin() + static_cast<std::ptrdiff_t>(i));
            foldersChanged = true;
            ImGui::PopID();
            break;
        }
        ImGui::SetItemTooltip("Retirer ce dossier");
        ImGui::SameLine();
        ImGui::TextUnformatted(U8(folders[i].wstring()).c_str());
        ImGui::PopID();
    }
    if (ImGui::Button(WithIcon(icon::kAdd, "Ajouter un dossier…").c_str())) {
        if (const auto folder = PickFolder(owner_)) {
            folders.push_back(*folder);
            foldersChanged = true;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(WithIcon(icon::kFolderOpen, "Ouvrir les fonds importés").c_str())) {
        OpenInShell(UserLibraryDir());
    }
    if (foldersChanged) {
        services_.SetLibraryFolders(std::move(folders));
        RetireAllThumbnails();
        Toast("Bibliothèque mise à jour");
    }

    section(icon::kDocument, "Dépannage");
    if (ImGui::Button("Ouvrir le dossier de configuration")) {
        services_.OpenConfigFolder();
    }
    ImGui::SameLine();
    if (ImGui::Button("Ouvrir le journal")) {
        services_.OpenLogFile();
    }

    section(icon::kInfo, "À propos");
    if (logo_) {
        const float size = 92 * s;
        ImGui::Image(ToTextureId(logo_.view.Get()), ImVec2(size, size));
        ImGui::SameLine(0, 16 * s);
    }
    ImGui::BeginGroup();
    ImGui::PushFont(fonts_.heading);
    ImGui::TextUnformatted("WallEnginePaper");
    ImGui::PopFont();
    ImGui::TextDisabled("Version %s", LWE_VERSION_STRING);
    ImGui::TextUnformatted("100 % local : aucune connexion réseau, aucun compte, aucune télémétrie.");
    ImGui::TextDisabled("Rendu Direct3D 11  ·  Interface Dear ImGui");
    ImGui::EndGroup();
    ImGui::Dummy(ImVec2(0, 20 * s));

    ImGui::EndChild();
    if (dirty) {
        services_.Config().MarkDirty();
    }
}

}  // namespace lwe
