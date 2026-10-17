#include "widgets.h"
#include "i18n.h"
#include "motion.h"
#include "theme.h"
#include <Windows.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <unordered_map>
#include <vector>
#include "imgui_internal.h"

using namespace ImGui;
using namespace theme;
using motion::SpringTo;

namespace m3 {

namespace {

// Distinct storage slots derived from one widget id
inline ImGuiID Slot(ImGuiID id, unsigned n) { return id * 2654435761u + n * 40503u + 1u; }

ImVec2 TextSize(ImFont* f, float size, const char* t, const char* end = nullptr) { return f->CalcTextSizeA(size, FLT_MAX, 0.f, t, end); }

void Text(ImDrawList* dl, ImFont* f, float size, ImVec2 p, ImU32 col, const char* t, const char* end = nullptr) {
    dl->AddText(f, size, p, col, t, end);
}

void TextCentered(ImDrawList* dl, ImFont* f, float size, ImVec2 center, ImU32 col, const char* t, const char* end = nullptr) {
    ImVec2 sz = TextSize(f, size, t, end);
    dl->AddText(f, size, ImVec2(ImFloor(center.x - sz.x * 0.5f), ImFloor(center.y - sz.y * 0.5f)), col, t, end);
}

// Animated state layer (hover 8 %, press 12 %) + ripple from the click point
float StateLayer(ImGuiID id, bool hov, bool held) {
    if (hov && IsMouseClicked(ImGuiMouseButton_Left)) motion::RippleStart(id, GetIO().MousePos);
    return SpringTo(Slot(id, 1), held ? 0.12f : hov ? 0.08f : 0.f, motion::kEffects);
}

// Press "squish": corner radius springs from rest to pressed (M3 Expressive shape morph)
float PressRadius(ImGuiID id, bool held, float rest, float pressed) {
    return ImMax(0.f, SpringTo(Slot(id, 2), held ? pressed : rest, motion::kSpatialFast));
}

ImGuiID g_capturing = 0;
bool g_prevKeys[256]{};
float g_flowMaxX = 0.f;
bool g_flowFirst = true;

// Cards
unsigned g_visit = 0;
float g_visitT0 = 0.f;
int g_cardIndex = 0;
struct CardCtx { float t; ImGuiID id; };
std::vector<CardCtx> g_cardStack;
std::unordered_map<ImGuiID, ImRect> g_cardRects;

// Navigation rail indicator
motion::Spring g_railY, g_railW;
float g_surfaceAlpha = 1.f;

} // namespace

bool Switch(const char* label, bool* v, const char* supporting) {
    ImGuiWindow* w = GetCurrentWindow();
    if (w->SkipItems) return false;
    const ImGuiID id = w->GetID(label);
    const char* end = FindRenderedTextEnd(label);
    const float width = GetContentRegionAvail().x;
    const float rowH = supporting ? 54.f : 40.f;
    const ImVec2 pos = w->DC.CursorPos;
    const ImRect bb(pos, pos + ImVec2(width, rowH));
    ItemSize(bb);
    if (!ItemAdd(bb, id)) return false;
    bool hov, held;
    const bool pressed = ButtonBehavior(bb, id, &hov, &held);
    if (pressed) {
        *v = !*v;
        MarkItemEdited(id);
    }
    // position: bouncy spatial spring; colour: effects spring (no overshoot)
    const float t = SpringTo(Slot(id, 3), *v ? 1.f : 0.f, motion::kSpatialFast);
    const float ct = SpringTo(Slot(id, 4), *v ? 1.f : 0.f, motion::kEffects);
    const float vel = motion::SpringVelocity(Slot(id, 3));
    const float grow = SpringTo(Slot(id, 5), held ? 1.f : 0.f, motion::kSpatialFast);
    ImDrawList* dl = w->DrawList;

    if (supporting) {
        Text(dl, fontRegular, kBodyLarge, pos + ImVec2(0, 7), S.onSurface, label, end);
        Text(dl, fontRegular, kLabel, pos + ImVec2(0, 29), S.onSurfaceVariant, supporting);
    } else {
        Text(dl, fontRegular, kBodyLarge, pos + ImVec2(0, (rowH - kBodyLarge) * 0.5f - 2.f), S.onSurface, label, end);
    }

    const ImVec2 tp(bb.Max.x - 52.f, pos.y + (rowH - 32.f) * 0.5f);
    dl->AddRectFilled(tp, tp + ImVec2(52, 32), Mix(S.surfaceContainerHighest, S.primary, ct), 16.f);
    if (ct < 0.99f) dl->AddRect(tp, tp + ImVec2(52, 32), WithAlpha(S.outline, 1.f - ct), 16.f, 0, 2.f);
    const ImVec2 c(tp.x + 16.f + 20.f * t, tp.y + 16.f);
    const float layer = SpringTo(Slot(id, 6), hov ? 1.f : 0.f, motion::kEffects);
    if (layer > 0.01f) dl->AddCircleFilled(c, 20.f, WithAlpha(*v ? S.primary : S.onSurface, 0.10f * layer), 32);
    // squash & stretch: the thumb elongates along its travel while moving fast
    const float r = 8.f + 4.f * ct + 2.f * grow;
    const float stretch = ImMin(std::fabs(vel) * 1.4f, 9.f);
    dl->AddRectFilled(c - ImVec2(r + stretch * 0.5f, r - stretch * 0.12f), c + ImVec2(r + stretch * 0.5f, r - stretch * 0.12f),
                      Mix(S.outline, S.onPrimary, ct), r);
    if (ct > 0.4f) TextCentered(dl, fontRegular, 13.f, c + ImVec2(0, -1), WithAlpha(S.onPrimaryContainer, (ct - 0.4f) / 0.6f), icon::Check);
    return pressed;
}

static bool SliderImpl(const char* label, float* v, float mn, float mx, const char* valueText) {
    ImGuiWindow* w = GetCurrentWindow();
    if (w->SkipItems) return false;
    const ImGuiID id = w->GetID(label);
    const char* end = FindRenderedTextEnd(label);
    const float width = GetContentRegionAvail().x;
    const ImVec2 pos = w->DC.CursorPos;
    const ImRect bb(pos, pos + ImVec2(width, 54.f));
    ItemSize(bb);
    if (!ItemAdd(bb, id)) return false;
    const ImRect hit(ImVec2(bb.Min.x, pos.y + 22.f), bb.Max);
    bool hov, held;
    ButtonBehavior(hit, id, &hov, &held, ImGuiButtonFlags_PressedOnClick);

    const float x0 = bb.Min.x + 4.f, x1 = bb.Max.x - 4.f, cy = pos.y + 38.f;
    bool changed = false;
    if (held) {
        const float tt = ImClamp((GetIO().MousePos.x - x0) / (x1 - x0), 0.f, 1.f);
        const float nv = mn + (mx - mn) * tt;
        if (nv != *v) {
            *v = nv;
            changed = true;
            MarkItemEdited(id);
        }
    }
    const float frac = ImClamp((*v - mn) / (mx - mn), 0.f, 1.f);
    const float shown = SpringTo(Slot(id, 3), frac, held ? motion::kSpatialFast : motion::kSpatial);  // handle glides to clicks
    const float xc = x0 + (x1 - x0) * ImClamp(shown, 0.f, 1.f);
    const float press = SpringTo(Slot(id, 4), held ? 1.f : 0.f, motion::kSpatialFast);
    ImDrawList* dl = w->DrawList;

    Text(dl, fontRegular, kBodyLarge, pos, S.onSurface, label, end);
    const ImVec2 vs = TextSize(fontBold, kBody, valueText);
    Text(dl, fontBold, kBody, ImVec2(bb.Max.x - vs.x, pos.y + 1.f), S.primary, valueText);

    // M3 Expressive slider: rounded tracks separated by a gap around a bar handle that narrows while pressed
    const float trackH = 16.f, gap = 6.f;
    const float handleW = 4.f - 2.f * press;
    if (xc - gap - x0 > 2.f)
        dl->AddRectFilled(ImVec2(x0, cy - trackH * 0.5f), ImVec2(xc - gap, cy + trackH * 0.5f), S.primary, 8.f, ImDrawFlags_RoundCornersLeft);
    if (x1 - (xc + gap) > 2.f) {
        dl->AddRectFilled(ImVec2(xc + gap, cy - trackH * 0.5f), ImVec2(x1, cy + trackH * 0.5f), S.secondaryContainer, 8.f,
                          ImDrawFlags_RoundCornersRight);
        dl->AddCircleFilled(ImVec2(x1 - 6.f, cy), 2.f, S.primary);
    }
    dl->AddRectFilled(ImVec2(xc - handleW * 0.5f, cy - 22.f), ImVec2(xc + handleW * 0.5f, cy + 22.f), S.primary, 2.f);
    const float layer = SpringTo(Slot(id, 5), (hov || held) ? 1.f : 0.f, motion::kEffects);
    if (layer > 0.01f) dl->AddRectFilled(ImVec2(xc - 10.f, cy - 22.f), ImVec2(xc + 10.f, cy + 22.f), WithAlpha(S.primary, 0.08f * layer), 10.f);

    // value indicator bubble that pops up while dragging
    if (press > 0.02f) {
        const float s = ImClamp(press, 0.f, 1.15f);
        const ImVec2 ts = TextSize(fontBold, 13.f, valueText);
        const ImVec2 sz((ts.x + 22.f) * s, 30.f * s);
        const ImVec2 anchor(xc, cy - 28.f);
        const ImVec2 mnB(anchor.x - sz.x * 0.5f, anchor.y - sz.y), mxB(anchor.x + sz.x * 0.5f, anchor.y);
        dl->AddRectFilled(mnB, mxB, WithAlpha(S.inverseSurface, ImMin(1.f, press)), sz.y * 0.5f);
        if (s > 0.6f)
            TextCentered(dl, fontBold, 13.f * ImMin(s, 1.f), ImVec2(anchor.x, anchor.y - sz.y * 0.5f), WithAlpha(S.inverseOnSurface, ImMin(1.f, press)),
                         valueText);
    }
    return changed;
}

bool Slider(const char* label, float* v, float mn, float mx, const char* fmt) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), fmt, *v);
    return SliderImpl(label, v, mn, mx, buf);
}

