#include "esp.h"
#include "config.h"
#include "i18n.h"
#include "state.h"
#include "theme.h"
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include "imgui_internal.h"

using namespace theme;

namespace esp {

namespace {

struct Projector {
    Matrix4x4 view, proj;
    ImVec2 screen;

    static void Mul(const Matrix4x4& m, const float v[4], float out[4]) {
        for (int r = 0; r < 4; ++r)
            out[r] = m.m[0 * 4 + r] * v[0] + m.m[1 * 4 + r] * v[1] + m.m[2 * 4 + r] * v[2] + m.m[3 * 4 + r] * v[3];
    }

    // View-space position (Unity camera space: looks down -Z)
    void ToView(Vec3 p, float out[4]) const {
        const float wp[4] = { p.x, p.y, p.z, 1.f };
        Mul(view, wp, out);
    }

    bool W2S(Vec3 p, ImVec2& out) const {
        float vp[4], clip[4];
        ToView(p, vp);
        Mul(proj, vp, clip);
        if (clip[3] < 0.05f) return false;
        const float nx = clip[0] / clip[3], ny = clip[1] / clip[3];
        out = ImVec2((nx * 0.5f + 0.5f) * screen.x, (1.f - (ny * 0.5f + 0.5f)) * screen.y);
        return true;
    }
};

ImU32 Col(const float c[4], float alphaMul = 1.f) { return ImGui::ColorConvertFloat4ToU32(ImVec4(c[0], c[1], c[2], c[3] * alphaMul)); }

void OutlinedRect(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col) {
    dl->AddRect(a - ImVec2(1, 1), b + ImVec2(1, 1), IM_COL32(0, 0, 0, 140), 3.f, 0, 3.f);
    dl->AddRect(a, b, col, 3.f, 0, 1.6f);
}

void Corners(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col) {
    const float lx = (b.x - a.x) * 0.28f, ly = (b.y - a.y) * 0.22f;
    const ImVec2 pts[4][3] = {
        { ImVec2(a.x, a.y + ly), a, ImVec2(a.x + lx, a.y) },
        { ImVec2(b.x - lx, a.y), ImVec2(b.x, a.y), ImVec2(b.x, a.y + ly) },
        { ImVec2(b.x, b.y - ly), b, ImVec2(b.x - lx, b.y) },
        { ImVec2(a.x + lx, b.y), ImVec2(a.x, b.y), ImVec2(a.x, b.y - ly) },
    };
    for (const auto& p : pts) dl->AddPolyline(p, 3, IM_COL32(0, 0, 0, 150), 0, 4.f);
    for (const auto& p : pts) dl->AddPolyline(p, 3, col, 0, 2.f);
}

// Material-style label pill centred on x
void Pill(ImDrawList* dl, ImVec2 anchor, const char* text, ImU32 dot, float size, bool above) {
    const ImVec2 ts = fontBold->CalcTextSizeA(size, FLT_MAX, 0, text);
    const float padX = 10.f, h = ts.y + 8.f, w = ts.x + padX * 2.f + (dot ? 12.f : 0.f);
    ImVec2 mn(anchor.x - w * 0.5f, above ? anchor.y - h - 4.f : anchor.y + 4.f);
    dl->AddRectFilled(mn, mn + ImVec2(w, h), WithAlpha(S.surfaceContainerHigh, 0.88f), h * 0.5f);
    float x = mn.x + padX;
    if (dot) {
        dl->AddCircleFilled(ImVec2(x + 4.f, mn.y + h * 0.5f), 4.f, dot, 16);
        x += 12.f;
    }
    dl->AddText(fontBold, size, ImVec2(x, mn.y + 4.f), S.onSurface, text);
}

void SmallText(ImDrawList* dl, ImVec2 center, const char* text, ImU32 col, float size = 13.f) {
    const ImVec2 ts = fontRegular->CalcTextSizeA(size, FLT_MAX, 0, text);
    const ImVec2 p(center.x - ts.x * 0.5f, center.y);
    dl->AddText(fontRegular, size, p + ImVec2(1, 1), IM_COL32(0, 0, 0, 170), text);
    dl->AddText(fontRegular, size, p, col, text);
}

void OffscreenArrow(ImDrawList* dl, const Projector& pr, Vec3 pos, ImU32 col, const char* label) {
    float v[4];
    pr.ToView(pos, v);
    ImVec2 dir(v[0], -v[1]);
    const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
    if (len < 0.001f) return;
    dir = dir / len;
    const ImVec2 c = pr.screen * 0.5f;
    const float radius = ImMin(pr.screen.x, pr.screen.y) * 0.40f;
    const ImVec2 tip = c + dir * (radius + 14.f);
    const ImVec2 base = c + dir * radius;
    const ImVec2 perp(-dir.y, dir.x);
    dl->AddTriangleFilled(tip + dir * 1.5f, base + perp * 10.f - dir * 1.f, base - perp * 10.f - dir * 1.f, IM_COL32(0, 0, 0, 120));
    dl->AddTriangleFilled(tip, base + perp * 9.f, base - perp * 9.f, col);
    SmallText(dl, base - dir * 14.f - ImVec2(0, 7), label, col, 12.5f);
}

std::string Lower(std::string s) {
    for (char& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}

void DrawRadar(ImDrawList* dl, const Snapshot& s) {
    const float size = g_cfg.radarSize, r = size * 0.5f;
    const ImVec2 c(ImGui::GetIO().DisplaySize.x - r - 24.f, r + 24.f);
    dl->AddCircleFilled(c, r + 2.f, WithAlpha(S.surfaceContainerLowest, 0.55f), 64);
    dl->AddCircleFilled(c, r, WithAlpha(S.surfaceContainer, 0.82f), 64);
    dl->AddCircle(c, r * 0.5f, WithAlpha(S.outlineVariant, 0.8f), 48, 1.f);
    dl->AddCircle(c, r, WithAlpha(S.outline, 0.9f), 64, 1.5f);
    dl->AddLine(c - ImVec2(r, 0), c + ImVec2(r, 0), WithAlpha(S.outlineVariant, 0.6f));
    dl->AddLine(c - ImVec2(0, r), c + ImVec2(0, r), WithAlpha(S.outlineVariant, 0.6f));

    float fx = s.camFwd.x, fz = s.camFwd.z;
    const float fl = std::sqrt(fx * fx + fz * fz);
    if (fl < 0.001f) return;
    fx /= fl;
    fz /= fl;
    const float rx = fz, rz = -fx;  // right vector in XZ (Unity is left-handed)
    const float scale = r / g_cfg.radarRange;

    auto plot = [&](Vec3 p, ImU32 col, float dotR) {
        const float dx = p.x - s.playerPos.x, dz = p.z - s.playerPos.z;
        ImVec2 o((dx * rx + dz * rz) * scale, -(dx * fx + dz * fz) * scale);
        const float d = std::sqrt(o.x * o.x + o.y * o.y);
        if (d > r - dotR) o = o * ((r - dotR) / d);
        dl->AddCircleFilled(c + o, dotR + 1.5f, IM_COL32(0, 0, 0, 150), 16);
        dl->AddCircleFilled(c + o, dotR, col, 16);
    };
    if (g_cfg.espItems)
        for (const ItemView& it : s.items)
            if (it.dist <= g_cfg.radarRange) plot(it.pos, Col(g_cfg.itemColor, 0.85f), 2.5f);
    for (const EnemyView& e : s.enemies) plot(e.pos, Col(g_cfg.enemyColor), 4.5f);
    // player: view cone + arrow
    dl->PathArcTo(c, r * 0.9f, -IM_PI * 0.5f - 0.5f, -IM_PI * 0.5f + 0.5f, 16);
    dl->PathLineTo(c);
    dl->PathFillConvex(WithAlpha(S.primary, 0.14f));
    dl->AddTriangleFilled(c + ImVec2(0, -8), c + ImVec2(6, 6), c + ImVec2(-6, 6), S.primary);
}

} // namespace

void Draw() {
    const Snapshot s = state::Get();
    if (!s.inGame || state::NowMs() - s.tickMs > 400) return;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    Projector pr{ s.view, s.proj, ImGui::GetIO().DisplaySize };

    // ---- items
    if (g_cfg.espItems) {
        const std::string filter = Lower(g_cfg.itemFilter);
        const ImU32 col = Col(g_cfg.itemColor);
        for (const ItemView& it : s.items) {
            if (it.dist > g_cfg.itemMaxDist) break;  // sorted by distance
            if (!filter.empty() && Lower(it.name).find(filter) == std::string::npos) continue;
            ImVec2 p;
            if (!pr.W2S(it.pos, p)) continue;
            const float a = ImClamp(1.25f - it.dist / g_cfg.itemMaxDist, 0.35f, 1.f);
            dl->AddCircleFilled(p, 4.5f, IM_COL32(0, 0, 0, static_cast<int>(150 * a)), 16);
            dl->AddCircleFilled(p, 3.f, Col(g_cfg.itemColor, a), 16);
            char buf[128];
            if (g_cfg.itemDistance) std::snprintf(buf, sizeof(buf), L(u8"%s · %.0f м"), it.name.c_str(), it.dist);
            else std::snprintf(buf, sizeof(buf), "%s", it.name.c_str());
            SmallText(dl, p + ImVec2(0, 6), buf, WithAlpha(col, a));
        }
    }

    // ---- enemies
    if (g_cfg.espEnemies) {
        const ImU32 col = Col(g_cfg.enemyColor);
        for (const EnemyView& e : s.enemies) {
            if (e.dist > g_cfg.enemyMaxDist) continue;
            ImVec2 mn(FLT_MAX, FLT_MAX), mx(-FLT_MAX, -FLT_MAX);
            bool visible = true;
            if (e.hasBounds) {
                for (int i = 0; i < 8; ++i) {
                    const Vec3 corner{ (i & 1) ? e.bmax.x : e.bmin.x, (i & 2) ? e.bmax.y : e.bmin.y, (i & 4) ? e.bmax.z : e.bmin.z };
                    ImVec2 p;
                    if (!pr.W2S(corner, p)) {
                        visible = false;
                        break;
                    }
                    mn = ImMin(mn, p);
                    mx = ImMax(mx, p);
                }
            } else {
                ImVec2 feet, head;
                visible = pr.W2S(e.pos, feet) && pr.W2S(e.pos + Vec3{ 0, 1.8f, 0 }, head);
                if (visible) {
                    const float h = feet.y - head.y, w = h * 0.45f;
                    mn = ImVec2(feet.x - w * 0.5f, head.y);
                    mx = ImVec2(feet.x + w * 0.5f, feet.y);
                }
            }
            const bool onScreen = visible && mx.x > 0 && mn.x < pr.screen.x && mx.y > 0 && mn.y < pr.screen.y;
            if (!onScreen) {
                if (g_cfg.enemyOffscreen) {
                    char lbl[64];
                    std::snprintf(lbl, sizeof(lbl), L(u8"%s %.0fм"), e.name.c_str(), e.dist);
                    OffscreenArrow(dl, pr, e.pos, col, lbl);
                }
                continue;
            }
            if (mx.x - mn.x < 6.f) { const float cx = (mn.x + mx.x) * 0.5f; mn.x = cx - 3.f; mx.x = cx + 3.f; }

            if (g_cfg.enemySnaplines) {
                dl->AddLine(ImVec2(pr.screen.x * 0.5f, pr.screen.y), ImVec2((mn.x + mx.x) * 0.5f, mx.y), IM_COL32(0, 0, 0, 120), 3.f);
                dl->AddLine(ImVec2(pr.screen.x * 0.5f, pr.screen.y), ImVec2((mn.x + mx.x) * 0.5f, mx.y), WithAlpha(col, 0.8f), 1.5f);
            }
            const ImU32 boxCol = e.frozen ? S.tertiary : col;
            if (g_cfg.enemyBox == static_cast<int>(BoxStyle::Full)) {
                dl->AddRectFilled(mn, mx, WithAlpha(boxCol, 0.07f), 3.f);
                OutlinedRect(dl, mn, mx, boxCol);
            } else if (g_cfg.enemyBox == static_cast<int>(BoxStyle::Corners)) {
                Corners(dl, mn, mx, boxCol);
            }
            if (g_cfg.enemyName) Pill(dl, ImVec2((mn.x + mx.x) * 0.5f, mn.y), e.name.c_str(), boxCol, 13.f, true);
            float y = mx.y + 2.f;
            if (g_cfg.enemyDistance) {
                char buf[32];
                std::snprintf(buf, sizeof(buf), L(u8"%.0f м"), e.dist);
                SmallText(dl, ImVec2((mn.x + mx.x) * 0.5f, y), buf, S.onSurface);
                y += 16.f;
            }
            if (g_cfg.enemyState && !e.state.empty()) SmallText(dl, ImVec2((mn.x + mx.x) * 0.5f, y), e.state.c_str(), boxCol, 12.5f);
        }
    }

    // ---- aimbot FOV + target marker
    if (g_cfg.aimCamera || g_cfg.silentAim) {
        const ImVec2 c = pr.screen * 0.5f;
        if (g_cfg.aimDrawFov && g_cfg.aimFov < 89.f) {
            // Unity's fieldOfView is vertical
            const float r = std::tan(g_cfg.aimFov * 3.14159265f / 180.f) / std::tan(s.camFov * 0.5f * 3.14159265f / 180.f) * pr.screen.y * 0.5f;
            if (r < pr.screen.x) {
                dl->AddCircle(c, r, IM_COL32(0, 0, 0, 110), 96, 3.f);
                dl->AddCircle(c, r, WithAlpha(S.primary, 0.85f), 96, 1.5f);
            }
        }
        ImVec2 p;
        if (s.aimHasTarget && pr.W2S(s.aimTarget, p)) {
            dl->AddCircle(p, 9.f, IM_COL32(0, 0, 0, 160), 24, 4.f);
            dl->AddCircle(p, 9.f, S.primary, 24, 2.f);
            dl->AddCircleFilled(p, 2.5f, S.primary, 12);
        }
    }

    if (g_cfg.radar) DrawRadar(dl, s);
}

} // namespace esp
