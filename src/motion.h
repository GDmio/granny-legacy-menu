#pragma once
#include "imgui.h"
#include "imgui_internal.h"

// Material 3 Expressive motion: physics springs, ripples, shape morphing.
namespace motion {

// Spring tokens from the M3 Expressive motion scheme (mass = 1)
struct SpringSpec { float stiffness, damping; };
constexpr SpringSpec kSpatial{ 380.f, 0.8f };       // movement, slight bounce
constexpr SpringSpec kSpatialFast{ 800.f, 0.6f };   // small elements (thumbs, indicators): bouncy
constexpr SpringSpec kSpatialSlow{ 200.f, 0.8f };   // large surfaces
constexpr SpringSpec kEffects{ 1600.f, 1.f };       // colour / opacity: no overshoot
constexpr SpringSpec kEffectsFast{ 3800.f, 1.f };

// Free-standing spring (for state that lives outside an ImGui window)
struct Spring {
    float value = 0.f, velocity = 0.f;
    bool init = false;
    float Update(float target, SpringSpec spec = kSpatial);
    void Snap(float v) { value = v; velocity = 0.f; init = true; }
};

// Spring stored in the current window's state storage. `start` is the value on first use (default: target).
float SpringTo(ImGuiID id, float target, SpringSpec spec = kSpatial, float start = -1e30f);
float SpringVelocity(ImGuiID id);

// Ripple from the press point, clipped exactly to the item's rounded shape
void RippleStart(ImGuiID id, ImVec2 pos);
void RippleDraw(ImGuiID id, ImDrawList* dl, const ImRect& bb, ImU32 color, float rounding);

// Expressive shape library (polar outlines), morphable into each other
enum class Shape { Circle, Cookie4, Cookie6, Cookie9, Pentagon, Pill, Sunny, Clover, Flower, Count };
void FillShape(ImDrawList* dl, ImVec2 c, float radius, Shape a, Shape b, float t, float rotation, ImU32 col);
void LoadingIndicator(ImDrawList* dl, ImVec2 c, float radius, ImU32 col, ImU32 containerCol = 0);
void WavyProgress(ImDrawList* dl, ImVec2 left, float width, float progress, ImU32 active, ImU32 track, float time);

// Soft M3 elevation shadow (layered blur approximation) under a rounded rect
void SoftShadow(ImDrawList* dl, ImVec2 mn, ImVec2 mx, float rounding, float elevation, float opacity = 1.f);

// Rounded rect with a vertical colour gradient
void GradientRoundedRect(ImDrawList* dl, ImVec2 mn, ImVec2 mx, float rounding, ImU32 top, ImU32 bottom, ImDrawFlags flags = 0);
// Fades a window and all of its child windows (call after End())
void FadeWindowTree(ImGuiWindow* window, float alpha);

// Moves / fades geometry already emitted into a draw list (entrance animations without touching layout)
void TransformDrawList(ImDrawList* dl, int vtxStart, int cmdStart, ImVec2 offset, float alpha);

float Time();  // seconds, monotonic
inline float Clamp01(float v) { return v < 0.f ? 0.f : v > 1.f ? 1.f : v; }

} // namespace motion