bool SliderInt(const char* label, int* v, int mn, int mx, const char* fmt) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), fmt, *v);
    float f = static_cast<float>(*v);
    if (!SliderImpl(label, &f, static_cast<float>(mn), static_cast<float>(mx), buf)) return false;
    const int nv = static_cast<int>(std::lround(f));
    if (nv == *v) return false;
    *v = nv;
    return true;
}

float ButtonWidth(const char* label, const char* icon) {
    const char* end = FindRenderedTextEnd(label);
    return TextSize(fontBold, 14.f, label, end).x + 48.f + (icon ? 26.f : 0.f);
}

bool Button(const char* label, Btn style, float width, const char* icon) {
    ImGuiWindow* w = GetCurrentWindow();
    if (w->SkipItems) return false;
    const ImGuiID id = w->GetID(label);
    const char* end = FindRenderedTextEnd(label);
    const float bw = width > 0.f ? width : ButtonWidth(label, icon);
    const ImVec2 pos = w->DC.CursorPos;
    const ImRect bb(pos, pos + ImVec2(bw, 40.f));
    ItemSize(bb);
    if (!ItemAdd(bb, id)) return false;
    bool hov, held;
    const bool pressed = ButtonBehavior(bb, id, &hov, &held);

    ImU32 bg = 0, fg = S.primary;
    bool border = false;
    switch (style) {
    case Btn::Filled: bg = S.primary; fg = S.onPrimary; break;
    case Btn::Tonal: bg = S.secondaryContainer; fg = S.onSecondaryContainer; break;
    case Btn::Outlined: border = true; break;
    case Btn::Text: break;
    case Btn::Error: bg = S.errorContainer; fg = S.onErrorContainer; break;
    }
    const float r = PressRadius(id, held, 20.f, 8.f);
    const float layer = StateLayer(id, hov, held);
    ImDrawList* dl = w->DrawList;
    if (bg) dl->AddRectFilled(bb.Min, bb.Max, bg, r);
    if (layer > 0.003f) dl->AddRectFilled(bb.Min, bb.Max, WithAlpha(fg, layer), r);
    motion::RippleDraw(id, dl, bb, fg, r);
    if (border) dl->AddRect(bb.Min, bb.Max, S.outline, r, 0, 1.f);

    const ImVec2 ts = TextSize(fontBold, 14.f, label, end);
    const float contentW = ts.x + (icon ? 26.f : 0.f);
    float x = bb.Min.x + (bw - contentW) * 0.5f;
    const float cy = bb.Min.y + 20.f;
    if (icon) {
        TextCentered(dl, fontRegular, kIcon, ImVec2(x + 9.f, cy - 1.f), fg, icon);
        x += 26.f;
    }
    Text(dl, fontBold, 14.f, ImVec2(x, cy - ts.y * 0.5f), fg, label, end);
    return pressed;
}

