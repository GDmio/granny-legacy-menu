#include "motion.h"
#include <cmath>

namespace motion {

namespace {

constexpr float kPi = 3.14159265f;

void Integrate(float& x, float& v, float target, SpringSpec s) {
    const float dt = ImMin(ImGui::GetIO().DeltaTime, 1.f / 20.f);
    const int steps = ImMax(1, static_cast<int>(std::ceil(dt * 240.f)));
    const float h = dt / steps;
    const float c = 2.f * s.damping * std::sqrt(s.stiffness);
    for (int i = 0; i < steps; ++i) {
        const float a = -s.stiffness * (x - target) - c * v;
        v += a * h;
        x += v * h;
    }
    if (std::fabs(x - target) < 1e-3f && std::fabs(v) < 1e-2f) {
        x = target;
        v = 0.f;
    }
}

struct Ripple { ImGuiID id = 0; ImVec2 pos; float t0 = -10.f; };
Ripple g_ripples[24];

// Normalised outline radius of a shape at angle th (average ~1)
float ShapeRadius(Shape s, float th) {
    switch (s) {
    case Shape::Circle: return 1.f;
    case Shape::Cookie4: return (1.f + 0.13f * std::cos(4.f * th)) / 1.02f;
    case Shape::Cookie6: return (1.f + 0.09f * std::cos(6.f * th)) / 1.01f;
    case Shape::Cookie9: return (1.f + 0.065f * std::cos(9.f * th)) / 1.01f;
    case Shape::Pentagon: return (1.f + 0.07f * std::cos(5.f * th)) / 1.01f;
    case Shape::Pill: {
        const float a = 1.f, b = 0.64f, c = std::cos(th), s2 = std::sin(th);
        return 1.08f / std::sqrt((c * c) / (a * a) + (s2 * s2) / (b * b));
    }
    case Shape::Sunny: {
        const float k = std::cos(8.f * th);
        return (1.f + 0.11f * k * std::fabs(k)) / 1.0f;
    }
    case Shape::Clover: return (0.86f + 0.24f * std::fabs(std::cos(2.f * th))) / 1.0f;
    case Shape::Flower: return (0.9f + 0.17f * std::fabs(std::cos(4.f * th))) / 1.0f;
    default: return 1.f;
    }
}

float EaseOutBack(float t) {
    const float c1 = 1.4f, c3 = c1 + 1.f;
    const float u = t - 1.f;
    return 1.f + c3 * u * u * u + c1 * u * u;
}

} // namespace

float Time() { return static_cast<float>(ImGui::GetTime()); }

float Spring::Update(float target, SpringSpec spec) {
    if (!init) Snap(target);
    Integrate(value, velocity, target, spec);
    return value;
}

float SpringTo(ImGuiID id, float target, SpringSpec spec, float start) {
    ImGuiStorage* st = ImGui::GetStateStorage();
    const ImGuiID vid = id ^ 0x5bd1e995u, iid = id ^ 0x27d4eb2fu;
    if (!st->GetBool(iid)) {
        st->SetBool(iid, true);
        st->SetFloat(id, start > -1e29f ? start : target);
        st->SetFloat(vid, 0.f);
    }
    float x = st->GetFloat(id), v = st->GetFloat(vid);
    Integrate(x, v, target, spec);
    st->SetFloat(id, x);
    st->SetFloat(vid, v);
    return x;
}

float SpringVelocity(ImGuiID id) { return ImGui::GetStateStorage()->GetFloat(id ^ 0x5bd1e995u); }

void RippleStart(ImGuiID id, ImVec2 pos) {
    Ripple* slot = &g_ripples[0];
    for (Ripple& r : g_ripples) {
        if (r.id == id) { slot = &r; break; }
        if (r.t0 < slot->t0) slot = &r;
    }
    *slot = { id, pos, Time() };
}

namespace {

// Convex outline of a rounded rectangle (counter-clockwise in screen space)
int RoundedRectPoly(const ImRect& bb, float rounding, ImVec2* out) {
    rounding = ImMin(rounding, ImMin(bb.GetWidth(), bb.GetHeight()) * 0.5f);
    constexpr int seg = 8;
    const ImVec2 centers[4] = { ImVec2(bb.Max.x - rounding, bb.Max.y - rounding), ImVec2(bb.Min.x + rounding, bb.Max.y - rounding),
                                ImVec2(bb.Min.x + rounding, bb.Min.y + rounding), ImVec2(bb.Max.x - rounding, bb.Min.y + rounding) };
    int n = 0;
    for (int c = 0; c < 4; ++c)
        for (int i = 0; i <= seg; ++i) {
            const float a = (c + static_cast<float>(i) / seg) * kPi * 0.5f;
            out[n++] = ImVec2(centers[c].x + std::cos(a) * rounding, centers[c].y + std::sin(a) * rounding);
        }
    return n;
}

// Sutherland-Hodgman: clip polygon `subject` by the convex polygon `clip` (same winding)
int ClipConvex(const ImVec2* subject, int ns, const ImVec2* clip, int nc, ImVec2* out, ImVec2* tmp) {
    int n = ns;
    for (int i = 0; i < n; ++i) out[i] = subject[i];
    for (int e = 0; e < nc && n > 0; ++e) {
        const ImVec2 a = clip[e], b = clip[(e + 1) % nc];
        auto inside = [&](ImVec2 p) { return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x) >= 0.f; };
        auto cross = [&](ImVec2 p, ImVec2 q) {
            const float d1 = (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
            const float d2 = (b.x - a.x) * (q.y - a.y) - (b.y - a.y) * (q.x - a.x);
            const float t = d1 / (d1 - d2);
            return ImVec2(p.x + (q.x - p.x) * t, p.y + (q.y - p.y) * t);
        };
        int m = 0;
        for (int i = 0; i < n; ++i) {
            const ImVec2 p = out[i], q = out[(i + 1) % n];
            const bool ip = inside(p), iq = inside(q);
            if (ip) tmp[m++] = p;
            if (ip != iq) tmp[m++] = cross(p, q);
        }
        n = m;
        for (int i = 0; i < n; ++i) out[i] = tmp[i];
    }
    return n;
}

} // namespace

void RippleDraw(ImGuiID id, ImDrawList* dl, const ImRect& bb, ImU32 color, float rounding) {
    const float now = Time();
    for (const Ripple& r : g_ripples) {
        if (r.id != id) continue;
        const float age = now - r.t0;
        if (age < 0.f || age > 0.7f) continue;
        const ImVec2 far(ImMax(std::fabs(r.pos.x - bb.Min.x), std::fabs(r.pos.x - bb.Max.x)),
                         ImMax(std::fabs(r.pos.y - bb.Min.y), std::fabs(r.pos.y - bb.Max.y)));
        const float maxR = std::sqrt(far.x * far.x + far.y * far.y);
        const float grow = 1.f - std::pow(1.f - Clamp01(age / 0.45f), 3.f);
        const float fade = 1.f - Clamp01((age - 0.2f) / 0.5f);
        ImVec4 c = ImGui::ColorConvertU32ToFloat4(color);
        c.w *= 0.14f * fade;
        // circle ∩ rounded rect, so the wave never shows square corners
        constexpr int kCircle = 48;
        ImVec2 circle[kCircle], shape[40], out[160], tmp[160];
        const float rad = ImMax(2.f, maxR * grow);
        for (int i = 0; i < kCircle; ++i) {
            const float a = 2.f * kPi * i / kCircle;
            circle[i] = ImVec2(r.pos.x + std::cos(a) * rad, r.pos.y + std::sin(a) * rad);
        }
        const int ns = RoundedRectPoly(bb, rounding, shape);
        const int n = ClipConvex(circle, kCircle, shape, ns, out, tmp);
        if (n >= 3) dl->AddConvexPolyFilled(out, n, ImGui::ColorConvertFloat4ToU32(c));
    }
}

void FillShape(ImDrawList* dl, ImVec2 c, float radius, Shape a, Shape b, float t, float rotation, ImU32 col) {
    constexpr int N = 120;
    ImVec2 pts[N];
    for (int i = 0; i < N; ++i) {
        const float th = 2.f * kPi * i / N;
        const float r = radius * ((1.f - t) * ShapeRadius(a, th) + t * ShapeRadius(b, th));
        pts[i] = ImVec2(c.x + std::cos(th + rotation) * r, c.y + std::sin(th + rotation) * r);
    }
    dl->AddConcavePolyFilled(pts, N, col);
}

void LoadingIndicator(ImDrawList* dl, ImVec2 c, float radius, ImU32 col, ImU32 containerCol) {
    // M3 Expressive loading indicator: continuously rotating shape that morphs through a sequence
    static const Shape seq[] = { Shape::Cookie9, Shape::Pentagon, Shape::Pill, Shape::Sunny, Shape::Cookie4, Shape::Clover, Shape::Flower };
    constexpr int n = sizeof(seq) / sizeof(seq[0]);
    const float period = 0.65f, now = Time();
    const int idx = static_cast<int>(now / period);
    const float local = std::fmod(now, period) / period;
    const float morph = EaseOutBack(Clamp01(local / 0.75f));
    const float rotation = now * 2.2f + (idx + morph) * (kPi / 2.f);  // spins, with a kick on every morph
    if (containerCol) dl->AddCircleFilled(c, radius * 1.45f, containerCol, 48);
    FillShape(dl, c, radius, seq[idx % n], seq[(idx + 1) % n], morph, rotation, col);
}

void WavyProgress(ImDrawList* dl, ImVec2 left, float width, float progress, ImU32 active, ImU32 track, float time) {
    progress = Clamp01(progress);
    const float amp = 2.2f, wavelength = 18.f, gap = 6.f, thick = 3.f;
    const float split = left.x + width * progress;
    if (progress > 0.01f) {
        ImVec2 pts[160];
        int n = 0;
        const int steps = ImClamp(static_cast<int>(width * progress / 2.f), 2, 159);
        for (int i = 0; i <= steps; ++i) {
            const float x = left.x + (split - left.x) * i / steps;
            pts[n++] = ImVec2(x, left.y + std::sin((x - left.x) / wavelength * 2.f * kPi - time * 6.f) * amp);
        }
        dl->AddPolyline(pts, n, active, 0, thick);
    }
    if (split + gap < left.x + width) {
        dl->AddLine(ImVec2(split + gap, left.y), ImVec2(left.x + width, left.y), track, thick);
        dl->AddCircleFilled(ImVec2(left.x + width, left.y), thick * 0.7f, active, 12);  // stop indicator
    }
}

void SoftShadow(ImDrawList* dl, ImVec2 mn, ImVec2 mx, float rounding, float elevation, float opacity) {
    // key + ambient shadow: concentric rounded rects, larger ones fainter, slightly offset downwards
    constexpr int layers = 10;
    const float drop = elevation * 0.45f;
    for (int i = layers; i >= 1; --i) {
        const float k = static_cast<float>(i) / layers;
        const float spread = elevation * k;
        const float a = opacity * 0.055f * (1.f - k * 0.85f);
        const ImVec2 off(0.f, drop * (0.4f + 0.6f * k));
        dl->AddRectFilled(ImVec2(mn.x - spread, mn.y - spread) + off, ImVec2(mx.x + spread, mx.y + spread) + off,
                          IM_COL32(0, 0, 0, static_cast<int>(255 * Clamp01(a))), rounding + spread);
    }
}

void GradientRoundedRect(ImDrawList* dl, ImVec2 mn, ImVec2 mx, float rounding, ImU32 top, ImU32 bottom, ImDrawFlags flags) {
    const int start = dl->VtxBuffer.Size;
    dl->AddRectFilled(mn, mx, top, rounding, flags);
    const ImVec4 a = ImGui::ColorConvertU32ToFloat4(top), b = ImGui::ColorConvertU32ToFloat4(bottom);
    const float h = ImMax(1.f, mx.y - mn.y);
    for (int i = start; i < dl->VtxBuffer.Size; ++i) {
        ImDrawVert& v = dl->VtxBuffer[i];
        const float t = Clamp01((v.pos.y - mn.y) / h);
        v.col = ImGui::ColorConvertFloat4ToU32(ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t));
    }
}

