#include "theme.h"
#include "config.h"
#include "motion.h"
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <string>

namespace theme {

Scheme S{};
ImFont* fontRegular = nullptr;
ImFont* fontBold = nullptr;

namespace {

float ToSrgb(float c) {
    c = std::clamp(c, 0.f, 1.f);
    return c <= 0.0031308f ? 12.92f * c : 1.055f * std::pow(c, 1.f / 2.4f) - 0.055f;
}

// OKLCH -> linear sRGB; returns false when outside the sRGB gamut
bool OklchToLinear(float L, float C, float hDeg, float out[3]) {
    const float h = hDeg * 3.14159265f / 180.f;
    const float a = C * std::cos(h), b = C * std::sin(h);
    float l_ = L + 0.3963377774f * a + 0.2158037573f * b;
    float m_ = L - 0.1055613458f * a - 0.0638541728f * b;
    float s_ = L - 0.0894841775f * a - 1.2914855480f * b;
    float l = l_ * l_ * l_, m = m_ * m_ * m_, s = s_ * s_ * s_;
    out[0] = 4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s;
    out[1] = -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s;
    out[2] = -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s;
    for (int i = 0; i < 3; ++i)
        if (out[i] < -0.0005f || out[i] > 1.0005f) return false;
    return true;
}

// Material tone (CIELAB L*, 0..100) -> OKLab lightness
float ToneToL(float tone) {
    const float fy = (tone + 16.f) / 116.f;
    const float y = tone > 8.f ? fy * fy * fy : tone / 903.3f;
    return std::cbrt(y);
}

std::wstring FontPath(const wchar_t* file) {
    wchar_t dir[MAX_PATH];
    GetWindowsDirectoryW(dir, MAX_PATH);
    return std::wstring(dir) + L"\\Fonts\\" + file;
}

bool Exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

std::string Narrow(const std::wstring& w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr);
    return s;
}

ImFont* AddWithIcons(const wchar_t* textFont, const wchar_t* fallback) {
    ImGuiIO& io = ImGui::GetIO();
    std::wstring path = FontPath(textFont);
    if (!Exists(path)) path = FontPath(fallback);
    ImFont* font = nullptr;
    if (Exists(path)) font = io.Fonts->AddFontFromFileTTF(Narrow(path).c_str(), 16.f);
    if (!font) return io.Fonts->AddFontDefault();

    std::wstring icons = FontPath(L"SegoeIcons.ttf");
    if (!Exists(icons)) icons = FontPath(L"segmdl2.ttf");
    if (Exists(icons)) {
        ImFontConfig cfg;
        cfg.MergeMode = true;
        cfg.GlyphOffset = ImVec2(0.f, 2.f);
        io.Fonts->AddFontFromFileTTF(Narrow(icons).c_str(), 16.f, &cfg);
    }
    return font;
}

} // namespace

ImU32 Tone(float hue, float chroma, float tone, float alpha) {
    const float L = ToneToL(tone);
    float rgb[3];
    // Reduce chroma until the colour fits into sRGB (keeps hue + lightness, like HCT does)
    float lo = 0.f, hi = chroma;
    if (!OklchToLinear(L, hi, hue, rgb)) {
        for (int i = 0; i < 18; ++i) {
            const float mid = (lo + hi) * 0.5f;
            if (OklchToLinear(L, mid, hue, rgb)) lo = mid;
            else hi = mid;
        }
        OklchToLinear(L, lo, hue, rgb);
    }
    return ImGui::ColorConvertFloat4ToU32(ImVec4(ToSrgb(rgb[0]), ToSrgb(rgb[1]), ToSrgb(rgb[2]), alpha));
}

ImU32 WithAlpha(ImU32 c, float a) {
    ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
    v.w *= a;
    return ImGui::ColorConvertFloat4ToU32(v);
}

ImU32 Mix(ImU32 a, ImU32 b, float t) {
    ImVec4 x = ImGui::ColorConvertU32ToFloat4(a), y = ImGui::ColorConvertU32ToFloat4(b);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(x.x + (y.x - x.x) * t, x.y + (y.y - x.y) * t, x.z + (y.z - x.z) * t, x.w + (y.w - x.w) * t));
}

ImVec4 ToVec4(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }

void LoadFonts() {
    fontRegular = AddWithIcons(L"segoeui.ttf", L"arial.ttf");
    fontBold = AddWithIcons(L"seguisb.ttf", L"segoeuib.ttf");
    ImGui::GetIO().FontDefault = fontRegular;
}