bool IconButton(const char* idStr, const char* ic, bool tonal, const char* tooltip) {
    ImGuiWindow* w = GetCurrentWindow();
    if (w->SkipItems) return false;
    const ImGuiID id = w->GetID(idStr);
    const ImVec2 pos = w->DC.CursorPos;
    const ImRect bb(pos, pos + ImVec2(40, 40));
    ItemSize(bb);
    if (!ItemAdd(bb, id)) return false;
    bool hov, held;
    const bool pressed = ButtonBehavior(bb, id, &hov, &held);
    ImDrawList* dl = w->DrawList;
    const ImU32 fg = tonal ? S.onSecondaryContainer : S.onSurfaceVariant;
    const float r = PressRadius(id, held, 20.f, 11.f);  // circle squishes into a squircle
    const float layer = StateLayer(id, hov, held);
    if (tonal) dl->AddRectFilled(bb.Min, bb.Max, S.secondaryContainer, r);
    if (layer > 0.003f) dl->AddRectFilled(bb.Min, bb.Max, WithAlpha(fg, layer), r);
    motion::RippleDraw(id, dl, bb, fg, r);
    TextCentered(dl, fontRegular, 20.f, bb.GetCenter() + ImVec2(0, -1), fg, ic);
    if (tooltip && hov) SetTooltip("%s", tooltip);
    return pressed;
}

