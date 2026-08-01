#include "yor_theme.hpp"

#include "imgui.h"

#include <array>
#include <filesystem>

#include <windows.h>

namespace yorstudio::ui {

namespace {

ImVec4 rgba(int red, int green, int blue, float alpha = 1.0f) {
    return {
        static_cast<float>(red) / 255.0f,
        static_cast<float>(green) / 255.0f,
        static_cast<float>(blue) / 255.0f,
        alpha,
    };
}

void applyGeometry(ImGuiStyle& style) {
    style.Alpha = 1.0f;
    style.DisabledAlpha = 0.38f;
    style.WindowPadding = {24.0f, 24.0f};
    style.WindowRounding = 0.0f;
    style.WindowBorderSize = 0.0f;
    style.WindowTitleAlign = {0.0f, 0.5f};
    style.WindowMenuButtonPosition = ImGuiDir_None;
    style.ChildRounding = 0.0f;
    style.ChildBorderSize = 0.0f;
    style.PopupRounding = 0.0f;
    style.PopupBorderSize = 0.0f;
    style.FramePadding = {16.0f, 10.0f};
    style.FrameRounding = 0.0f;
    style.FrameBorderSize = 0.0f;
    style.ItemSpacing = {12.0f, 12.0f};
    style.ItemInnerSpacing = {8.0f, 8.0f};
    style.CellPadding = {16.0f, 10.0f};
    style.IndentSpacing = 24.0f;
    style.ColumnsMinSpacing = 12.0f;
    style.ScrollbarSize = 12.0f;
    style.ScrollbarRounding = 0.0f;
    style.GrabMinSize = 20.0f;
    style.GrabRounding = 0.0f;
    style.TabRounding = 0.0f;
    style.TabBorderSize = 0.0f;
    style.ButtonTextAlign = {0.5f, 0.5f};
    style.SelectableTextAlign = {0.0f, 0.0f};
}

void applyColors(ImGuiStyle& style) {
    auto* colors = style.Colors;
    // Material 3 dark scheme. The colors are semantic surfaces, not per-widget
    // exceptions, so new ImGui controls inherit the same visual hierarchy.
    const auto primary = rgba(140, 214, 216);                    // #8CD6D8
    const auto primaryContainer = rgba(0, 79, 82);              // #004F52
    const auto onPrimaryContainer = rgba(168, 239, 241);         // #A8EFF1
    const auto secondary = rgba(205, 205, 205);                  // neutral secondary
    const auto secondaryContainer = rgba(58, 58, 58);            // neutral tonal button
    const auto tertiary = rgba(197, 197, 197);                   // neutral tertiary
    const auto error = rgba(255, 180, 171);                      // #FFB4AB
    const auto surface = rgba(18, 18, 18);                       // neutral surface
    const auto surfaceDim = rgba(18, 18, 18);                    // neutral surface dim
    const auto surfaceBright = rgba(62, 62, 62);                 // neutral surface bright
    const auto surfaceContainerLowest = rgba(10, 10, 10);        // neutral lowest container
    const auto surfaceContainerLow = rgba(24, 24, 24);           // neutral low container
    const auto surfaceContainer = rgba(30, 30, 30);              // neutral container
    const auto surfaceContainerHigh = rgba(42, 42, 42);          // neutral high container
    const auto surfaceContainerHighest = rgba(52, 52, 52);        // neutral highest container
    const auto onSurface = rgba(232, 232, 232);                  // neutral foreground
    const auto onSurfaceVariant = rgba(190, 190, 190);           // neutral secondary foreground
    const auto outline = rgba(148, 148, 148);                    // neutral outline
    const auto outlineVariant = rgba(78, 78, 78);                 // neutral decorative outline

    colors[ImGuiCol_Text] = onSurface;
    colors[ImGuiCol_TextDisabled] = rgba(224, 227, 228, 0.38f);
    colors[ImGuiCol_WindowBg] = surface;
    colors[ImGuiCol_ChildBg] = surfaceContainerLow;
    colors[ImGuiCol_PopupBg] = surfaceContainerHigh;
    colors[ImGuiCol_Border] = outlineVariant;
    colors[ImGuiCol_BorderShadow] = rgba(0, 0, 0, 0.0f);
    colors[ImGuiCol_FrameBg] = surfaceContainerHighest;
    colors[ImGuiCol_FrameBgHovered] = surfaceBright;
    colors[ImGuiCol_FrameBgActive] = surfaceContainerHigh;
    colors[ImGuiCol_TitleBg] = surfaceContainerLow;
    colors[ImGuiCol_TitleBgActive] = surfaceContainer;
    colors[ImGuiCol_TitleBgCollapsed] = surfaceContainerLow;
    colors[ImGuiCol_MenuBarBg] = surfaceContainerLow;
    colors[ImGuiCol_ScrollbarBg] = surfaceContainerLowest;
    colors[ImGuiCol_ScrollbarGrab] = outlineVariant;
    colors[ImGuiCol_ScrollbarGrabHovered] = outline;
    colors[ImGuiCol_ScrollbarGrabActive] = primary;
    colors[ImGuiCol_CheckMark] = primary;
    colors[ImGuiCol_SliderGrab] = primary;
    colors[ImGuiCol_SliderGrabActive] = onPrimaryContainer;
    colors[ImGuiCol_Button] = secondaryContainer;
    colors[ImGuiCol_ButtonHovered] = surfaceBright;
    colors[ImGuiCol_ButtonActive] = primaryContainer;
    colors[ImGuiCol_Header] = secondaryContainer;
    colors[ImGuiCol_HeaderHovered] = surfaceBright;
    colors[ImGuiCol_HeaderActive] = primaryContainer;
    colors[ImGuiCol_Separator] = outlineVariant;
    colors[ImGuiCol_SeparatorHovered] = primary;
    colors[ImGuiCol_SeparatorActive] = primary;
    colors[ImGuiCol_ResizeGrip] = outlineVariant;
    colors[ImGuiCol_ResizeGripHovered] = primary;
    colors[ImGuiCol_ResizeGripActive] = onPrimaryContainer;
    colors[ImGuiCol_Tab] = surfaceContainerLow;
    colors[ImGuiCol_TabHovered] = surfaceContainerHigh;
    colors[ImGuiCol_TabActive] = primaryContainer;
    colors[ImGuiCol_TabUnfocused] = surfaceContainerLowest;
    colors[ImGuiCol_TabUnfocusedActive] = surfaceContainer;
    colors[ImGuiCol_PlotLines] = onSurfaceVariant;
    colors[ImGuiCol_PlotLinesHovered] = primary;
    colors[ImGuiCol_PlotHistogram] = tertiary;
    colors[ImGuiCol_PlotHistogramHovered] = primary;
    colors[ImGuiCol_TableHeaderBg] = surfaceContainerHigh;
    colors[ImGuiCol_TableBorderStrong] = outlineVariant;
    colors[ImGuiCol_TableBorderLight] = rgba(78, 78, 78, 0.55f);
    colors[ImGuiCol_TableRowBg] = surfaceContainer;
    colors[ImGuiCol_TableRowBgAlt] = surfaceContainerLow;
    colors[ImGuiCol_TextSelectedBg] = rgba(140, 214, 216, 0.30f);
    colors[ImGuiCol_DragDropTarget] = primary;
    colors[ImGuiCol_NavHighlight] = primary;
    colors[ImGuiCol_NavWindowingHighlight] = primary;
    colors[ImGuiCol_NavWindowingDimBg] = rgba(18, 18, 18, 0.70f);
    colors[ImGuiCol_ModalWindowDimBg] = rgba(0, 0, 0, 0.60f);

    // Keep the semantic tokens alive in the palette even where ImGui has no
    // dedicated Material role. Error is the fallback for invalid states.
    colors[ImGuiCol_DockingEmptyBg] = surfaceDim;
    colors[ImGuiCol_DockingPreview] = rgba(140, 214, 216, 0.28f);
    colors[ImGuiCol_ModalWindowDimBg] = rgba(0, 0, 0, 0.60f);
    (void)secondary;
    (void)error;
}

void loadFonts(ImGuiIO& io) {
    const auto directory = assetDirectory();
    const auto googleSans = directory / L"GoogleSans.ttf";
    const auto icons = directory / L"MaterialDesignIcons.ttf";
    if (std::filesystem::exists(googleSans)) {
        if (auto* font = io.Fonts->AddFontFromFileTTF(googleSans.string().c_str(), 18.0f, nullptr,
                                                       io.Fonts->GetGlyphRangesCyrillic())) {
            io.FontDefault = font;
        }
    }
    if (std::filesystem::exists(icons)) {
        ImFontConfig config{};
        config.MergeMode = true;
        config.PixelSnapH = true;
        config.GlyphMinAdvanceX = 16.0f;
        static constexpr ImWchar ranges[] = {0xF0000, 0xF1FFF, 0};
        io.Fonts->AddFontFromFileTTF(icons.string().c_str(), 18.0f, &config, ranges);
    }
}

} // namespace

std::filesystem::path assetDirectory() {
    std::array<wchar_t, 32768> executable{};
    const auto length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (length && length < executable.size()) {
        return std::filesystem::path(executable.data()).parent_path() / L"yorstudio-assets";
    }
#ifdef YORSTUDIO_UI_ASSET_DIR
    return std::filesystem::path(YORSTUDIO_UI_ASSET_DIR);
#else
    return {};
#endif
}

void applyYorTheme() {
    auto& io = ImGui::GetIO();
    auto& style = ImGui::GetStyle();
    ImGui::StyleColorsDark();
    applyGeometry(style);
    applyColors(style);
    loadFonts(io);
}

} // namespace yorstudio::ui