namespace {

// Fills S with the tonal scheme for one seed (dark or light)
void FillScheme(float h, float k, bool light) {
    k = std::clamp(k, 0.f, 1.f);
    const float cP = 0.26f * k, cS = 0.075f * k, cT = 0.18f * k, cN = 0.018f * k + 0.004f, cNV = 0.035f * k + 0.006f;
    const float hT = h + 60.f, hE = 27.f;

    if (!light) {
        S.primary = Tone(h, cP, 80); S.onPrimary = Tone(h, cP, 20);
        S.primaryContainer = Tone(h, cP, 30); S.onPrimaryContainer = Tone(h, cP, 90);
        S.secondary = Tone(h, cS, 80); S.secondaryContainer = Tone(h, cS, 30); S.onSecondaryContainer = Tone(h, cS, 90);
        S.tertiary = Tone(hT, cT, 80); S.tertiaryContainer = Tone(hT, cT, 30); S.onTertiaryContainer = Tone(hT, cT, 90);
        S.error = Tone(hE, 0.2f, 80); S.errorContainer = Tone(hE, 0.2f, 30); S.onErrorContainer = Tone(hE, 0.2f, 90);
        S.surface = Tone(h, cN, 6); S.surfaceContainerLowest = Tone(h, cN, 4); S.surfaceContainerLow = Tone(h, cN, 10);
        S.surfaceContainer = Tone(h, cN, 12); S.surfaceContainerHigh = Tone(h, cN, 17); S.surfaceContainerHighest = Tone(h, cN, 22);
        S.onSurface = Tone(h, cN, 90); S.onSurfaceVariant = Tone(h, cNV, 80);
        S.outline = Tone(h, cNV, 60); S.outlineVariant = Tone(h, cNV, 30);
        S.inverseSurface = Tone(h, cN, 90); S.inverseOnSurface = Tone(h, cN, 20); S.inversePrimary = Tone(h, cP, 40);
    } else {
        S.primary = Tone(h, cP, 40); S.onPrimary = Tone(h, cP, 100);
        S.primaryContainer = Tone(h, cP, 90); S.onPrimaryContainer = Tone(h, cP, 10);
        S.secondary = Tone(h, cS, 40); S.secondaryContainer = Tone(h, cS, 90); S.onSecondaryContainer = Tone(h, cS, 10);
        S.tertiary = Tone(hT, cT, 40); S.tertiaryContainer = Tone(hT, cT, 90); S.onTertiaryContainer = Tone(hT, cT, 10);
        S.error = Tone(hE, 0.2f, 40); S.errorContainer = Tone(hE, 0.2f, 90); S.onErrorContainer = Tone(hE, 0.2f, 10);
        S.surface = Tone(h, cN, 98); S.surfaceContainerLowest = Tone(h, cN, 100); S.surfaceContainerLow = Tone(h, cN, 96);
        S.surfaceContainer = Tone(h, cN, 94); S.surfaceContainerHigh = Tone(h, cN, 92); S.surfaceContainerHighest = Tone(h, cN, 90);
        S.onSurface = Tone(h, cN, 10); S.onSurfaceVariant = Tone(h, cNV, 30);
        S.outline = Tone(h, cNV, 50); S.outlineVariant = Tone(h, cNV, 80);
        S.inverseSurface = Tone(h, cN, 20); S.inverseOnSurface = Tone(h, cN, 95); S.inversePrimary = Tone(h, cP, 80);
    }
    S.scrim = IM_COL32(0, 0, 0, 255);
}

void PushStyle() {
    ImGuiStyle& st = ImGui::GetStyle();
    st.WindowRounding = 28.f;
    st.ChildRounding = 20.f;
    st.FrameRounding = 12.f;
    st.PopupRounding = 16.f;
    st.GrabRounding = 12.f;
    st.ScrollbarRounding = 12.f;
    st.TabRounding = 12.f;
    st.WindowBorderSize = 0.f;
    st.ChildBorderSize = 0.f;
    st.PopupBorderSize = 0.f;
    st.FrameBorderSize = 0.f;
    st.WindowPadding = ImVec2(0, 0);
    st.FramePadding = ImVec2(14, 10);
    st.ItemSpacing = ImVec2(10, 10);
    st.ItemInnerSpacing = ImVec2(8, 6);
    st.ScrollbarSize = 8.f;
    st.GrabMinSize = 18.f;

    ImVec4* c = st.Colors;
    c[ImGuiCol_Text] = ToVec4(S.onSurface);
    c[ImGuiCol_TextDisabled] = ToVec4(WithAlpha(S.onSurface, 0.38f));
    c[ImGuiCol_WindowBg] = ToVec4(S.surface);
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = ToVec4(S.surfaceContainerHigh);
    c[ImGuiCol_Border] = ToVec4(S.outlineVariant);
    c[ImGuiCol_FrameBg] = ToVec4(S.surfaceContainerHighest);
    c[ImGuiCol_FrameBgHovered] = ToVec4(Mix(S.surfaceContainerHighest, S.onSurface, 0.08f));
    c[ImGuiCol_FrameBgActive] = ToVec4(Mix(S.surfaceContainerHighest, S.onSurface, 0.12f));
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = ToVec4(S.outlineVariant);
    c[ImGuiCol_ScrollbarGrabHovered] = ToVec4(S.outline);
    c[ImGuiCol_ScrollbarGrabActive] = ToVec4(S.primary);
    c[ImGuiCol_CheckMark] = ToVec4(S.primary);
    c[ImGuiCol_SliderGrab] = ToVec4(S.primary);
    c[ImGuiCol_SliderGrabActive] = ToVec4(S.primary);
    c[ImGuiCol_Button] = ToVec4(S.secondaryContainer);
    c[ImGuiCol_ButtonHovered] = ToVec4(Mix(S.secondaryContainer, S.onSecondaryContainer, 0.08f));
    c[ImGuiCol_ButtonActive] = ToVec4(Mix(S.secondaryContainer, S.onSecondaryContainer, 0.12f));
    c[ImGuiCol_Header] = ToVec4(S.secondaryContainer);
    c[ImGuiCol_HeaderHovered] = ToVec4(Mix(S.secondaryContainer, S.onSecondaryContainer, 0.08f));
    c[ImGuiCol_HeaderActive] = ToVec4(Mix(S.secondaryContainer, S.onSecondaryContainer, 0.12f));
    c[ImGuiCol_Separator] = ToVec4(S.outlineVariant);
    c[ImGuiCol_TextSelectedBg] = ToVec4(WithAlpha(S.primary, 0.35f));
    c[ImGuiCol_NavCursor] = ToVec4(S.primary);
    c[ImGuiCol_ModalWindowDimBg] = ToVec4(WithAlpha(S.scrim, 0.32f));
}

// lightT: 0 = dark scheme, 1 = light scheme, in between = blended (theme switch animation)
void ApplyValues(float hue, float chroma, float lightT) {
    lightT = std::clamp(lightT, 0.f, 1.f);
    if (lightT <= 0.001f || lightT >= 0.999f) {
        FillScheme(hue, chroma, lightT > 0.5f);
    } else {
        FillScheme(hue, chroma, false);
        const Scheme dark = S;
        FillScheme(hue, chroma, true);
        const Scheme light = S;
        const ImU32* a = reinterpret_cast<const ImU32*>(&dark);
        const ImU32* b = reinterpret_cast<const ImU32*>(&light);
        ImU32* out = reinterpret_cast<ImU32*>(&S);
        for (size_t i = 0; i < sizeof(Scheme) / sizeof(ImU32); ++i) out[i] = Mix(a[i], b[i], lightT);
    }
    PushStyle();
}

motion::Spring g_hue, g_chroma, g_light;

} // namespace

void Apply() {
    g_hue.Snap(g_cfg.seedHue);
    g_chroma.Snap(g_cfg.seedChroma);
    g_light.Snap(g_cfg.lightTheme ? 1.f : 0.f);
    ApplyValues(g_hue.value, g_chroma.value, g_light.value);
}

void Animate() {
    // hue travels the short way round the colour wheel
    float target = g_cfg.seedHue;
    while (target - g_hue.value > 180.f) target -= 360.f;
    while (target - g_hue.value < -180.f) target += 360.f;
    const float h = g_hue.Update(target, motion::kEffects);
    const float c = g_chroma.Update(g_cfg.seedChroma, motion::kEffects);
    const float l = g_light.Update(g_cfg.lightTheme ? 1.f : 0.f, motion::kSpatialSlow);
    static float lastH = -1, lastC = -1, lastL = -1;
    if (h != lastH || c != lastC || l != lastL) {
        ApplyValues(std::fmod(h + 360.f, 360.f), c, motion::Clamp01(l));
        lastH = h; lastC = c; lastL = l;
    }
}

} // namespace theme