bool Segmented(const char* idStr, int* v, std::initializer_list<const char*> items) {
    ImGuiWindow* w = GetCurrentWindow();
    if (w->SkipItems) return false;
    const int n = static_cast<int>(items.size());
    const float width = GetContentRegionAvail().x;
    const ImVec2 pos = w->DC.CursorPos;
    const ImRect bb(pos, pos + ImVec2(width, 40.f));
    ItemSize(bb);
    PushID(idStr);
    const ImGuiID baseId = GetID("##seg");
    bool changed = false;
    const float segW = width / n;
    ImDrawList* dl = w->DrawList;

    // the selection glides between segments and stretches while travelling (connected button group)
    const float sel = SpringTo(Slot(baseId, 1), static_cast<float>(*v), motion::kSpatialFast);
    const float vel = motion::SpringVelocity(Slot(baseId, 1));
    const float stretch = ImMin(std::fabs(vel) * segW * 0.06f, segW * 0.35f);
    dl->AddRectFilled(bb.Min, bb.Max, WithAlpha(S.surfaceContainerHighest, 0.6f), 20.f);
    // no clipping: when the spring overshoots, the pill squashes against the group's edge instead of being cut
    ImVec2 iMin(bb.Min.x + segW * sel + 3.f - stretch * 0.5f, bb.Min.y + 3.f);
    ImVec2 iMax(bb.Min.x + segW * (sel + 1.f) - 3.f + stretch * 0.5f, bb.Max.y - 3.f);
    const float minW = 34.f;
    iMin.x = ImClamp(iMin.x, bb.Min.x + 3.f, bb.Max.x - 3.f - minW);
    iMax.x = ImClamp(iMax.x, iMin.x + minW, bb.Max.x - 3.f);
    dl->AddRectFilled(iMin, iMax, S.secondaryContainer, 17.f);

    int i = 0;
    for (const char* item : items) {
        const ImRect r(ImVec2(bb.Min.x + segW * i, bb.Min.y), ImVec2(bb.Min.x + segW * (i + 1), bb.Max.y));
        const ImGuiID id = GetID(i);
        if (ItemAdd(r, id)) {
            bool hov, held;
            if (ButtonBehavior(r, id, &hov, &held) && *v != i) {
                *v = i;
                changed = true;
            }
            const float closeness = motion::Clamp01(1.f - std::fabs(sel - i));
            const ImU32 fg = Mix(S.onSurface, S.onSecondaryContainer, closeness);
            const float layer = StateLayer(id, hov, held);
            const ImRect inner(r.Min + ImVec2(3, 3), r.Max - ImVec2(3, 3));
            if (layer > 0.003f) dl->AddRectFilled(inner.Min, inner.Max, WithAlpha(fg, layer), 17.f);
            motion::RippleDraw(id, dl, inner, fg, 17.f);
            const ImVec2 ts = TextSize(fontBold, 13.5f, item);
            const float iconW = 20.f * closeness;
            float x = r.Min.x + (segW - ts.x - iconW) * 0.5f;
            if (closeness > 0.05f) TextCentered(dl, fontRegular, 15.f, ImVec2(x + 7.f, r.GetCenter().y - 1.f), WithAlpha(fg, closeness), icon::Check);
            x += iconW;
            Text(dl, fontBold, 13.5f, ImVec2(x, r.GetCenter().y - ts.y * 0.5f), fg, item);
        }
        ++i;
    }
    PopID();
    return changed;
}

float ChipWidth(const char* label, const char* ic) {
    return TextSize(fontRegular, 14.f, label, FindRenderedTextEnd(label)).x + 32.f + (ic ? 22.f : 0.f);
}

