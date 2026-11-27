#pragma once

#include <imgui.h>

namespace lwe::ui {

// Palette tiree du logo : fond nuit, violet et bleu lumineux.
namespace color {
inline constexpr ImVec4 kBackground{0.055f, 0.055f, 0.082f, 1.0f};
inline constexpr ImVec4 kPanel{0.086f, 0.086f, 0.125f, 1.0f};
inline constexpr ImVec4 kPanelRaised{0.114f, 0.114f, 0.165f, 1.0f};
inline constexpr ImVec4 kFrame{0.145f, 0.145f, 0.208f, 1.0f};
inline constexpr ImVec4 kFrameHover{0.192f, 0.188f, 0.278f, 1.0f};
inline constexpr ImVec4 kBorder{0.22f, 0.21f, 0.32f, 1.0f};
inline constexpr ImVec4 kAccent{0.553f, 0.384f, 1.0f, 1.0f};     // violet
inline constexpr ImVec4 kAccentHover{0.639f, 0.494f, 1.0f, 1.0f};
inline constexpr ImVec4 kAccentActive{0.459f, 0.302f, 0.910f, 1.0f};
inline constexpr ImVec4 kAccent2{0.235f, 0.655f, 1.0f, 1.0f};    // bleu
inline constexpr ImVec4 kText{0.918f, 0.918f, 0.957f, 1.0f};
inline constexpr ImVec4 kTextDim{0.600f, 0.600f, 0.690f, 1.0f};
inline constexpr ImVec4 kSuccess{0.290f, 0.851f, 0.580f, 1.0f};
inline constexpr ImVec4 kWarning{1.0f, 0.733f, 0.302f, 1.0f};
}  // namespace color

// Glyphes "Segoe MDL2 Assets" (presents sur Windows 10 et 11), fusionnes dans les polices de texte.
namespace icon {
inline constexpr const char* kPlay = "";
inline constexpr const char* kPause = "";
inline constexpr const char* kNext = "";
inline constexpr const char* kSettings = "";
inline constexpr const char* kLibrary = "";
inline constexpr const char* kMonitor = "";
inline constexpr const char* kPlaylist = "";
inline constexpr const char* kSearch = "";
inline constexpr const char* kFolder = "";
inline constexpr const char* kFolderOpen = "";
inline constexpr const char* kAdd = "";
inline constexpr const char* kDelete = "";
inline constexpr const char* kRefresh = "";
inline constexpr const char* kImport = "";
inline constexpr const char* kReset = "";
inline constexpr const char* kCode = "";
inline constexpr const char* kVideo = "";
inline constexpr const char* kGlobe = "";
inline constexpr const char* kPicture = "";
inline constexpr const char* kCheck = "";
inline constexpr const char* kInfo = "";
inline constexpr const char* kWarning = "";
inline constexpr const char* kShuffle = "";
inline constexpr const char* kTimer = "";
inline constexpr const char* kPower = "";
inline constexpr const char* kSpan = "";
inline constexpr const char* kDocument = "";
inline constexpr const char* kLock = "";
inline constexpr const char* kSpeed = "";
inline constexpr const char* kVolume = "";
inline constexpr const char* kMute = "";
}  // namespace icon

struct Fonts {
    ImFont* body = nullptr;
    ImFont* caption = nullptr;
    ImFont* heading = nullptr;
    ImFont* display = nullptr;
    ImFont* glyphs = nullptr;  // icones seules, grande taille (vignettes vides)
};

// (Re)construit les polices pour l'echelle DPI donnee (Segoe UI + Segoe MDL2 Assets).
Fonts BuildFonts(float scale);
// Applique le theme sombre WallEnginePaper a l'echelle donnee.
void ApplyTheme(float scale);

ImU32 ToU32(const ImVec4& color, float alphaMultiplier = 1.0f);

}  // namespace lwe::ui
