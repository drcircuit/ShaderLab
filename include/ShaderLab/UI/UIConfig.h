#pragma once

#include <imgui.h>

namespace ShaderLab::UIConfig {

// Font filenames (relative to editor_assets/fonts/)
inline constexpr const char* FontFileHacked = "Hacked-KerX.ttf";
inline constexpr const char* FontFileOrbitron = "OrbitronMedium-Bz9B.ttf";
inline constexpr const char* FontFileErbosOpen = "ErbosDraco1StOpenNbpRegular-l5wX.ttf";
inline constexpr const char* FontFileCode = "Hack-Regular.ttf";
inline constexpr const char* FontFileCodeItalic = "Hack-Italic.ttf";
inline constexpr const char* FontFileOpenFontIcons = "OpenFontIcons.ttf";

// Font sizes
inline constexpr float FontLogo = 56.0f;
inline constexpr float FontHeading = 22.0f;
inline constexpr float FontText = 12.0f;
inline constexpr float FontNumeric = 12.0f;
inline constexpr float FontMenu = 12.0f;
inline constexpr float FontCode = 13.0f;

// Window padding for the merged titlebar area (base values at 96 DPI, scaled by dpiScale at runtime)
inline constexpr float TitlebarPadX = 4.0f;
inline constexpr float TitlebarPadY = 20.0f;

// Menu bar layout (base values at 96 DPI, scaled by dpiScale at runtime)
inline constexpr float MenuBarHeight = 14.0f;
inline constexpr float MenuLeftPad = 4.0f;
inline constexpr float MenuFramePadX = 4.0f;
inline constexpr float MenuFramePadY = 3.0f;
inline constexpr float MenuItemSpacingX = 4.0f;
inline constexpr float MenuItemSpacingY = 4.0f;
inline constexpr float MenuTopPad = 2.0f;
inline constexpr float MenuBottomPad = 2.0f;
inline constexpr float MenuRightPad = 3.0f;
inline constexpr float MenuLogoBaselineOffset = 0.0f;

// Theme colors
inline const ImVec4 ColorText = ImVec4(0.85f, 0.95f, 0.95f, 1.00f);
inline const ImVec4 ColorTextDisabled = ImVec4(0.40f, 0.45f, 0.45f, 1.00f);
inline const ImVec4 ColorWindowBg = ImVec4(0.02f, 0.03f, 0.04f, 1.00f);
inline const ImVec4 ColorChildBg = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
inline const ImVec4 ColorPopupBg = ImVec4(0.04f, 0.06f, 0.08f, 0.95f);
inline const ImVec4 ColorBorder = ImVec4(0.00f, 0.40f, 0.45f, 0.60f);
inline const ImVec4 ColorBorderShadow = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

inline const ImVec4 ColorFrameBg = ImVec4(0.05f, 0.08f, 0.10f, 1.00f);
inline const ImVec4 ColorFrameBgHovered = ImVec4(0.08f, 0.15f, 0.18f, 1.00f);
inline const ImVec4 ColorFrameBgActive = ImVec4(0.10f, 0.20f, 0.25f, 1.00f);

inline const ImVec4 ColorTitleBg = ImVec4(0.02f, 0.03f, 0.04f, 1.00f);
inline const ImVec4 ColorTitleBgActive = ImVec4(0.00f, 0.50f, 0.55f, 1.00f);
inline const ImVec4 ColorTitleBgCollapsed = ImVec4(0.00f, 0.00f, 0.00f, 0.51f);

inline const ImVec4 ColorMenuBarBg = ImVec4(0.04f, 0.05f, 0.06f, 1.00f);

inline const ImVec4 ColorScrollbarBg = ImVec4(0.01f, 0.02f, 0.03f, 0.53f);
inline const ImVec4 ColorScrollbarGrab = ImVec4(0.00f, 0.35f, 0.40f, 1.00f);
inline const ImVec4 ColorScrollbarGrabHovered = ImVec4(0.00f, 0.50f, 0.55f, 1.00f);
inline const ImVec4 ColorScrollbarGrabActive = ImVec4(0.00f, 0.70f, 0.75f, 1.00f);

inline const ImVec4 ColorCheckMark = ImVec4(0.00f, 1.00f, 1.00f, 1.00f);
inline const ImVec4 ColorSliderGrab = ImVec4(0.00f, 0.75f, 0.80f, 1.00f);
inline const ImVec4 ColorSliderGrabActive = ImVec4(0.00f, 1.00f, 1.00f, 1.00f);

inline const ImVec4 ColorButton = ImVec4(0.08f, 0.12f, 0.15f, 1.00f);
inline const ImVec4 ColorButtonHovered = ImVec4(0.00f, 0.35f, 0.40f, 1.00f);
inline const ImVec4 ColorButtonActive = ImVec4(0.00f, 0.60f, 0.65f, 1.00f);

inline const ImVec4 ColorHeader = ImVec4(0.08f, 0.12f, 0.15f, 1.00f);
inline const ImVec4 ColorHeaderHovered = ImVec4(0.00f, 0.35f, 0.40f, 1.00f);
inline const ImVec4 ColorHeaderActive = ImVec4(0.00f, 0.55f, 0.60f, 1.00f);

inline const ImVec4 ColorSeparator = ImVec4(0.00f, 0.40f, 0.45f, 0.50f);
inline const ImVec4 ColorSeparatorHovered = ImVec4(0.00f, 0.60f, 0.65f, 0.78f);
inline const ImVec4 ColorSeparatorActive = ImVec4(0.00f, 0.80f, 0.85f, 1.00f);

inline const ImVec4 ColorResizeGrip = ImVec4(0.00f, 0.50f, 0.55f, 0.25f);
inline const ImVec4 ColorResizeGripHovered = ImVec4(0.00f, 0.70f, 0.75f, 0.67f);
inline const ImVec4 ColorResizeGripActive = ImVec4(0.00f, 0.90f, 0.95f, 0.95f);

inline const ImVec4 ColorTab = ImVec4(0.06f, 0.08f, 0.10f, 1.00f);
inline const ImVec4 ColorTabHovered = ImVec4(0.00f, 0.40f, 0.45f, 1.00f);
inline const ImVec4 ColorTabActive = ImVec4(0.00f, 0.60f, 0.65f, 1.00f);
inline const ImVec4 ColorTabUnfocused = ImVec4(0.04f, 0.05f, 0.06f, 0.97f);
inline const ImVec4 ColorTabUnfocusedActive = ImVec4(0.06f, 0.08f, 0.10f, 1.00f);

inline const ImVec4 ColorPlotLines = ImVec4(0.00f, 0.80f, 0.85f, 1.00f);
inline const ImVec4 ColorPlotLinesHovered = ImVec4(0.00f, 1.00f, 1.00f, 1.00f);
inline const ImVec4 ColorPlotHistogram = ImVec4(0.00f, 0.85f, 0.60f, 1.00f);
inline const ImVec4 ColorPlotHistogramHovered = ImVec4(0.00f, 1.00f, 0.70f, 1.00f);

inline const ImVec4 ColorTextSelectedBg = ImVec4(0.00f, 0.60f, 0.65f, 0.35f);
inline const ImVec4 ColorDragDropTarget = ImVec4(0.00f, 1.00f, 1.00f, 0.90f);
inline const ImVec4 ColorNavHighlight = ImVec4(0.00f, 0.90f, 0.95f, 1.00f);
inline const ImVec4 ColorNavWindowingHighlight = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
inline const ImVec4 ColorNavWindowingDimBg = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
inline const ImVec4 ColorModalWindowDimBg = ImVec4(0.00f, 0.00f, 0.00f, 0.60f);

} // namespace ShaderLab::UIConfig