bool Chip(const char* label, bool selected, const char* ic) {
    ImGuiWindow* w = GetCurrentWindow();
    if (w->SkipItems) return false;
    const ImGuiID id = w->GetID(label);
    const char* end = FindRenderedTextEnd(label);
    if (selected && !ic) ic = icon::Check;
    const float cw = ChipWidth(label, ic);
    const ImVec2 pos = w->DC.CursorPos;
    const ImRect bb(pos, pos + ImVec2(cw, 32.f));
    ItemSize(bb);
    if (!ItemAdd(bb, id)) return false;
    bool hov, held;
    const bool pressed = ButtonBehavior(bb, id, &hov, &held);
    ImDrawList* dl = w->DrawList;
    const float st = SpringTo(Slot(id, 3), selected ? 1.f : 0.f, motion::kEffects);
    const float r = PressRadius(id, held, 8.f, 16.f);  // chips round out when pressed
    const ImU32 fg = Mix(S.onSurfaceVariant, S.onSecondaryContainer, st);
    if (st > 0.01f) dl->AddRectFilled(bb.Min, bb.Max, WithAlpha(S.secondaryContainer, st), r);
    if (st < 0.99f) dl->AddRect(bb.Min, bb.Max, WithAlpha(S.outlineVariant, 1.f - st), r, 0, 1.f);
    const float layer = StateLayer(id, hov, held);
    if (layer > 0.003f) dl->AddRectFilled(bb.Min, bb.Max, WithAlpha(fg, layer), r);
    motion::RippleDraw(id, dl, bb, fg, r);
    float x = bb.Min.x + 16.f;
    if (ic) {
        TextCentered(dl, fontRegular, 16.f, ImVec2(x + 7.f, bb.GetCenter().y - 1.f), Mix(S.primary, fg, st), ic);
        x += 22.f;
    }
    const ImVec2 ts = TextSize(fontRegular, 14.f, label, end);
    Text(dl, fontRegular, 14.f, ImVec2(x, bb.GetCenter().y - ts.y * 0.5f), fg, label, end);
    return pressed;
}

bool ColorRow(const char* label, float col[4]) {
    ImGuiWindow* w = GetCurrentWindow();
    if (w->SkipItems) return false;
    const ImGuiID id = w->GetID(label);
    const char* end = FindRenderedTextEnd(label);
    const float width = GetContentRegionAvail().x;
    const ImVec2 pos = w->DC.CursorPos;
    const ImRect bb(pos, pos + ImVec2(width, 40.f));
    ItemSize(bb);
    if (!ItemAdd(bb, id)) return false;
    bool hov, held;
    const bool pressed = ButtonBehavior(bb, id, &hov, &held);
    ImDrawList* dl = w->DrawList;
    Text(dl, fontRegular, kBodyLarge, pos + ImVec2(0, 10), S.onSurface, label, end);
    const ImVec2 c(bb.Max.x - 22.f, bb.GetCenter().y);
    const float hv = SpringTo(Slot(id, 3), hov ? 1.f : 0.f, motion::kSpatialFast);
    if (hv > 0.01f) dl->AddCircleFilled(c, 20.f, WithAlpha(S.onSurface, 0.08f * ImMin(hv, 1.f)), 32);
    // swatch is a cookie shape that turns a little on hover
    motion::FillShape(dl, c, 14.f + 2.f * hv, motion::Shape::Cookie9, motion::Shape::Cookie9, 0.f, hv * 0.5f,
                      ColorConvertFloat4ToU32(ImVec4(col[0], col[1], col[2], 1.f)));
    PushID(label);
    if (pressed) OpenPopup("##colorpop");
    bool changed = false;
    PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 16));
    PushStyleVar(ImGuiStyleVar_FrameRounding, 8.f);
    if (BeginPopup("##colorpop")) {
        PushFont(fontRegular, kBody);
        SetNextItemWidth(230.f);
        changed = ColorPicker4("##picker", col,
                               ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_PickerHueWheel |
                                   ImGuiColorEditFlags_NoLabel);
        PopFont();
        EndPopup();
    }
    PopStyleVar(2);
    PopID();
    return changed;
}

