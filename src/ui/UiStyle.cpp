#include "ui/UiStyle.h"

#include <windows.h>

#include <filesystem>
#include <string>

#include "core/StringUtil.h"

namespace lwe::ui {
namespace {

std::filesystem::path FontsDir() {
    wchar_t windows[MAX_PATH] = {};
    GetWindowsDirectoryW(windows, MAX_PATH);
    return std::filesystem::path(windows) / L"Fonts";
}

// Latin + Latin-1 (accents), ligatures francaises, tirets, guillemets, puces, points de suspension.
const ImWchar kTextRanges[] = {
    0x0020, 0x00FF, 0x0152, 0x0153, 0x2013, 0x2014, 0x2018, 0x201E,
    0x2022, 0x2022, 0x2026, 0x2026, 0x2192, 0x2192, 0x25CF, 0x25CF, 0,
};

// Uniquement les icones utilisees : atlas de police minimal.
const ImWchar kIconRanges[] = {
    0xE710, 0xE710, 0xE713, 0xE714, 0xE721, 0xE721, 0xE72C, 0xE72E, 0xE73E, 0xE740,
    0xE74D, 0xE74D, 0xE74F, 0xE74F, 0xE767, 0xE769, 0xE774, 0xE777, 0xE7BA, 0xE7BA, 0xE7E8, 0xE7E8,
    0xE7F4, 0xE7F4, 0xE838, 0xE838, 0xE893, 0xE893, 0xE8A5, 0xE8A5, 0xE8B1, 0xE8B1,
    0xE8B5, 0xE8B5, 0xE8B7, 0xE8B9, 0xE8F1, 0xE8F1, 0xE8FD, 0xE8FD, 0xE916, 0xE916,
    0xE943, 0xE943, 0xE946, 0xE946, 0xEC4A, 0xEC4A, 0,
};

}  // namespace

ImU32 ToU32(const ImVec4& color, float alphaMultiplier) {
    return ImGui::ColorConvertFloat4ToU32(ImVec4(color.x, color.y, color.z, color.w * alphaMultiplier));
}

Fonts BuildFonts(float scale) {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();

    const std::filesystem::path dir = FontsDir();
    std::error_code ec;
    const auto existing = [&](const wchar_t* name) -> std::string {
        const std::filesystem::path path = dir / name;
        return std::filesystem::exists(path, ec) ? WideToUtf8(path.wstring()) : std::string();
    };
    const std::string regular = existing(L"segoeui.ttf");
    std::string semibold = existing(L"seguisb.ttf");
    if (semibold.empty()) {
        semibold = existing(L"segoeuib.ttf");
    }
    if (semibold.empty()) {
        semibold = regular;
    }
    std::string icons = existing(L"SegoeIcons.ttf");
    if (icons.empty()) {
        icons = existing(L"segmdl2.ttf");
    }

    const auto addFont = [&](const std::string& file, float size, bool withIcons) -> ImFont* {
        ImFont* font = nullptr;
        if (!file.empty()) {
            ImFontConfig config;
            config.OversampleH = 2;
            config.OversampleV = 1;
            font = io.Fonts->AddFontFromFileTTF(file.c_str(), size, &config, kTextRanges);
        }
        if (!font) {
            ImFontConfig config;
            config.SizePixels = size;
            font = io.Fonts->AddFontDefault(&config);
        }
        if (withIcons && !icons.empty()) {
            ImFontConfig config;
            config.MergeMode = true;
            config.PixelSnapH = true;
            config.GlyphOffset = ImVec2(0.0f, size * 0.14f);
            config.GlyphMinAdvanceX = size;
            io.Fonts->AddFontFromFileTTF(icons.c_str(), size * 0.92f, &config, kIconRanges);
        }
        return font;
    };

    Fonts fonts;
    fonts.body = addFont(regular, 16.0f * scale, true);
    fonts.caption = addFont(regular, 13.5f * scale, true);
    fonts.heading = addFont(semibold, 21.0f * scale, true);
    fonts.display = addFont(semibold, 28.0f * scale, false);
    if (!icons.empty()) {
        fonts.glyphs = io.Fonts->AddFontFromFileTTF(icons.c_str(), 44.0f * scale, nullptr, kIconRanges);
    }
    if (!fonts.glyphs) {
        fonts.glyphs = fonts.display;
    }
    io.FontDefault = fonts.body;
    return fonts;
}

void ApplyTheme(float scale) {
    ImGuiStyle style;
    ImGui::StyleColorsDark(&style);

    style.WindowPadding = ImVec2(18, 16);
    style.FramePadding = ImVec2(10, 7);
    style.ItemSpacing = ImVec2(10, 9);
    style.ItemInnerSpacing = ImVec2(8, 6);
    style.ScrollbarSize = 11;
    style.GrabMinSize = 14;
    style.WindowBorderSize = 0;
    style.ChildBorderSize = 0;
    style.PopupBorderSize = 1;
    style.FrameBorderSize = 0;
    style.WindowRounding = 0;
    style.ChildRounding = 12;
    style.FrameRounding = 7;
    style.PopupRounding = 9;
    style.ScrollbarRounding = 9;
    style.GrabRounding = 7;
    style.TabRounding = 7;
    style.SeparatorTextBorderSize = 1;
    style.SeparatorTextPadding = ImVec2(0, 6);

    ImVec4* c = style.Colors;
    c[ImGuiCol_Text] = color::kText;
    c[ImGuiCol_TextDisabled] = color::kTextDim;
    c[ImGuiCol_WindowBg] = color::kBackground;
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = color::kPanelRaised;
    c[ImGuiCol_Border] = color::kBorder;
    c[ImGuiCol_FrameBg] = color::kFrame;
    c[ImGuiCol_FrameBgHovered] = color::kFrameHover;
    c[ImGuiCol_FrameBgActive] = color::kFrameHover;
    c[ImGuiCol_TitleBg] = color::kPanel;
    c[ImGuiCol_TitleBgActive] = color::kPanel;
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = color::kFrameHover;
    c[ImGuiCol_ScrollbarGrabHovered] = color::kBorder;
    c[ImGuiCol_ScrollbarGrabActive] = color::kAccent;
    c[ImGuiCol_CheckMark] = color::kAccentHover;
    c[ImGuiCol_SliderGrab] = color::kAccent;
    c[ImGuiCol_SliderGrabActive] = color::kAccentHover;
    c[ImGuiCol_Button] = color::kFrame;
    c[ImGuiCol_ButtonHovered] = color::kFrameHover;
    c[ImGuiCol_ButtonActive] = color::kAccentActive;
    c[ImGuiCol_Header] = ImVec4(color::kAccent.x, color::kAccent.y, color::kAccent.z, 0.28f);
    c[ImGuiCol_HeaderHovered] = ImVec4(color::kAccent.x, color::kAccent.y, color::kAccent.z, 0.40f);
    c[ImGuiCol_HeaderActive] = ImVec4(color::kAccent.x, color::kAccent.y, color::kAccent.z, 0.55f);
    c[ImGuiCol_Separator] = color::kBorder;
    c[ImGuiCol_SeparatorHovered] = color::kAccent;
    c[ImGuiCol_SeparatorActive] = color::kAccent;
    c[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_Tab] = color::kFrame;
    c[ImGuiCol_TabHovered] = color::kFrameHover;
    c[ImGuiCol_TabSelected] = color::kAccentActive;
    c[ImGuiCol_TextSelectedBg] = ImVec4(color::kAccent.x, color::kAccent.y, color::kAccent.z, 0.35f);
    c[ImGuiCol_NavCursor] = color::kAccent2;
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.55f);

    style.ScaleAllSizes(scale);
    ImGui::GetStyle() = style;
}

}  // namespace lwe::ui
