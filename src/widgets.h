#pragma once
#include "imgui.h"
#include <initializer_list>

// Material 3 Expressive widgets built on ImGui internals: spring motion, ripples, shape morphing.
namespace m3 {

enum class Btn { Filled, Tonal, Outlined, Text, Error };

bool Switch(const char* label, bool* v, const char* supporting = nullptr);
bool Slider(const char* label, float* v, float min, float max, const char* fmt = "%.1f");
bool SliderInt(const char* label, int* v, int min, int max, const char* fmt = "%d");
bool Button(const char* label, Btn style = Btn::Filled, float width = 0.f, const char* icon = nullptr);
bool IconButton(const char* id, const char* icon, bool tonal = false, const char* tooltip = nullptr);
bool Segmented(const char* id, int* v, std::initializer_list<const char*> items);
bool Chip(const char* label, bool selected = false, const char* icon = nullptr);
bool ColorRow(const char* label, float col[4]);
bool KeyBind(const char* label, int* vk, float pillWidth = 132.f);
bool SearchField(const char* id, const char* hint, char* buf, size_t size);

// Navigation rail: items draw hover/press only; the shared active indicator glides between them
bool RailItem(const char* icon, const char* label, bool active);
void RailIndicator(ImDrawList* dl, ImVec2 itemTopCenter);

// Cards slide + fade in one after another after BeginPage(visit) (call when a page is (re)shown)
void BeginPage(unsigned visit);
void SetSurfaceAlpha(float a);              // card background opacity (glass effects)
bool BeginCard(const char* title, const char* icon = nullptr, const char* subtitle = nullptr);
void EndCard();
void SectionLabel(const char* text);
void Supporting(const char* text);          // wrapped secondary text
void Badge(const char* text, ImU32 bg, ImU32 fg);
void FlowBegin();                           // simple wrap layout for chips / buttons
void FlowNext(float nextWidth);
float ButtonWidth(const char* label, const char* icon = nullptr);
float ChipWidth(const char* label, const char* icon = nullptr);

bool IsCapturingKey();
const char* KeyName(int vk);

} // namespace m3