const char* KeyName(int vk) {
    static char buf[64];
    switch (vk) {
    case 0: return "—";
    case VK_LBUTTON: return "Mouse1";
    case VK_RBUTTON: return "Mouse2";
    case VK_MBUTTON: return "Mouse3";
    case VK_XBUTTON1: return "Mouse4";
    case VK_XBUTTON2: return "Mouse5";
    default: break;
    }
    UINT sc = MapVirtualKeyA(static_cast<UINT>(vk), MAPVK_VK_TO_VSC);
    switch (vk) {
    case VK_INSERT: case VK_DELETE: case VK_HOME: case VK_END: case VK_PRIOR: case VK_NEXT:
    case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN: case VK_DIVIDE: case VK_NUMLOCK:
        sc |= 0x100;
        break;
    default: break;
    }
    if (GetKeyNameTextA(static_cast<LONG>(sc << 16), buf, sizeof(buf)) > 0) return buf;
    std::snprintf(buf, sizeof(buf), "0x%02X", vk);
    return buf;
}

bool IsCapturingKey() { return g_capturing != 0; }

bool KeyBind(const char* label, int* vk, float pillWidth) {
    ImGuiWindow* w = GetCurrentWindow();
    if (w->SkipItems) return false;
    const ImGuiID id = w->GetID(label);
    const char* end = FindRenderedTextEnd(label);
    const float width = GetContentRegionAvail().x;
    const ImVec2 pos = w->DC.CursorPos;
    const ImRect bb(pos, pos + ImVec2(width, 40.f));
    ItemSize(bb);
    if (!ItemAdd(bb, id)) return false;
    const ImRect pill(ImVec2(bb.Max.x - pillWidth, bb.Min.y + 2.f), ImVec2(bb.Max.x, bb.Max.y - 2.f));
    bool hov, held;
    const bool pressed = ButtonBehavior(pill, id, &hov, &held);
    bool changed = false;
    if (pressed && g_capturing != id) {
        g_capturing = id;
        for (int k = 0; k < 256; ++k) g_prevKeys[k] = (GetAsyncKeyState(k) & 0x8000) != 0;
    } else if (g_capturing == id) {
        for (int k = 2; k < 255; ++k) {  // from VK_RBUTTON: the left button is what opened the capture
            if (k == 3) continue;          // VK_CANCEL
            const bool down = (GetAsyncKeyState(k) & 0x8000) != 0;
            if (down && !g_prevKeys[k] && k != VK_SHIFT && k != VK_CONTROL && k != VK_MENU) {
                *vk = (k == VK_ESCAPE || k == VK_BACK) ? 0 : k;
                g_capturing = 0;
                changed = true;
                break;
            }
            g_prevKeys[k] = down;
        }
    }
    ImDrawList* dl = w->DrawList;
    // long labels end with an ellipsis before the key pill instead of running under it
    PushFont(fontRegular, kBodyLarge);
    PushStyleColor(ImGuiCol_Text, ToVec4(S.onSurface));
    RenderTextEllipsis(dl, pos + ImVec2(0, 10), ImVec2(pill.Min.x - 8.f, pos.y + 34.f), pill.Min.x - 8.f, label, end, nullptr);
    PopStyleColor();
    PopFont();
    const bool cap = g_capturing == id;
    const float ct = SpringTo(Slot(id, 3), cap ? 1.f : 0.f, motion::kEffects);
    const float pulse = cap ? 0.5f + 0.5f * std::sin(motion::Time() * 6.f) : 0.f;
    const float r = PressRadius(id, held || cap, 18.f, 10.f);
    const ImU32 fg = Mix(S.onSecondaryContainer, S.onPrimary, ct);
    dl->AddRectFilled(pill.Min, pill.Max, Mix(S.secondaryContainer, S.primary, ct * (0.75f + 0.25f * pulse)), r);
    const float layer = StateLayer(id, hov, held);
    if (layer > 0.003f) dl->AddRectFilled(pill.Min, pill.Max, WithAlpha(fg, layer), r);
    motion::RippleDraw(id, dl, pill, fg, r);
    TextCentered(dl, fontBold, 13.5f, pill.GetCenter(), fg, cap ? L(u8"Нажмите клавишу") : KeyName(*vk));
    return changed;
}