void FadeWindowTree(ImGuiWindow* window, float alpha) {
    if (!window || alpha >= 0.999f) return;
    TransformDrawList(window->DrawList, 0, 0, ImVec2(0, 0), alpha);
    for (ImGuiWindow* child : window->DC.ChildWindows) FadeWindowTree(child, alpha);
}

void TransformDrawList(ImDrawList* dl, int vtxStart, int cmdStart, ImVec2 offset, float alpha) {
    if (!dl) return;
    const bool move = offset.x != 0.f || offset.y != 0.f;
    if (!move && alpha >= 0.999f) return;
    alpha = Clamp01(alpha);
    for (int i = vtxStart; i < dl->VtxBuffer.Size; ++i) {
        ImDrawVert& v = dl->VtxBuffer[i];
        v.pos.x += offset.x;
        v.pos.y += offset.y;
        if (alpha < 0.999f) {
            const ImU32 a = static_cast<ImU32>(((v.col >> IM_COL32_A_SHIFT) & 0xFF) * alpha);
            v.col = (v.col & ~IM_COL32_A_MASK) | (a << IM_COL32_A_SHIFT);
        }
    }
    if (move)
        for (int i = cmdStart; i < dl->CmdBuffer.Size; ++i) {
            ImVec4& r = dl->CmdBuffer[i].ClipRect;
            r.x += offset.x; r.z += offset.x;
            r.y += offset.y; r.w += offset.y;
        }
}

} // namespace motion
