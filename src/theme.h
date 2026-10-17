#pragma once
#include "imgui.h"

// Material You (Material 3) colour scheme generated from a single seed hue.
struct Scheme {
    ImU32 primary, onPrimary, primaryContainer, onPrimaryContainer;
    ImU32 secondary, secondaryContainer, onSecondaryContainer;
    ImU32 tertiary, tertiaryContainer, onTertiaryContainer;
    ImU32 error, errorContainer, onErrorContainer;
    ImU32 surface, surfaceContainerLowest, surfaceContainerLow, surfaceContainer, surfaceContainerHigh, surfaceContainerHighest;
    ImU32 onSurface, onSurfaceVariant, outline, outlineVariant;
    ImU32 inverseSurface, inverseOnSurface, inversePrimary, scrim;
};

namespace theme {

extern Scheme S;
extern ImFont* fontRegular;
extern ImFont* fontBold;

// Type scale (px)
constexpr float kLabel = 13.f, kBody = 15.f, kBodyLarge = 16.f, kTitle = 17.f, kHeadline = 26.f, kIcon = 18.f;

void LoadFonts();
void Apply();                                  // rebuild scheme from g_cfg immediately
void Animate();                                // per frame: springs the scheme toward g_cfg (smooth theme changes)
ImU32 Tone(float hue, float chroma, float tone, float alpha = 1.f);  // OKLCH-based tonal colour
ImU32 WithAlpha(ImU32 c, float a);
ImU32 Mix(ImU32 a, ImU32 b, float t);
ImVec4 ToVec4(ImU32 c);

// Segoe Fluent / MDL2 icon code points (UTF-8)
namespace icon {
constexpr const char* Eye = "\xEE\xA2\x90";          // E890
constexpr const char* Person = "\xEE\x9D\xBB";       // E77B
constexpr const char* Bolt = "\xEE\xA5\x85";         // E945
constexpr const char* Package = "\xEE\x9E\xB8";      // E7B8
constexpr const char* Bug = "\xEE\xAF\xA8";          // EBE8
constexpr const char* Globe = "\xEE\x9D\xB4";        // E774
constexpr const char* Settings = "\xEE\x9C\x93";     // E713
constexpr const char* Power = "\xEE\x9F\xA8";        // E7E8
constexpr const char* Close = "\xEE\xA2\xBB";        // E8BB
constexpr const char* Check = "\xEE\x9C\xBE";        // E73E
constexpr const char* Search = "\xEE\x9C\xA1";       // E721
constexpr const char* Pin = "\xEE\x9C\x87";          // E707
constexpr const char* Location = "\xEE\xA0\x9D";     // E81D
constexpr const char* Snow = "\xEE\xA7\x8A";         // E9CA
constexpr const char* Save = "\xEE\x9D\x8E";         // E74E
constexpr const char* Folder = "\xEE\xA2\xB7";       // E8B7
constexpr const char* Palette = "\xEE\x9E\x90";      // E790
constexpr const char* Keyboard = "\xEE\x9D\xA5";     // E765
constexpr const char* Refresh = "\xEE\x9C\xAC";      // E72C
constexpr const char* Info = "\xEE\xA5\x86";         // E946
constexpr const char* Shield = "\xEE\xA8\x98";       // EA18
constexpr const char* Run = "\xEE\x9E\x85";          // E785 (walk)
constexpr const char* Camera = "\xEE\x9C\xA2";       // E722
constexpr const char* Brightness = "\xEE\x9C\x86";   // E706
constexpr const char* Clock = "\xEE\xA0\xA3";        // E823 (Recent)
constexpr const char* Add = "\xEE\x9C\x90";          // E710
constexpr const char* Download = "\xEE\xA2\x96";     // E896
}

} // namespace theme