bool SearchField(const char* idStr, const char* hint, char* buf, size_t size) {
    ImGuiWindow* w = GetCurrentWindow();
    if (w->SkipItems) return false;
    const float width = GetContentRegionAvail().x;
    const ImVec2 pos = w->DC.CursorPos;
    ImDrawList* dl = w->DrawList;
    const ImGuiID id = w->GetID(idStr);
    const float focus = SpringTo(Slot(id, 3), GetActiveID() == id ? 1.f : 0.f, motion::kEffects);
    dl->AddRectFilled(pos, pos + ImVec2(width, 48.f), Mix(S.surfaceContainerHigh, S.surfaceContainerHighest, focus), 24.f);
    if (focus > 0.01f) dl->AddRect(pos, pos + ImVec2(width, 48.f), WithAlpha(S.primary, focus), 24.f, 0, 2.f);
    TextCentered(dl, fontRegular, 18.f, pos + ImVec2(26.f, 23.f), Mix(S.onSurfaceVariant, S.primary, focus), icon::Search);
    SetCursorScreenPos(pos + ImVec2(46.f, 8.f));
    PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
    PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 0));
    PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(0, 0, 0, 0));
    PushStyleColor(ImGuiCol_TextDisabled, ToVec4(S.onSurfaceVariant));
    PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 6));
    PushFont(fontRegular, kBodyLarge);
    SetNextItemWidth(width - 62.f);
    const bool changed = InputTextWithHint(idStr, hint, buf, size);
    PopFont();
    PopStyleVar();
    PopStyleColor(4);
    SetCursorScreenPos(pos);
    Dummy(ImVec2(width, 48.f));
    return changed;
}

bool RailItem(const char* ic, const char* label, bool active) {
    ImGuiWindow* w = GetCurrentWindow();
    if (w->SkipItems) return false;
    const ImGuiID id = w->GetID(label);
    const float width = GetContentRegionAvail().x;
    const ImVec2 pos = w->DC.CursorPos;
    const ImRect bb(pos, pos + ImVec2(width, 62.f));
    ItemSize(bb);
    if (!ItemAdd(bb, id)) return false;
    bool hov, held;
    const bool pressed = ButtonBehavior(bb, id, &hov, &held);
    ImDrawList* dl = w->DrawList;
    const float cx = bb.GetCenter().x;
    const ImRect pill(ImVec2(cx - 28.f, pos.y + 4.f), ImVec2(cx + 28.f, pos.y + 36.f));
    const float layer = StateLayer(id, hov && !active, held);
    if (layer > 0.003f) dl->AddRectFilled(pill.Min, pill.Max, WithAlpha(S.onSurface, layer), 16.f);
    motion::RippleDraw(id, dl, pill, S.onSurface, 16.f);

    // icon "pops" when it becomes active (spring from a smaller size with overshoot)
    ImGuiStorage* st = GetStateStorage();
    const bool wasActive = st->GetBool(Slot(id, 7));
    if (active && !wasActive) st->SetFloat(Slot(id, 8), 0.6f);
    st->SetBool(Slot(id, 7), active);
    const float pop = SpringTo(Slot(id, 8), 1.f, motion::kSpatialFast);
    const float at = SpringTo(Slot(id, 9), active ? 1.f : 0.f, motion::kEffects);
    TextCentered(dl, fontRegular, 20.f * pop, ImVec2(cx, pos.y + 19.f), Mix(S.onSurfaceVariant, S.onSecondaryContainer, at), ic);
    TextCentered(dl, active ? fontBold : fontRegular, 12.5f, ImVec2(cx, pos.y + 49.f), Mix(S.onSurfaceVariant, S.onSurface, at), label);
    return pressed;
}

void RailIndicator(ImDrawList* dl, ImVec2 itemTopCenter) {
    // one shared pill that travels between destinations; it widens and stretches while moving
    const float y = g_railY.Update(itemTopCenter.y, motion::kSpatial);
    const float stretch = ImMin(std::fabs(g_railY.velocity) * 0.025f, 14.f);
    const float wpill = g_railW.Update(56.f, motion::kSpatialFast);
    const ImVec2 mn(itemTopCenter.x - wpill * 0.5f + stretch * 0.15f, y + 4.f - stretch * 0.5f);
    const ImVec2 mx(itemTopCenter.x + wpill * 0.5f - stretch * 0.15f, y + 36.f + stretch * 0.5f);
    dl->AddRectFilled(mn, mx, S.secondaryContainer, 16.f);
}

void SetSurfaceAlpha(float a) { g_surfaceAlpha = motion::Clamp01(a); }

void BeginPage(unsigned visit) {
    if (visit != g_visit) {
        g_visit = visit;
        g_visitT0 = motion::Time();
        g_railW.Snap(30.f);  // indicator re-expands on every navigation
    }
    g_cardIndex = 0;
}

bool BeginCard(const char* title, const char* ic, const char* subtitle) {
    const ImGuiID cardId = GetID(title);
    // staggered entrance: card i starts ~45 ms after card i-1, springs up with a soft overshoot
    const int idx = g_cardIndex++;
    const ImGuiID animId = ImHashData(&g_visit, sizeof(g_visit), cardId);
    const bool started = motion::Time() - g_visitT0 >= idx * 0.045f;
    const float t = SpringTo(animId, started ? 1.f : 0.f, motion::kSpatial, 0.f);
    auto it = g_cardRects.find(cardId);
    const bool hovered = it != g_cardRects.end() && IsMouseHoveringRect(it->second.Min, it->second.Max) && IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
    const float hv = SpringTo(Slot(cardId, 3), hovered ? 1.f : 0.f, motion::kEffects);

    PushStyleColor(ImGuiCol_ChildBg, ToVec4(WithAlpha(Mix(S.surfaceContainer, S.surfaceContainerHigh, hv * 0.7f), g_surfaceAlpha)));
    PushStyleVar(ImGuiStyleVar_ChildRounding, 24.f);
    PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.f, 16.f));
    PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.f, 6.f));
    const bool open = BeginChild(title, ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding,
                                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    g_cardStack.push_back({ t, cardId });
    ImGuiWindow* w = GetCurrentWindow();
    ImDrawList* dl = w->DrawList;
    const ImVec2 pos = w->DC.CursorPos;
    const char* end = FindRenderedTextEnd(title);
    float x = pos.x;
    if (ic) {
        // expressive icon container: a cookie shape that turns slightly on hover
        motion::FillShape(dl, ImVec2(x + 17.f, pos.y + 16.f), 17.f, motion::Shape::Cookie9, motion::Shape::Flower, hv * 0.6f, hv * 0.8f,
                          S.primaryContainer);
        TextCentered(dl, fontRegular, 17.f, ImVec2(x + 17.f, pos.y + 15.f), S.onPrimaryContainer, ic);
        x += 44.f;
    }
    if (subtitle) {
        Text(dl, fontBold, kTitle, ImVec2(x, pos.y - 2.f), S.onSurface, title, end);
        Text(dl, fontRegular, kLabel, ImVec2(x, pos.y + 18.f), S.onSurfaceVariant, subtitle);
    } else {
        Text(dl, fontBold, kTitle, ImVec2(x, pos.y + 6.f), S.onSurface, title, end);
    }
    Dummy(ImVec2(0, 38.f));
    return open;
}

void EndCard() {
    const CardCtx ctx = g_cardStack.empty() ? CardCtx{ 1.f, 0 } : g_cardStack.back();
    if (!g_cardStack.empty()) g_cardStack.pop_back();
    // slide + fade the whole card (background included: it lives in the child's own draw list)
    motion::TransformDrawList(GetWindowDrawList(), 0, 0, ImVec2(0.f, (1.f - ctx.t) * 36.f), ctx.t);
    EndChild();
    g_cardRects[ctx.id] = ImRect(GetItemRectMin(), GetItemRectMax());
    PopStyleVar(3);
    PopStyleColor();
    Dummy(ImVec2(0, 4.f));
}

void SectionLabel(const char* text) {
    Dummy(ImVec2(0, 2.f));
    ImGuiWindow* w = GetCurrentWindow();
    const ImVec2 pos = w->DC.CursorPos;
    Text(w->DrawList, fontBold, kLabel, pos, S.primary, text);
    Dummy(ImVec2(0, 18.f));
}

void Supporting(const char* text) {
    PushFont(fontRegular, kLabel + 0.5f);
    PushStyleColor(ImGuiCol_Text, ToVec4(S.onSurfaceVariant));
    TextWrapped("%s", text);
    PopStyleColor();
    PopFont();
}

void Badge(const char* text, ImU32 bg, ImU32 fg) {
    ImGuiWindow* w = GetCurrentWindow();
    if (w->SkipItems) return;
    const ImVec2 ts = TextSize(fontBold, 12.5f, text);
    const ImVec2 pos = w->DC.CursorPos;
    const ImRect bb(pos, pos + ImVec2(ts.x + 20.f, 24.f));
    ItemSize(bb);
    ItemAdd(bb, 0);
    w->DrawList->AddRectFilled(bb.Min, bb.Max, bg, 12.f);
    Text(w->DrawList, fontBold, 12.5f, ImVec2(bb.Min.x + 10.f, bb.GetCenter().y - ts.y * 0.5f), fg, text);
}

void FlowBegin() {
    g_flowMaxX = GetCursorScreenPos().x + GetContentRegionAvail().x;
    g_flowFirst = true;
}

void FlowNext(float nextWidth) {
    if (!g_flowFirst) {
        const float spacing = GetStyle().ItemSpacing.x;
        if (GetItemRectMax().x + spacing + nextWidth <= g_flowMaxX + 0.5f) SameLine(0.f, spacing);
    }
    g_flowFirst = false;
}

} // namespace m3
