#include "game.h"
#include "config.h"
#include "i18n.h"
#include "log.h"
#include "state.h"
#include "unity.h"
#include <MinHook.h>
#include <Windows.h>
#include <intrin.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace game {

std::atomic<bool> g_menuOpen{ false };

namespace {

using MI = const void*;
constexpr const char* kAsm = "Assembly-CSharp.dll";

// ---------------------------------------------------------------------------
// Metadata resolved by name (no hardcoded RVAs / offsets)
// ---------------------------------------------------------------------------
#define GAME_CLASSES(X) \
    X(MobileFPS) X(PickRay) X(Inventory) X(ItemDefs) X(ItemSeedData) X(PlayerStatus) X(EnemyController) \
    X(Eyes_Granny) X(Eyes_Grandpa) X(Eyes_MomSpider) X(AI_Granny) X(AI_Grandpa) X(AI_MomSpider) X(FallingHolder) X(Outline) X(Paused)

#define GAME_FIELDS(X) \
    X(MobileFPS, playerCamera) X(MobileFPS, characterController) X(MobileFPS, playerCamera2) X(MobileFPS, PS) \
    X(PickRay, Drop1) X(PickRay, Ring) X(PickRay, IsCharged) X(PickRay, LoadedGun) X(PickRay, AmmoAmount) \
    X(PickRay, SprayLeft) X(PickRay, DartHasEnergy) X(PickRay, H_SG) X(PickRay, H_DSH) X(PickRay, H_CR) \
    X(PickRay, H_SPRAY) X(PickRay, H_FT) X(PickRay, H_PTRAP) X(PickRay, H_BATON) X(PickRay, Inventory) X(PickRay, ShootKey) \
    X(PickRay, buttonClicked) X(PickRay, T_NeedShit) \
    X(Inventory, ItemDefs) X(ItemDefs, itemName) X(ItemDefs, handObject) X(ItemDefs, textHighlighted) \
    X(ItemSeedData, itemName) X(PlayerStatus, EC) \
    X(Eyes_Granny, AI) X(Eyes_Grandpa, AI) X(Eyes_MomSpider, ViewRange) X(AI_Granny, IsBlind) X(AI_Grandpa, IsBlind) \
    X(FallingHolder, fallDuration) X(FallingHolder, isFalling) X(Outline, renderers) \
    X(PlayerStatus, HidingUnderThings) X(PlayerStatus, IsJumpscared) X(PlayerStatus, Killed) \
    X(MobileFPS, isAllowedToMove) X(MobileFPS, AbleToMove) X(MobileFPS, InWeb) X(AI_MomSpider, PlayerClose) \
    X(MobileFPS, rotationX) X(MobileFPS, minXRotation) X(MobileFPS, maxXRotation) \
    X(PickRay, RayShoot1) X(PickRay, RayShoot2) X(PickRay, SpawnDart)

namespace cls {
#define DECL_CLASS(name) void* name = nullptr;
GAME_CLASSES(DECL_CLASS)
#undef DECL_CLASS
}
namespace off {
#define DECL_FIELD(c, f) int c##_##f = -1;
GAME_FIELDS(DECL_FIELD)
#undef DECL_FIELD
}

struct Methods {
    void (*CheckItemDropping)(void*, MI) = nullptr;
    void (*PickShotgun)(void*, MI) = nullptr;
    void (*ShootGun1)(void*, MI) = nullptr;
    void (*ShootGun2)(void*, MI) = nullptr;
    void (*TurnOffItemTexts)(void*, MI) = nullptr;
    void (*PickBaton)(void*, MI) = nullptr;
    void (*PickupItem)(void*, Il2CppString*, MI) = nullptr;
    void (*GrannyDeath)(void*, bool, MI) = nullptr;
    void (*GrandpaDeath)(void*, bool, MI) = nullptr;
    void (*OutlineSetMode)(void*, int, MI) = nullptr;
    void (*OutlineSetColor)(void*, Color, MI) = nullptr;
    void (*OutlineSetWidth)(void*, float, MI) = nullptr;
} m;

void* g_pausedField = nullptr;

// ---------------------------------------------------------------------------
// Enemy catalogue
// ---------------------------------------------------------------------------
enum class Kill { None, Granny, Grandpa };

struct EnemyType {
    const char* cls;
    const char* display;
    Kill kill = Kill::None;
    void* klass = nullptr;
    void* type = nullptr;
    int offAgent = -1, offAnim = -1;
    int offSpeed[4] = { -1, -1, -1, -1 };
    int offChasing = -1, offSearching = -1, offAngry = -1, offIdle = -1, offDying = -1, offTargeting = -1,
        offLunging = -1, offEating = -1, offHit = -1;
    const void* miKnockout = nullptr;
    const void* miPepper = nullptr;
    const void* miFreezeFx = nullptr;
    const void* miChase = nullptr;
    const void* miStopChase = nullptr;
};

EnemyType g_types[] = {
    { "AI_Granny", "Granny", Kill::Granny },
    { "AI_Grandpa", "Grandpa", Kill::Grandpa },
    { "AI_MomSpider", u8"Мама-паук" },
    { "AI_Slendrina", "Slendrina" },
    { "AtticSpider", u8"Паук на чердаке" },
    { "SpectralGranny", u8"Призрачная Granny" },
    { "SunnyGrannyEnemy", "Sunny Granny" },
    { "AI_RobotEnemy", u8"Робот" },
    { "LittleFucker", u8"Малыш" },
    { "RatEnemy", u8"Крыса" },
    { "CrowManage", u8"Ворона" },
    { "LeprechaunAI", u8"Лепрекон" },
    { "SnowManAI", u8"Снеговик" },
    { "Pumpkin", u8"Тыква" },
    { "UndeadBunny", u8"Зомби-кролик" },
};

void* g_typeItemSeed = nullptr;
void* g_typeOutline = nullptr;

// ---------------------------------------------------------------------------
// Tracked scene objects
// ---------------------------------------------------------------------------
struct ChamsState {
    ChamsMode mode = ChamsMode::Off;
    void* outline = nullptr;
    uint32_t hOutline = 0;
    uint32_t hOutlineRenderers = 0;  // keeps our filtered Renderer[] alive (stored into Outline.renderers)
    struct Saved { void* renderer; void* mats; uint32_t handle; };
    std::vector<Saved> saved;
    Color color{};
    float width = 0.f;
    ChamsMode failed = ChamsMode::Off;  // mode that could not be applied (don't retry every frame)
};

struct Tracked {
    void* comp = nullptr;
    uint32_t hComp = 0;
    int type = -1;  // index into g_types, -1 for items
    void* go = nullptr;
    void* transform = nullptr;
    void* agent = nullptr;
    void* anim = nullptr;
    Il2CppArray* rendArray = nullptr;
    uint32_t hRend = 0;
    std::vector<void*> renderers;
    std::string name;
    bool frozen = false, freezeSingle = false;
    bool wasEnabled = true, animWasEnabled = true, agentWasStopped = false;
    float origSpeed[4]{}, lastSpeed[4]{};
    bool speedInit = false;
    uint64_t stuckSince = 0;  // godmode: time the enemy got stuck in its "caught the player" state
    ChamsState chams;
};

// Granny and Grandpa share the same catch logic (field names match, offsets differ)
struct HouseAI {
    void* klass = nullptr;
    int caught = -1, timerBed = -1, catchRadius = -1, agent = -1, bc = -1;
    const void* miReset = nullptr;
    const void* miStopChase = nullptr;
};
HouseAI g_houseAI[2];  // [0] Granny, [1] Grandpa (same order as g_types)
const void* g_miMomStopChase = nullptr;
uint64_t g_jumpscareSince = 0, g_lastMomRelease = 0;

// Aimbot: target chosen once per frame on the main thread, used by the camera and silent-aim hooks
bool g_aimHas = false;
Vec3 g_aimPoint{};
constexpr float kRad = 3.14159265f / 180.f;

Quat QuatFromPitchYaw(float pitchDeg, float yawDeg) {
    // Unity's Euler order is Z, X, Y: q = qYaw * qPitch (no roll)
    const float hp = pitchDeg * kRad * 0.5f, hy = yawDeg * kRad * 0.5f;
    const float sx = std::sin(hp), cx = std::cos(hp), sy = std::sin(hy), cy = std::cos(hy);
    return { cy * sx, sy * cx, -sy * sx, cy * cx };
}

void DirToAngles(Vec3 d, float& pitchDeg, float& yawDeg) {
    yawDeg = std::atan2(d.x, d.z) / kRad;
    pitchDeg = -std::atan2(d.y, std::sqrt(d.x * d.x + d.z * d.z)) / kRad;  // Unity: positive pitch looks down
}

Quat LookAt(Vec3 from, Vec3 to) {
    float p, y;
    DirToAngles(to - from, p, y);
    return QuatFromPitchYaw(p, y);
}

float DeltaAngle(float from, float to) {
    float d = std::fmod(to - from, 360.f);
    if (d > 180.f) d -= 360.f;
    if (d < -180.f) d += 360.f;
    return d;
}

std::unordered_map<void*, Tracked> g_enemies;
std::unordered_map<void*, Tracked> g_items;
std::vector<GiveEntry> g_giveList;

void* g_player = nullptr;       // MobileFPS
void* g_lastPlayer = nullptr;
void* g_pickRay = nullptr;
void* g_playerCC = nullptr;

bool g_mainInit = false;
std::atomic<bool> g_cleanupRequested{ false };
std::atomic<bool> g_cleanupDone{ false };
uint64_t g_lastRefresh = 0, g_lastGiveRefresh = 0, g_lastShot = 0, g_lastGodToast = 0;


// Player feature bookkeeping
bool g_noclipApplied = false;
bool g_timescaleApplied = false;
bool g_fovApplied = false; float g_fovOrig = 60.f;
bool g_occlusionOff = false;
bool g_brightApplied = false; int g_ambientModeOrig = 0; Color g_ambientOrig{};
bool g_fogApplied = false; bool g_fogOrig = false;
bool g_cursorSaved = false;
// Cursor.lockState / Cursor.visible are hooked: while the menu is open the game cannot re-lock or hide
// the cursor; its latest requests are remembered and restored when the menu closes.
void (*oCursorSetLock)(int, MI) = nullptr;
void (*oCursorSetVisible)(bool, MI) = nullptr;
int g_gameCursorLock = -1, g_gameCursorVisible = -1;
void CursorLockRaw(int v) { oCursorSetLock ? oCursorSetLock(v, nullptr) : unity::SetCursorLock(v); }
void CursorVisibleRaw(bool v) { oCursorSetVisible ? oCursorSetVisible(v, nullptr) : unity::SetCursorVisible(v); }
bool g_hasSaved = false; Vec3 g_savedPoint{};

// Rapid fire / key spoof state (main thread only)
bool g_inPickRay = false;
int g_spoofKey = -1;

// Auto-use: every "hand" object (an item held in the player's hand) and its display name
std::unordered_map<void*, std::string> g_hands;
void* g_pickRayUpdate = nullptr;    // start of PickRay.Update, to recognise direct calls from it
bool g_clickArmed = false;          // the interact click was registered during this PickRay.Update
std::vector<void*> g_missingHands;  // hand objects PickRay.Update asked about (not held), in order
std::vector<void*> g_doneHands;     // hands auto-use already applied successfully on this level
void* g_requiredHand = nullptr;     // hand the game complained about ("I need ...")
struct AutoUse {
    void* required = nullptr;
    void* previous = nullptr;
    bool running = false;
    uint64_t startMs = 0;
    uint64_t clickedMs = 0;  // when the game consumed the replayed click (0 = not yet)
} g_auto;
std::vector<std::pair<int, std::string>> g_handFields;  // every PickRay.H_* field (offset, name)

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
inline void CallMI(const void* mi, void* self) {
    if (mi) reinterpret_cast<void (*)(void*, MI)>(*reinterpret_cast<void* const*>(mi))(self, mi);
}

bool IsPaused() {
    bool paused = false;
    if (g_pausedField && il2cpp::StaticsReady(cls::Paused)) il2cpp::StaticFieldValue(g_pausedField, &paused);
    return paused;
}

Color ToColor(const float c[4]) { return { c[0], c[1], c[2], c[3] }; }
bool SameColor(Color a, Color b) { return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a; }

void* PickRayObj(int fieldOff) {
    if (!il2cpp::Alive(g_pickRay) || fieldOff < 0) return nullptr;
    return FieldAt<void*>(g_pickRay, fieldOff);
}

bool HandActive(int fieldOff) { return unity::GO_ActiveSelf(PickRayObj(fieldOff)); }

void* PlayerTransform() { return il2cpp::Alive(g_player) ? unity::GetTransform(g_player) : nullptr; }

Vec3 PlayerPos() { return unity::GetPosition(PlayerTransform()); }

Vec3 CameraForwardFlat() {
    void* camT = il2cpp::Alive(g_player) ? FieldAt<void*>(g_player, off::MobileFPS_playerCamera) : nullptr;
    Vec3 f = unity::GetForward(camT);
    f.y = 0;
    float len = Length(f);
    return len > 0.001f ? f * (1.f / len) : Vec3{ 0, 0, 1 };
}

void SetPlayerPos(Vec3 p) {
    void* t = PlayerTransform();
    if (!t) return;
    const bool ccOn = unity::ColliderGetEnabled(g_playerCC);
    if (ccOn) unity::ColliderSetEnabled(g_playerCC, false);
    unity::SetPosition(t, p);
    if (ccOn) unity::ColliderSetEnabled(g_playerCC, true);
}

// Particles, light cones and FX planes must never get chams (they turn into huge growing quads),
// so enemies use only their skinned body meshes; static meshes are a fallback (and the norm for items).
void CollectRenderers(Tracked& t) {
    il2cpp::GCHandleFree(t.hRend);
    t.hRend = 0;
    t.renderers.clear();
    t.rendArray = unity::GO_GetComponentsInChildren(t.go, unity::typeRenderer, false);
    if (!t.rendArray) return;
    t.hRend = il2cpp::GCHandleNew(t.rendArray);
    std::vector<void*> skinned, meshes;
    for (uintptr_t i = 0; i < il2cpp::ArrayLen(t.rendArray); ++i) {
        void* r = il2cpp::ArrayAt(t.rendArray, i);
        if (!il2cpp::Alive(r)) continue;
        const char* n = il2cpp::ClassName(r);
        if (!std::strcmp(n, "SkinnedMeshRenderer")) skinned.push_back(r);
        else if (!std::strcmp(n, "MeshRenderer")) meshes.push_back(r);
    }
    t.renderers = skinned;
    if (t.type < 0 || skinned.empty()) t.renderers.insert(t.renderers.end(), meshes.begin(), meshes.end());
    if (t.renderers.size() > 32) t.renderers.resize(32);
}

// ---------------------------------------------------------------------------
// Chams
// ---------------------------------------------------------------------------
void RemoveChams(Tracked& t) {
    ChamsState& c = t.chams;
    if (c.outline) {
        unity::Destroy(c.outline);
        il2cpp::GCHandleFree(c.hOutline);
        il2cpp::GCHandleFree(c.hOutlineRenderers);
        c.outline = nullptr;
        c.hOutline = 0;
        c.hOutlineRenderers = 0;
    }
    for (auto& s : c.saved) {
        if (il2cpp::Alive(s.renderer)) unity::SetSharedMaterials(s.renderer, static_cast<Il2CppArray*>(s.mats));
        il2cpp::GCHandleFree(s.handle);
    }
    c.saved.clear();
    c.mode = ChamsMode::Off;
}

void UpdateOutlineProps(ChamsState& c, Color color, float width) {
    if (!il2cpp::Alive(c.outline)) return;
    if (m.OutlineSetColor && !SameColor(color, c.color)) m.OutlineSetColor(c.outline, color, nullptr);
    if (m.OutlineSetWidth && width != c.width) m.OutlineSetWidth(c.outline, width, nullptr);
    c.color = color;
    c.width = width;
}

// --- Material library -------------------------------------------------------
// Built-in shaders only (Unity cannot compile shaders at runtime):
//  * Hidden/Internal-Colored exposes ZTest / blend / cull, which gives through-wall passes.
//  * Standard gives lit metallic and emissive looks (emission blooms with the game's post-processing).
struct MatSet {
    ChamsMode style = ChamsMode::Off;
    void* main = nullptr;   uint32_t hMain = 0;     // replaces the model's own materials or overlays them
    void* hidden = nullptr; uint32_t hHidden = 0;   // extra pass drawn only where the model is behind walls
    Color lastMain{ -1, -1, -1, -1 }, lastHidden{ -1, -1, -1, -1 }, lastEmission{ -1, -1, -1, -1 };
};
MatSet g_enemyMats, g_itemMats;

namespace zt { constexpr float LessEqual = 4.f, Greater = 5.f, Always = 8.f; }
namespace bl { constexpr float Zero = 0.f, One = 1.f, SrcAlpha = 5.f, OneMinusSrcAlpha = 10.f; }

void* NewColoredMat(uint32_t& handle, float ztest, float zwrite, float src, float dst, int queue) {
    void* shader = unity::FindShader("Hidden/Internal-Colored");
    void* mat = shader ? unity::NewMaterial(shader) : nullptr;
    if (!mat) return nullptr;
    handle = il2cpp::GCHandleNew(mat);
    unity::MaterialSetFloat(mat, "_ZTest", ztest);
    unity::MaterialSetFloat(mat, "_ZWrite", zwrite);
    unity::MaterialSetFloat(mat, "_Cull", 2.f);  // back faces off: avoids seeing the model's own interior
    unity::MaterialSetFloat(mat, "_SrcBlend", src);
    unity::MaterialSetFloat(mat, "_DstBlend", dst);
    unity::MaterialSetRenderQueue(mat, queue);
    return mat;
}

void* NewStandardMat(uint32_t& handle, bool emissive) {
    void* shader = unity::FindShader("Standard");
    void* mat = shader ? unity::NewMaterial(shader) : nullptr;
    if (!mat) return nullptr;
    handle = il2cpp::GCHandleNew(mat);
    if (emissive) {
        unity::MaterialEnableKeyword(mat, "_EMISSION");
        unity::MaterialSetFloat(mat, "_Metallic", 0.f);
        unity::MaterialSetFloat(mat, "_Glossiness", 0.6f);
    } else {
        unity::MaterialSetFloat(mat, "_Metallic", 1.f);
        unity::MaterialSetFloat(mat, "_Glossiness", 0.95f);
    }
    return mat;
}

void DestroyMats(MatSet& s) {
    unity::Destroy(s.main);
    unity::Destroy(s.hidden);
    il2cpp::GCHandleFree(s.hMain);
    il2cpp::GCHandleFree(s.hHidden);
    s = MatSet{};
}

void BuildMats(MatSet& s, ChamsMode style) {
    DestroyMats(s);
    s.style = style;
    auto hiddenPass = [&] { s.hidden = NewColoredMat(s.hHidden, zt::Greater, 0.f, bl::SrcAlpha, bl::OneMinusSrcAlpha, 4000); };
    switch (style) {
    case ChamsMode::Flat: s.main = NewColoredMat(s.hMain, zt::Always, 0.f, bl::SrcAlpha, bl::OneMinusSrcAlpha, 4000); break;
    case ChamsMode::TwoTone:
        s.main = NewColoredMat(s.hMain, zt::LessEqual, 1.f, bl::One, bl::Zero, 2450);
        hiddenPass();
        break;
    case ChamsMode::Xqz: hiddenPass(); break;
    case ChamsMode::Ghost: s.main = NewColoredMat(s.hMain, zt::Always, 0.f, bl::SrcAlpha, bl::OneMinusSrcAlpha, 4000); break;
    case ChamsMode::Glow: s.main = NewColoredMat(s.hMain, zt::Always, 0.f, bl::One, bl::One, 4000); break;
    case ChamsMode::Neon: s.main = NewStandardMat(s.hMain, true); hiddenPass(); break;
    case ChamsMode::Chrome: s.main = NewStandardMat(s.hMain, false); hiddenPass(); break;
    default: break;
    }
    LOG("chams materials for style %d: main=%p hidden=%p", static_cast<int>(style), s.main, s.hidden);
}

void SetMatColor(void* mat, const char* prop, Color c, Color& cache) {
    if (mat && !SameColor(c, cache)) {
        unity::MaterialSetColor(mat, prop, c);
        cache = c;
    }
}

void UpdateMatColors(MatSet& s, Color main, Color hidden) {
    const float glow = g_cfg.chamsGlow;
    switch (s.style) {
    case ChamsMode::Flat:
    case ChamsMode::TwoTone:
    case ChamsMode::Chrome: SetMatColor(s.main, "_Color", main, s.lastMain); break;
    case ChamsMode::Ghost: SetMatColor(s.main, "_Color", { main.r, main.g, main.b, main.a * 0.45f }, s.lastMain); break;
    case ChamsMode::Glow: {
        const float k = main.a * std::min(glow / 3.f, 1.6f);
        SetMatColor(s.main, "_Color", { main.r * k, main.g * k, main.b * k, 1.f }, s.lastMain);
        break;
    }
    case ChamsMode::Neon:
        SetMatColor(s.main, "_Color", { main.r * 0.15f, main.g * 0.15f, main.b * 0.15f, 1.f }, s.lastMain);
        SetMatColor(s.main, "_EmissionColor", { main.r * glow, main.g * glow, main.b * glow, 1.f }, s.lastEmission);
        break;
    default: break;
    }
    // XQZ has a single colour: the main one, drawn behind walls only
    SetMatColor(s.hidden, "_Color", s.style == ChamsMode::Xqz ? main : hidden, s.lastHidden);
}

Color HsvToRgb(float h, float s, float v, float a) {
    h = std::fmod(h, 1.f) * 6.f;
    const int i = static_cast<int>(h);
    const float f = h - i, p = v * (1 - s), q = v * (1 - s * f), u = v * (1 - s * (1 - f));
    switch (i % 6) {
    case 0: return { v, u, p, a };
    case 1: return { q, v, p, a };
    case 2: return { p, v, u, a };
    case 3: return { p, q, v, a };
    case 4: return { u, p, v, a };
    default: return { v, p, q, a };
    }
}

// Applies the rainbow / pulse modifiers
Color AnimatedColor(const float c[4], bool rainbow, bool pulse, float hueShift) {
    const float t = static_cast<float>(state::NowMs() % 3600000) / 1000.f;
    Color out = rainbow ? HsvToRgb(t * 0.12f + hueShift, 0.75f, 1.f, c[3]) : ToColor(c);
    if (pulse) out.a *= 0.35f + 0.65f * (0.5f + 0.5f * std::sin(t * 4.f));
    return out;
}

// Overlay passes appended after the model's materials render only its last sub-mesh. For multi-material
// meshes add one more sub-mesh holding every triangle (same trick QuickOutline uses) - readable meshes only.
void EnsureFullOverlay(void* renderer, uintptr_t materialCount) {
    void* mesh = unity::RendererMesh(renderer);
    const int sc = unity::MeshSubMeshCount(mesh);
    if (sc <= 1 || sc != static_cast<int>(materialCount) || !unity::MeshReadable(mesh)) return;
    Il2CppArray* tris = unity::MeshTriangles(mesh);
    if (!il2cpp::ArrayLen(tris)) return;
    unity::MeshSetSubMeshCount(mesh, sc + 1);
    unity::MeshSetTriangles(mesh, tris, sc);
}

void ApplyChams(Tracked& t, ChamsMode want, Color color, float width, const MatSet& mats) {
    ChamsState& c = t.chams;
    const bool outlineLost = IsOutlineMode(static_cast<int>(c.mode)) && !il2cpp::Alive(c.outline);
    if (want == c.mode && !outlineLost) {
        if (IsOutlineMode(static_cast<int>(want))) UpdateOutlineProps(c, color, width);
        return;
    }
    if (want != ChamsMode::Off && want == c.failed) return;
    RemoveChams(t);
    if (want == ChamsMode::Off) {
        c.failed = ChamsMode::Off;
        return;
    }
    if (!unity::GO_ActiveInHierarchy(t.go)) return;
    c.failed = want;  // cleared below on success

    if (IsMaterialMode(static_cast<int>(want))) {
        if (!unity::klassMaterial || mats.style != want) return;
        const bool replace = want == ChamsMode::Flat || want == ChamsMode::TwoTone || want == ChamsMode::Neon || want == ChamsMode::Chrome;
        void* overlay = (want == ChamsMode::Ghost || want == ChamsMode::Glow) ? mats.main
                        : (want == ChamsMode::Flat)                          ? nullptr
                                                                             : mats.hidden;
        if ((replace && !mats.main) || (want != ChamsMode::Flat && !overlay)) return;
        for (void* r : t.renderers) {
            if (!il2cpp::Alive(r)) continue;
            Il2CppArray* orig = unity::GetSharedMaterials(r);
            const uintptr_t n = il2cpp::ArrayLen(orig);
            if (!n) continue;
            if (overlay) EnsureFullOverlay(r, n);
            Il2CppArray* repl = il2cpp::NewArray(unity::klassMaterial, n + (overlay ? 1 : 0));
            for (uintptr_t i = 0; i < n; ++i) repl->items[i] = replace ? mats.main : il2cpp::ArrayAt(orig, i);
            if (overlay) repl->items[n] = overlay;
            c.saved.push_back({ r, orig, il2cpp::GCHandleNew(orig) });
            unity::SetSharedMaterials(r, repl);
        }
    } else {
        if (!g_typeOutline || !m.OutlineSetMode || t.renderers.empty()) return;
        void* outline = unity::GO_AddComponent(t.go, g_typeOutline);
        if (!outline) return;
        c.outline = outline;
        c.hOutline = il2cpp::GCHandleNew(outline);
        if (off::Outline_renderers >= 0 && unity::klassRenderer) {
            // Awake grabbed every child renderer; OnDisable strips its materials, then OnEnable re-adds them to ours only
            unity::SetEnabled(outline, false);
            Il2CppArray* arr = il2cpp::NewArray(unity::klassRenderer, t.renderers.size());
            for (size_t i = 0; i < t.renderers.size(); ++i) arr->items[i] = t.renderers[i];
            c.hOutlineRenderers = il2cpp::GCHandleNew(arr);
            FieldAt<Il2CppArray*>(outline, off::Outline_renderers) = arr;
            unity::SetEnabled(outline, true);
        }
        // Outline.Mode: OutlineAll = 0, OutlineAndSilhouette = 3, SilhouetteOnly = 4
        const int mode = want == ChamsMode::Outline ? 0 : want == ChamsMode::Silhouette ? 4 : 3;
        m.OutlineSetMode(outline, mode, nullptr);
        c.color = Color{ -1, -1, -1, -1 };
        c.width = -1.f;
        UpdateOutlineProps(c, color, width);
    }
    c.mode = want;
    c.failed = ChamsMode::Off;
}

// ---------------------------------------------------------------------------
// Freeze / speed
// ---------------------------------------------------------------------------
bool AgentUsable(Tracked& t) {
    return t.agent && il2cpp::Alive(t.agent) && unity::GO_ActiveInHierarchy(t.go) && unity::GetEnabled(t.agent);
}

void SetFrozen(Tracked& t, bool on) {
    if (on == t.frozen || !il2cpp::Alive(t.comp)) return;
    if (on) {
        t.wasEnabled = unity::GetEnabled(t.comp);
        unity::SetEnabled(t.comp, false);
        if (il2cpp::Alive(t.anim)) {
            t.animWasEnabled = unity::GetEnabled(t.anim);
            unity::SetEnabled(t.anim, false);
        }
        if (AgentUsable(t)) {
            t.agentWasStopped = unity::AgentGetStopped(t.agent);
            unity::AgentSetStopped(t.agent, true);
        }
    } else {
        unity::SetEnabled(t.comp, t.wasEnabled);
        if (il2cpp::Alive(t.anim)) unity::SetEnabled(t.anim, t.animWasEnabled);
        if (AgentUsable(t)) unity::AgentSetStopped(t.agent, t.agentWasStopped);
    }
    t.frozen = on;
}

void ApplySpeed(Tracked& t) {
    const EnemyType& et = g_types[t.type];
    for (int k = 0; k < 4; ++k) {
        if (et.offSpeed[k] < 0) continue;
        float& f = FieldAt<float>(t.comp, et.offSpeed[k]);
        if (g_cfg.enemySpeedOn) {
            if (!t.speedInit || f != t.lastSpeed[k]) t.origSpeed[k] = f;  // first time, or the game changed it
            f = t.origSpeed[k] * g_cfg.enemySpeedMult;
            t.lastSpeed[k] = f;
        } else if (t.speedInit && f == t.lastSpeed[k]) {
            f = t.origSpeed[k];
        }
    }
    t.speedInit = g_cfg.enemySpeedOn;
}

void Release(Tracked& t, bool restore) {
    if (restore && il2cpp::Alive(t.comp)) {
        RemoveChams(t);
        SetFrozen(t, false);
        if (t.type >= 0 && t.speedInit) {
            const bool was = g_cfg.enemySpeedOn;
            g_cfg.enemySpeedOn = false;
            ApplySpeed(t);
            g_cfg.enemySpeedOn = was;
        }
    } else {
        for (auto& s : t.chams.saved) il2cpp::GCHandleFree(s.handle);
        il2cpp::GCHandleFree(t.chams.hOutline);
        il2cpp::GCHandleFree(t.chams.hOutlineRenderers);
    }
    il2cpp::GCHandleFree(t.hRend);
    il2cpp::GCHandleFree(t.hComp);
}

void ReleaseAll(bool restore) {
    for (auto& [k, t] : g_enemies) Release(t, restore);
    for (auto& [k, t] : g_items) Release(t, restore);
    g_enemies.clear();
    g_items.clear();
}

// ---------------------------------------------------------------------------
// Scene scan
// ---------------------------------------------------------------------------
void RefreshGiveList() {
    void* inv = PickRayObj(off::PickRay_Inventory);
    if (!il2cpp::Alive(inv) || off::Inventory_ItemDefs < 0) return;
    void* list = FieldAt<void*>(inv, off::Inventory_ItemDefs);
    if (!list) return;
    auto* items = FieldAt<Il2CppArray*>(list, 0x10);   // List<T>._items
    const int size = FieldAt<int>(list, 0x18);          // List<T>._size
    std::vector<GiveEntry> out;
    std::unordered_map<void*, std::string> hands;
    for (int i = 0; i < size && items && i < static_cast<int>(items->max_length); ++i) {
        void* def = items->items[i];
        if (!def) continue;
        GiveEntry e;
        e.itemName = il2cpp::ToUtf8(FieldAt<Il2CppString*>(def, off::ItemDefs_itemName));
        e.label = il2cpp::ToUtf8(FieldAt<Il2CppString*>(def, off::ItemDefs_textHighlighted));
        if (e.label.empty()) e.label = e.itemName;
        if (void* hand = FieldAt<void*>(def, off::ItemDefs_handObject)) hands[hand] = e.label;
        if (!e.itemName.empty()) out.push_back(std::move(e));
    }
    const std::pair<int, const char*> specials[] = {
        { off::PickRay_H_CR, L(u8"Арбалет") }, { off::PickRay_H_DSH, L(u8"Двустволка") }, { off::PickRay_H_SG, L(u8"Дробовик") },
        { off::PickRay_H_SPRAY, L(u8"Перцовый баллончик") }, { off::PickRay_H_BATON, L(u8"Электрошокер") },
        { off::PickRay_H_FT, L(u8"Ледяная ловушка") }, { off::PickRay_H_PTRAP, L(u8"Ядовитая ловушка") },
    };
    for (const auto& [fieldOff, label] : specials)
        if (void* hand = PickRayObj(fieldOff)) hands.try_emplace(hand, label);
    // Keys like the safe / master key or cogwheels are plain PickRay.H_* objects, not inventory entries
    for (const auto& [fieldOff, name] : g_handFields)
        if (void* hand = PickRayObj(fieldOff)) hands.try_emplace(hand, name.substr(2));
    if (!out.empty()) g_giveList = std::move(out);
    if (!g_auto.running) g_hands = std::move(hands);
}

std::string ItemLabel(const std::string& itemName) {
    for (const auto& e : g_giveList)
        if (e.itemName == itemName) return e.label;
    return itemName;
}

template <class Fn>
void Scan(std::unordered_map<void*, Tracked>& map, void* systemType, int typeIndex, Fn&& init) {
    Il2CppArray* arr = unity::FindObjectsOfType(systemType);
    for (uintptr_t i = 0; i < il2cpp::ArrayLen(arr); ++i) {
        void* comp = il2cpp::ArrayAt(arr, i);
        if (!il2cpp::Alive(comp) || map.count(comp)) continue;
        Tracked t;
        t.comp = comp;
        t.hComp = il2cpp::GCHandleNew(comp);
        t.type = typeIndex;
        t.go = unity::GetGameObject(comp);
        t.transform = unity::GetTransform(comp);
        CollectRenderers(t);
        init(t);
        map.emplace(comp, std::move(t));
    }
}

void Refresh() {
    // Drop dead objects first
    for (auto* map : { &g_enemies, &g_items }) {
        for (auto it = map->begin(); it != map->end();) {
            if (!il2cpp::Alive(it->second.comp)) {
                Release(it->second, false);
                it = map->erase(it);
            } else {
                ++it;
            }
        }
    }
    for (int i = 0; i < static_cast<int>(std::size(g_types)); ++i) {
        EnemyType& et = g_types[i];
        if (!et.type) continue;
        Scan(g_enemies, et.type, i, [&](Tracked& t) {
            t.name = et.display;
            if (et.offAgent >= 0) t.agent = FieldAt<void*>(t.comp, et.offAgent);
            if (et.offAnim >= 0) {
                void* anim = FieldAt<void*>(t.comp, et.offAnim);
                const char* cn = il2cpp::ClassName(anim);
                if (anim && (!std::strcmp(cn, "Animation") || !std::strcmp(cn, "Animator"))) t.anim = anim;
            }
        });
    }
    if (g_typeItemSeed) {
        Scan(g_items, g_typeItemSeed, -1, [&](Tracked& t) {
            t.name = ItemLabel(il2cpp::ToUtf8(FieldAt<Il2CppString*>(t.comp, off::ItemSeedData_itemName)));
        });
    }
}

// ---------------------------------------------------------------------------
// Main-thread tick (runs inside MobileFPS.Update)
// ---------------------------------------------------------------------------
void MainInit() {
    g_mainInit = true;
    unity::InitTypes();
    for (EnemyType& et : g_types) et.type = il2cpp::TypeObject(et.klass);
    g_typeItemSeed = il2cpp::TypeObject(cls::ItemSeedData);
    g_typeOutline = il2cpp::TypeObject(cls::Outline);
    LOG("MainInit: outlineType=%p itemSeedType=%p", g_typeOutline, g_typeItemSeed);
}

void PlayerFeatures() {
    g_playerCC = FieldAt<void*>(g_player, off::MobileFPS_characterController);

    // Noclip: collider off + free flight along the camera
    if (g_cfg.noclip) {
        if (unity::ColliderGetEnabled(g_playerCC)) unity::ColliderSetEnabled(g_playerCC, false);
        g_noclipApplied = true;
        if (!g_menuOpen) {
            void* camT = FieldAt<void*>(g_player, off::MobileFPS_playerCamera);
            Vec3 fwd = unity::GetForward(camT), right = unity::GetRight(camT);
            Vec3 dir{};
            if (unity::GetKey(unity::key::W)) dir = dir + fwd;
            if (unity::GetKey(unity::key::S)) dir = dir - fwd;
            if (unity::GetKey(unity::key::D)) dir = dir + right;
            if (unity::GetKey(unity::key::A)) dir = dir - right;
            if (unity::GetKey(unity::key::Space) || unity::GetKey(unity::key::E)) dir.y += 1.f;
            if (unity::GetKey(unity::key::LeftControl) || unity::GetKey(unity::key::Q)) dir.y -= 1.f;
            const float len = Length(dir);
            if (len > 0.001f) {
                float speed = g_cfg.noclipSpeed * (unity::GetKey(unity::key::LeftShift) ? 3.f : 1.f);
                void* t = PlayerTransform();
                unity::SetPosition(t, unity::GetPosition(t) + dir * (speed * unity::UnscaledDeltaTime() / len));
            }
        }
    } else if (g_noclipApplied) {
        unity::ColliderSetEnabled(g_playerCC, true);
        g_noclipApplied = false;
    }

    // Time scale (never fight the pause menu)
    if (g_cfg.timescaleOn && !IsPaused()) {
        unity::SetTimeScale(g_cfg.timescale);
        g_timescaleApplied = true;
    } else if (!g_cfg.timescaleOn && g_timescaleApplied) {
        if (!IsPaused()) unity::SetTimeScale(1.f);
        g_timescaleApplied = false;
    }

    // FOV
    void* cam = FieldAt<void*>(g_player, off::MobileFPS_playerCamera2);
    if (g_cfg.fovOn && il2cpp::Alive(cam)) {
        if (!g_fovApplied) g_fovOrig = unity::GetFov(cam);
        unity::SetFov(cam, g_cfg.fov);
        g_fovApplied = true;
    } else if (g_fovApplied) {
        unity::SetFov(cam, g_fovOrig);
        g_fovApplied = false;
    }

    // Occlusion culling skips renderers hidden behind walls entirely - chams would vanish with them
    const bool chamsOn = g_cfg.enemyChams != static_cast<int>(ChamsMode::Off) || g_cfg.itemChams != static_cast<int>(ChamsMode::Off);
    if (chamsOn) {
        unity::SetOcclusionCulling(cam, false);
        g_occlusionOff = true;
    } else if (g_occlusionOff) {
        unity::SetOcclusionCulling(cam, true);
        g_occlusionOff = false;
    }

    // Fullbright / fog
    if (g_cfg.fullbright) {
        if (!g_brightApplied) {
            g_ambientModeOrig = unity::GetAmbientMode();
            g_ambientOrig = unity::GetAmbientLight();
        }
        const float l = g_cfg.fullbrightLevel;
        unity::SetAmbientMode(3);  // AmbientMode.Flat
        unity::SetAmbientLight({ l, l, l, 1.f });
        g_brightApplied = true;
    } else if (g_brightApplied) {
        unity::SetAmbientMode(g_ambientModeOrig);
        if (unity::HasAmbientGetter()) unity::SetAmbientLight(g_ambientOrig);
        g_brightApplied = false;
    }
    if (g_cfg.noFog) {
        if (!g_fogApplied) g_fogOrig = unity::GetFog();
        unity::SetFog(false);
        g_fogApplied = true;
    } else if (g_fogApplied) {
        unity::SetFog(g_fogOrig);
        g_fogApplied = false;
    }

    // Free the cursor while the menu is open (ImGui draws its own pointer)
    if (g_menuOpen) {
        if (!g_cursorSaved) {
            if (g_gameCursorLock < 0) g_gameCursorLock = unity::GetCursorLock();
            if (g_gameCursorVisible < 0) g_gameCursorVisible = g_gameCursorLock == 0;
            g_cursorSaved = true;
        }
        CursorLockRaw(0);
        CursorVisibleRaw(false);
    } else if (g_cursorSaved) {
        CursorLockRaw(g_gameCursorLock);
        CursorVisibleRaw(g_gameCursorVisible != 0);
        g_cursorSaved = false;
    }
}

void hkCursorSetLock(int v, MI mi) {
    g_gameCursorLock = v;
    oCursorSetLock((g_menuOpen && !g_cleanupRequested) ? 0 : v, mi);
}

void hkCursorSetVisible(bool v, MI mi) {
    g_gameCursorVisible = v;
    oCursorSetVisible((g_menuOpen && !g_cleanupRequested) ? false : v, mi);
}

// Rebuild a category's materials when its style changes (objects using the old ones are restored first)
void SyncMats(MatSet& mats, std::unordered_map<void*, Tracked>& map, int mode) {
    const ChamsMode want = IsMaterialMode(mode) ? static_cast<ChamsMode>(mode) : ChamsMode::Off;
    if (mats.style == want) return;
    for (auto& [k, t] : map)
        if (IsMaterialMode(static_cast<int>(t.chams.mode)) && il2cpp::Alive(t.comp)) RemoveChams(t);
    if (want == ChamsMode::Off) DestroyMats(mats);
    else BuildMats(mats, want);
}

void ApplyEntityFeatures() {
    SyncMats(g_enemyMats, g_enemies, g_cfg.enemyChams);
    SyncMats(g_itemMats, g_items, g_cfg.itemChams);
    const Color enemyMain = AnimatedColor(g_cfg.enemyChamsColor, g_cfg.enemyChamsRainbow, g_cfg.enemyChamsPulse, 0.f);
    const Color enemyHidden = AnimatedColor(g_cfg.enemyChamsHidden, g_cfg.enemyChamsRainbow, g_cfg.enemyChamsPulse, 0.5f);
    const Color itemMain = AnimatedColor(g_cfg.itemChamsColor, g_cfg.itemChamsRainbow, g_cfg.itemChamsPulse, 0.25f);
    const Color itemHidden = AnimatedColor(g_cfg.itemChamsHidden, g_cfg.itemChamsRainbow, g_cfg.itemChamsPulse, 0.75f);
    UpdateMatColors(g_enemyMats, enemyMain, enemyHidden);
    UpdateMatColors(g_itemMats, itemMain, itemHidden);

    for (auto& [k, t] : g_enemies) {
        if (!il2cpp::Alive(t.comp)) continue;
        SetFrozen(t, g_cfg.freezeAll || t.freezeSingle);
        ApplySpeed(t);
        ApplyChams(t, static_cast<ChamsMode>(g_cfg.enemyChams), enemyMain, g_cfg.enemyOutlineWidth, g_enemyMats);
    }
    const Vec3 me = PlayerPos();
    for (auto& [k, t] : g_items) {
        if (!il2cpp::Alive(t.comp)) continue;
        ChamsMode want = static_cast<ChamsMode>(g_cfg.itemChams);
        // Outline components are not free: keep item chams within the ESP distance
        if (want != ChamsMode::Off && Length(unity::GetPosition(t.transform) - me) > g_cfg.itemMaxDist) want = ChamsMode::Off;
        ApplyChams(t, want, itemMain, g_cfg.itemOutlineWidth, g_itemMats);
    }
}

// World AABB of an enemy's body renderers; false when nothing sensible is rendered
bool EnemyBounds(const Tracked& t, Vec3& mn, Vec3& mx) {
    mn = { FLT_MAX, FLT_MAX, FLT_MAX };
    mx = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
    bool any = false;
    for (void* r : t.renderers) {
        if (!unity::RendererEnabled(r)) continue;
        Bounds b = unity::RendererBounds(r);
        if (b.extents.x <= 0 && b.extents.y <= 0 && b.extents.z <= 0) continue;
        mn = { std::min(mn.x, b.center.x - b.extents.x), std::min(mn.y, b.center.y - b.extents.y), std::min(mn.z, b.center.z - b.extents.z) };
        mx = { std::max(mx.x, b.center.x + b.extents.x), std::max(mx.y, b.center.y + b.extents.y), std::max(mx.z, b.center.z + b.extents.z) };
        any = true;
    }
    return any && Length(mx - mn) <= 12.f;  // reject absurd bounds (detached renderers)
}

// Pick the enemy closest to the crosshair (by angle) inside the aim FOV
void UpdateAimTarget(Vec3 camPos, Vec3 camFwd) {
    g_aimHas = false;
    if (!g_cfg.aimCamera && !g_cfg.silentAim) return;
    float best = g_cfg.aimFov;
    for (auto& [k, t] : g_enemies) {
        if (!il2cpp::Alive(t.comp) || !unity::GO_ActiveInHierarchy(t.go)) continue;
        if (g_cfg.aimIgnoreFrozen && (t.frozen)) continue;
        Vec3 mn, mx, point;
        if (EnemyBounds(t, mn, mx)) {
            const float h = mx.y - mn.y;
            const Vec3 c = (mn + mx) * 0.5f;
            point = g_cfg.aimBone == 0 ? Vec3{ c.x, mx.y - h * 0.1f, c.z } : g_cfg.aimBone == 1 ? Vec3{ c.x, c.y + h * 0.2f, c.z } : c;
        } else {
            point = unity::GetPosition(t.transform) + Vec3{ 0, 1.2f, 0 };
        }
        const Vec3 d = point - camPos;
        const float dist = Length(d);
        if (dist < 0.3f || dist > g_cfg.aimMaxDist) continue;
        const float cosA = (d.x * camFwd.x + d.y * camFwd.y + d.z * camFwd.z) / dist;
        const float angle = std::acos(std::clamp(cosA, -1.f, 1.f)) / kRad;
        if (angle < best) {
            best = angle;
            g_aimPoint = point;
            g_aimHas = true;
        }
    }
}

// Turn the view toward the target: yaw lives on the player body, pitch in MobileFPS.rotationX
void CameraAim() {
    if (!g_cfg.aimCamera || !g_aimHas || g_menuOpen || !(GetAsyncKeyState(g_cfg.aimKey) & 0x8000)) return;
    void* camT = FieldAt<void*>(g_player, off::MobileFPS_playerCamera);
    void* body = PlayerTransform();
    if (!il2cpp::Alive(camT) || !body) return;
    float wantPitch, wantYaw;
    DirToAngles(g_aimPoint - unity::GetPosition(camT), wantPitch, wantYaw);
    const Vec3 f = unity::GetForward(body);
    const float curYaw = std::atan2(f.x, f.z) / kRad;
    float& pitch = FieldAt<float>(g_player, off::MobileFPS_rotationX);
    wantPitch = std::clamp(wantPitch, FieldAt<float>(g_player, off::MobileFPS_minXRotation), FieldAt<float>(g_player, off::MobileFPS_maxXRotation));
    // frame-rate independent smoothing; aimSmooth 1 snaps instantly
    const float k = g_cfg.aimSmooth <= 1.f ? 1.f : 1.f - std::exp(-unity::UnscaledDeltaTime() * 30.f / g_cfg.aimSmooth);
    const float yaw = curYaw + DeltaAngle(curYaw, wantYaw) * k;
    pitch += (wantPitch - pitch) * k;
    unity::SetRotation(body, QuatFromPitchYaw(0.f, yaw));
    unity::SetLocalRotation(camT, QuatFromPitchYaw(pitch, 0.f));
}

std::string EnemyState(const Tracked& t) {
    if (t.frozen) return L(u8"Заморожен");
    const EnemyType& et = g_types[t.type];
    auto flag = [&](int o) { return o >= 0 && FieldAt<bool>(t.comp, o); };
    if (flag(et.offDying)) return L(u8"Без сознания");
    if (flag(et.offChasing)) return L(u8"Погоня");
    if (flag(et.offTargeting)) return L(u8"Охотится");
    if (flag(et.offLunging)) return L(u8"Бросок");
    if (flag(et.offAngry)) return L(u8"Злится");
    if (flag(et.offSearching)) return L(u8"Ищет");
    if (flag(et.offEating)) return L(u8"Ест");
    if (flag(et.offHit)) return L(u8"Оглушён");
    if (flag(et.offIdle)) return L(u8"Стоит");
    return {};
}

void BuildSnapshot() {
    Snapshot s;
    s.tickMs = state::NowMs();
    void* cam = FieldAt<void*>(g_player, off::MobileFPS_playerCamera2);
    if (!il2cpp::Alive(cam)) cam = unity::MainCamera();
    if (!il2cpp::Alive(cam)) return;
    s.inGame = true;
    s.view = unity::WorldToCameraMatrix(cam);
    s.proj = unity::ProjectionMatrix(cam);
    void* camT = unity::GetTransform(cam);
    s.camPos = unity::GetPosition(camT);
    s.camFwd = unity::GetForward(camT);
    s.playerPos = PlayerPos();

    for (auto& [k, t] : g_enemies) {
        if (!il2cpp::Alive(t.comp) || !unity::GO_ActiveInHierarchy(t.go)) continue;
        EnemyView v;
        v.id = reinterpret_cast<uintptr_t>(t.comp);
        v.name = L(t.name.c_str());
        v.pos = unity::GetPosition(t.transform);
        v.hasBounds = EnemyBounds(t, v.bmin, v.bmax);
        v.dist = Length(v.pos - s.playerPos);
        v.state = EnemyState(t);
        v.frozen = t.frozen;
        v.frozenSingle = t.freezeSingle;
        const EnemyType& et = g_types[t.type];
        v.caps = (et.miKnockout ? CAP_KNOCKOUT : 0) | (et.miPepper ? CAP_PEPPER : 0) | (et.miFreezeFx ? CAP_FREEZEFX : 0) |
                 (et.miChase ? CAP_CHASE : 0) | (et.kill != Kill::None ? CAP_KILL : 0) | CAP_MOVE;
        s.enemies.push_back(std::move(v));
    }
    std::sort(s.enemies.begin(), s.enemies.end(), [](const EnemyView& a, const EnemyView& b) { return a.dist < b.dist; });

    for (auto& [k, t] : g_items) {
        if (!il2cpp::Alive(t.comp) || !unity::GO_ActiveInHierarchy(t.go)) continue;
        ItemView v;
        v.id = reinterpret_cast<uintptr_t>(t.comp);
        v.name = t.name;
        v.pos = unity::GetPosition(t.transform);
        v.dist = Length(v.pos - s.playerPos);
        s.items.push_back(std::move(v));
    }
    std::sort(s.items.begin(), s.items.end(), [](const ItemView& a, const ItemView& b) { return a.dist < b.dist; });

    s.giveList = g_giveList;
    if (il2cpp::Alive(g_pickRay)) {
        s.ammo = FieldAt<float>(g_pickRay, off::PickRay_AmmoAmount);
        s.shotgunLoaded = FieldAt<bool>(g_pickRay, off::PickRay_LoadedGun);
        s.crossbowCharged = FieldAt<bool>(g_pickRay, off::PickRay_IsCharged);
        s.spray = FieldAt<float>(g_pickRay, off::PickRay_SprayLeft);
        if (HandActive(off::PickRay_H_CR)) s.heldItem = L(u8"Арбалет");
        else if (HandActive(off::PickRay_H_DSH)) s.heldItem = L(u8"Двустволка");
        else if (HandActive(off::PickRay_H_FT)) s.heldItem = L(u8"Ледяная ловушка");
        else if (HandActive(off::PickRay_H_PTRAP)) s.heldItem = L(u8"Ядовитая ловушка");
        else {
            void* inv = PickRayObj(off::PickRay_Inventory);
            void* list = il2cpp::Alive(inv) ? FieldAt<void*>(inv, off::Inventory_ItemDefs) : nullptr;
            auto* items = list ? FieldAt<Il2CppArray*>(list, 0x10) : nullptr;
            const int size = list ? FieldAt<int>(list, 0x18) : 0;
            for (int i = 0; items && i < size && i < static_cast<int>(items->max_length); ++i) {
                void* def = items->items[i];
                if (def && unity::GO_ActiveSelf(FieldAt<void*>(def, off::ItemDefs_handObject))) {
                    s.heldItem = ItemLabel(il2cpp::ToUtf8(FieldAt<Il2CppString*>(def, off::ItemDefs_itemName)));
                    break;
                }
            }
        }
    }
    s.hasSavedPoint = g_hasSaved;
    s.savedPoint = g_savedPoint;
    s.camFov = unity::GetFov(cam);
    s.aimHasTarget = g_aimHas;
    s.aimTarget = g_aimPoint;
    state::Publish(std::move(s));
}

void Cleanup() {
    ReleaseAll(true);
    if (g_noclipApplied) unity::ColliderSetEnabled(g_playerCC, true);
    if (g_timescaleApplied && !IsPaused()) unity::SetTimeScale(1.f);
    if (g_fovApplied) unity::SetFov(FieldAt<void*>(g_player, off::MobileFPS_playerCamera2), g_fovOrig);
    if (g_occlusionOff) unity::SetOcclusionCulling(FieldAt<void*>(g_player, off::MobileFPS_playerCamera2), true);
    if (g_brightApplied) {
        unity::SetAmbientMode(g_ambientModeOrig);
        if (unity::HasAmbientGetter()) unity::SetAmbientLight(g_ambientOrig);
    }
    if (g_fogApplied) unity::SetFog(g_fogOrig);
    if (g_cursorSaved) {
        CursorLockRaw(g_gameCursorLock);
        CursorVisibleRaw(g_gameCursorVisible != 0);
    }
    DestroyMats(g_enemyMats);
    DestroyMats(g_itemMats);
    LOG("Cleanup finished");
}

bool GodActive() { return g_cfg.godmode && !g_cleanupRequested; }

// Safety net for godmode: whatever path an attack took, never leave an enemy frozen in its
// "caught the player" pose or the player locked in a jumpscare that will never kill them.
void GodRecovery() {
    if (!GodActive()) {
        g_jumpscareSince = 0;
        return;
    }
    const uint64_t now = state::NowMs();
    for (auto& [k, t] : g_enemies) {
        if (t.type < 0 || t.type > 1 || !il2cpp::Alive(t.comp)) continue;
        const HouseAI& ai = g_houseAI[t.type];
        if (ai.caught < 0 || !FieldAt<bool>(t.comp, ai.caught)) {
            t.stuckSince = 0;
            continue;
        }
        if (!t.stuckSince) t.stuckSince = now;
        if (now - t.stuckSince < 800) continue;
        FieldAt<bool>(t.comp, ai.caught) = false;
        if (ai.agent >= 0) unity::SetEnabled(FieldAt<void*>(t.comp, ai.agent), true);
        if (ai.bc >= 0) unity::SetEnabled(FieldAt<void*>(t.comp, ai.bc), true);
        CallMI(ai.miStopChase, t.comp);
        CallMI(ai.miReset, t.comp);
        t.stuckSince = 0;
        LOG("godmode: released %s from caught state", t.name.c_str());
    }
    void* ps = FieldAt<void*>(g_player, off::MobileFPS_PS);
    if (il2cpp::Alive(ps) && off::PlayerStatus_IsJumpscared >= 0) {
        if (FieldAt<bool>(ps, off::PlayerStatus_IsJumpscared) && !FieldAt<bool>(ps, off::PlayerStatus_Killed)) {
            if (!g_jumpscareSince) g_jumpscareSince = now;
            if (now - g_jumpscareSince > 1000) {
                FieldAt<bool>(ps, off::PlayerStatus_IsJumpscared) = false;
                FieldAt<bool>(g_player, off::MobileFPS_isAllowedToMove) = true;
                FieldAt<bool>(g_player, off::MobileFPS_AbleToMove) = true;
                g_jumpscareSince = 0;
            }
        } else {
            g_jumpscareSince = 0;
        }
    }
    if (off::MobileFPS_InWeb >= 0) FieldAt<bool>(g_player, off::MobileFPS_InWeb) = false;  // spider webs slow you down
}

void Tick() {
    if (g_cleanupDone) return;
    if (!g_mainInit) MainInit();
    if (g_player != g_lastPlayer) {  // new scene / respawn: restore whatever survived, forget the rest
        ReleaseAll(true);
        g_lastPlayer = g_player;
        g_doneHands.clear();
        g_noclipApplied = g_fovApplied = g_brightApplied = g_fogApplied = false;
    }
    state::RunCommands();
    if (g_cleanupRequested) {
        Cleanup();
        g_cleanupDone = true;
        return;
    }
    PlayerFeatures();
    const uint64_t now = state::NowMs();
    if (now - g_lastGiveRefresh > 2000) {
        RefreshGiveList();
        g_lastGiveRefresh = now;
    }
    if (now - g_lastRefresh > 500) {
        Refresh();
        g_lastRefresh = now;
    }
    ApplyEntityFeatures();
    GodRecovery();
    void* cam = FieldAt<void*>(g_player, off::MobileFPS_playerCamera2);
    if (void* camT = il2cpp::Alive(cam) ? unity::GetTransform(cam) : nullptr)
        UpdateAimTarget(unity::GetPosition(camT), unity::GetForward(camT));
    CameraAim();
    BuildSnapshot();
}

// ---------------------------------------------------------------------------
// Hooks
// ---------------------------------------------------------------------------
void (*oMobileFPS_Update)(void*, MI);
void hkMobileFPS_Update(void* self, MI mi) {
    oMobileFPS_Update(self, mi);
    g_player = self;
    Tick();
}

void ApplyAmmo() {
    if (!g_pickRay || g_cleanupRequested) return;
    const bool cr = HandActive(off::PickRay_H_CR);
    if (cr && (g_cfg.infiniteAmmo || g_cfg.rapidFire)) FieldAt<bool>(g_pickRay, off::PickRay_IsCharged) = true;
    if (cr && g_cfg.electricDarts) FieldAt<bool>(g_pickRay, off::PickRay_DartHasEnergy) = true;
    if (!g_cfg.infiniteAmmo) return;
    if (HandActive(off::PickRay_H_DSH)) FieldAt<float>(g_pickRay, off::PickRay_AmmoAmount) = 2.f;
    if (HandActive(off::PickRay_H_SG)) FieldAt<bool>(g_pickRay, off::PickRay_LoadedGun) = true;
    if (HandActive(off::PickRay_H_SPRAY)) {
        float& spray = FieldAt<float>(g_pickRay, off::PickRay_SprayLeft);
        if (spray < 3.f) spray = 3.f;
    }
}

// ---- Auto-use -------------------------------------------------------------
// Every locked interaction in PickRay.Update looks like:
//   if (buttonClicked) { buttonClicked = 0; if (!H_X.activeSelf) { T_NeedShit.SetActive(true) /* "I need X" */ } else Use(); }
// We note which hand object was asked for after the click was consumed and the "I need" text that followed,
// then on the next frame hold that item (hiding the current one), replay the click and give the old item back.

// True when the call came straight from PickRay.Update's body (not from helpers like CheckItemDropping)
bool CalledFromPickRayUpdate(void* returnAddress) {
    DWORD64 base = 0;
    PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(returnAddress), &base, nullptr);
    for (int depth = 0; fn && depth < 8; ++depth) {
        if (reinterpret_cast<void*>(base + fn->BeginAddress) == g_pickRayUpdate) return true;
        // Large functions are split into chunks whose unwind info chains back to the primary entry
        const auto* info = reinterpret_cast<const uint8_t*>(base + fn->UnwindData);
        constexpr uint8_t kChainInfo = 0x4;  // UNW_FLAG_CHAININFO
        if (!((info[0] >> 3) & kChainInfo)) break;
        const uint8_t codes = info[2];
        fn = reinterpret_cast<PRUNTIME_FUNCTION>(const_cast<uint8_t*>(info) + 4 + ((codes + 1) & ~1) * 2);
    }
    return false;
}

void* HeldHand(void* except) {
    for (const auto& [hand, label] : g_hands)
        if (hand != except && unity::GO_ActiveSelf(hand)) return hand;
    return nullptr;
}

void AutoUseBegin(void* required) {
    if (!il2cpp::Alive(required) || !g_hands.count(required)) return;
    void* previous = HeldHand(required);
    if (previous) unity::GO_SetActive(previous, false);  // hide, don't drop: we give it back right after
    unity::GO_SetActive(required, true);
    unity::GO_SetActive(PickRayObj(off::PickRay_T_NeedShit), false);
    unity::StopAudio(g_pickRay);  // locks ignore clicks while their "locked" sound plays: replay right away
    FieldAt<bool>(g_pickRay, off::PickRay_buttonClicked) = true;  // replay the interaction next frame
    g_auto = { required, previous, true, state::NowMs() };
    LOG("auto-use: holding %s", g_hands[required].c_str());
}

void AutoUseFinish() {
    const bool consumed = !unity::GO_ActiveSelf(g_auto.required);
    if (consumed) g_doneHands.push_back(g_auto.required);  // the game took it: this part is placed
    else unity::GO_SetActive(g_auto.required, false);
    FieldAt<bool>(g_pickRay, off::PickRay_buttonClicked) = false;
    // the player may have picked something up meanwhile: never put a second item in hand
    void* nowHeld = HeldHand(g_auto.required);
    if (nowHeld && nowHeld != g_auto.previous) {
        unity::GO_SetActive(PickRayObj(off::PickRay_Drop1), true);
    } else if (il2cpp::Alive(g_auto.previous)) {
        unity::GO_SetActive(g_auto.previous, true);
        unity::GO_SetActive(PickRayObj(off::PickRay_Drop1), true);
    } else {
        unity::GO_SetActive(PickRayObj(off::PickRay_Drop1), false);
    }
    auto it = g_hands.find(g_auto.required);
    state::Notify(std::string(L(u8"Автоприменение: ")) + (it != g_hands.end() ? it->second : std::string("?")));
    g_auto = {};
}

void ArmClickIfPending() {
    if (g_inPickRay && g_cfg.autoUse && off::PickRay_buttonClicked >= 0 && FieldAt<bool>(g_pickRay, off::PickRay_buttonClicked))
        g_clickArmed = true;
}

bool (*oGoActiveSelf)(void*, MI);
bool hkGoActiveSelf(void* go, MI mi) {
    const bool active = oGoActiveSelf(go, mi);
    if (!g_inPickRay || !g_cfg.autoUse || g_auto.running || off::PickRay_buttonClicked < 0) return active;
    if (FieldAt<bool>(g_pickRay, off::PickRay_buttonClicked)) {
        g_clickArmed = true;
    } else if (g_clickArmed && !active && g_hands.count(go) && CalledFromPickRayUpdate(_ReturnAddress())) {
        if (std::find(g_missingHands.begin(), g_missingHands.end(), go) == g_missingHands.end()) g_missingHands.push_back(go);
    }
    return active;
}

void (*oGoSetActive)(void*, bool, MI);
void hkGoSetActive(void* go, bool value, MI mi) {
    if (value && g_inPickRay && g_cfg.autoUse && !g_auto.running && !g_missingHands.empty() && !g_requiredHand &&
        go == PickRayObj(off::PickRay_T_NeedShit)) {
        // multi-part locks (e.g. two cogwheels) check several hands: take the first one not applied yet
        for (void* h : g_missingHands)
            if (std::find(g_doneHands.begin(), g_doneHands.end(), h) == g_doneHands.end()) {
                g_requiredHand = h;
                break;
            }
        if (!g_requiredHand) g_requiredHand = g_missingHands.front();
    }
    oGoSetActive(go, value, mi);
}

// ---- Silent aim: point the guns' ray origins at the target only for the duration of the shot
struct AimSwap {
    void* t = nullptr;
    Quat saved{};
};
void AimTransform(AimSwap& s, void* transform) {
    if (!il2cpp::Alive(transform)) return;
    s.t = transform;
    s.saved = unity::GetLocalRotation(transform);
    unity::SetRotation(transform, LookAt(unity::GetPosition(transform), g_aimPoint));
}
void RestoreTransform(const AimSwap& s) {
    if (s.t) unity::SetLocalRotation(s.t, s.saved);
}

bool SilentAimReady() { return g_cfg.silentAim && g_aimHas && !g_cleanupRequested; }

template <int N> struct GunHook {
    static inline void (*orig)(void*, MI) = nullptr;
    static void Detour(void* self, MI mi) {
        if (!SilentAimReady()) return orig(self, mi);
        AimSwap a, b;
        AimTransform(a, FieldAt<void*>(self, off::PickRay_RayShoot1));
        AimTransform(b, FieldAt<void*>(self, off::PickRay_RayShoot2));
        orig(self, mi);
        RestoreTransform(b);
        RestoreTransform(a);
    }
};

void (*oPickRay_Update)(void*, MI);
void hkPickRay_Update(void* self, MI mi) {
    g_pickRay = self;
    ApplyAmmo();
    // crossbow darts are spawned (position + rotation) from SpawnDart inside Update
    AimSwap dartAim;
    if (SilentAimReady() && HandActive(off::PickRay_H_CR)) AimTransform(dartAim, FieldAt<void*>(self, off::PickRay_SpawnDart));
    const bool autoUseFrame = g_auto.running;
    g_clickArmed = false;
    g_missingHands.clear();
    g_requiredHand = nullptr;
    ArmClickIfPending();

    bool fireNow = false;
    if (g_cfg.rapidFire && !g_menuOpen && !g_cleanupRequested && !IsPaused()) {
        const int shootKey = FieldAt<int>(self, off::PickRay_ShootKey);
        const uint64_t now = state::NowMs();
        if (unity::GetKey(shootKey) && now - g_lastShot >= static_cast<uint64_t>(g_cfg.rapidIntervalMs)) {
            fireNow = true;
            g_lastShot = now;
            if (HandActive(off::PickRay_H_CR)) g_spoofKey = shootKey;  // crossbow fires through its own Update path
        }
    }
    g_inPickRay = true;
    oPickRay_Update(self, mi);
    g_inPickRay = false;
    g_spoofKey = -1;
    RestoreTransform(dartAim);

    if (fireNow) {
        if (HandActive(off::PickRay_H_DSH) && m.ShootGun2) m.ShootGun2(self, nullptr);
        else if (HandActive(off::PickRay_H_SG) && m.ShootGun1) m.ShootGun1(self, nullptr);
    }
    if (autoUseFrame) {
        // Some locks ignore clicks while their "locked" sound still plays: keep the click pending until the
        // game consumes it (buttonClicked reset) or give up after 3 s
        // 1) keep the click pending until the game consumes it (some locks ignore clicks while a sound plays);
        // 2) then keep the item in hand while the unlock action plays (doors, safe, hatch use the key
        //    after the click) until the game takes it away itself, at most 3 s
        const uint64_t now = state::NowMs();
        if (!g_auto.clickedMs) {
            if (!FieldAt<bool>(self, off::PickRay_buttonClicked)) g_auto.clickedMs = now;
            else if (now - g_auto.startMs > 3000) AutoUseFinish();
        } else if (!unity::GO_ActiveSelf(g_auto.required) || now - g_auto.clickedMs > 100) {
            AutoUseFinish();
        }
    } else if (g_requiredHand && g_cfg.autoUse && !g_cleanupRequested) {
        AutoUseBegin(g_requiredHand);
    }
    g_clickArmed = false;
    g_missingHands.clear();
    g_requiredHand = nullptr;
    ApplyAmmo();
}

bool (*oInput_GetKeyDown)(int, MI);
bool hkInput_GetKeyDown(int key, MI mi) {
    if (g_inPickRay && key == g_spoofKey) return true;
    ArmClickIfPending();
    return oInput_GetKeyDown(key, mi);
}

int (*oCC_Move)(void*, Vec3, MI);
int hkCC_Move(void* self, Vec3 motion, MI mi) {
    if (self == g_playerCC && !g_cleanupRequested) {
        if (g_cfg.noclip) return 0;
        if (g_cfg.speedhack) {
            motion.x *= g_cfg.speedMult;
            motion.z *= g_cfg.speedMult;
        }
    }
    return oCC_Move(self, motion, mi);
}

void (*oFalling_Update)(void*, MI);
void hkFalling_Update(void* self, MI mi) {
    if (g_cfg.noclip && !g_cleanupRequested) {
        FieldAt<float>(self, off::FallingHolder_fallDuration) = 0.f;
        FieldAt<bool>(self, off::FallingHolder_isFalling) = false;
        return;
    }
    oFalling_Update(self, mi);
}

template <int N> struct DeathHook {
    static inline void (*orig)(void*, MI) = nullptr;
    static inline const char* name = "";
    static void Detour(void* self, MI mi) {
        if (g_cfg.godmode && !g_cleanupRequested) {
            const uint64_t now = state::NowMs();
            if (now - g_lastGodToast > 3000) {
                state::Notify(L(u8"Бессмертие: смерть отменена"));
                g_lastGodToast = now;
            }
            return;
        }
        orig(self, mi);
    }
};

// ---- Godmode: stop the catch decision itself, so enemies never enter a "caught" state that can't finish
void (*oGrannySensor)(void*, MI);
void hkGrannySensor(void* self, MI mi) {
    if (!GodActive()) oGrannySensor(self, mi);
}
void (*oGrandpaSensor)(void*, MI);
void hkGrandpaSensor(void* self, MI mi) {
    if (!GodActive()) oGrandpaSensor(self, mi);
}

// FixedUpdate catches the player when `DistanceJumpscare >= distance && IsChasing`; that radius is used
// nowhere else, so a negative radius disables only the catch while chasing / searching stay untouched.
// The under-the-bed catch fires when TimerBedCaught runs out, so keep that timer at zero.
void GuardedHouseAI(const HouseAI& ai, void* self, MI mi, void (*orig)(void*, MI)) {
    if (!GodActive() || ai.catchRadius < 0) return orig(self, mi);
    float& radius = FieldAt<float>(self, ai.catchRadius);
    const float was = radius;
    radius = -1.f;
    if (ai.timerBed >= 0) FieldAt<float>(self, ai.timerBed) = 0.f;
    orig(self, mi);
    radius = was;
}
void (*oGrannyFixed)(void*, MI);
void hkGrannyFixed(void* self, MI mi) { GuardedHouseAI(g_houseAI[0], self, mi, oGrannyFixed); }
void (*oGrandpaFixed)(void*, MI);
void hkGrandpaFixed(void* self, MI mi) { GuardedHouseAI(g_houseAI[1], self, mi, oGrandpaFixed); }

void (*oMomCaught)(void*, MI);
void hkMomCaught(void* self, MI mi) {
    if (!GodActive()) return oMomCaught(self, mi);
    // Instead of the eat sequence: send her back to patrolling
    const uint64_t now = state::NowMs();
    if (now - g_lastMomRelease > 1500) {
        if (off::AI_MomSpider_PlayerClose >= 0) FieldAt<bool>(self, off::AI_MomSpider_PlayerClose) = false;
        CallMI(g_miMomStopChase, self);
        g_lastMomRelease = now;
    }
}

void (*oEyesGranny)(void*, MI);
void hkEyesGranny(void* self, MI mi) {
    void* ai = g_cfg.invisible ? FieldAt<void*>(self, off::Eyes_Granny_AI) : nullptr;
    if (!ai || off::AI_Granny_IsBlind < 0) return oEyesGranny(self, mi);
    bool& blind = FieldAt<bool>(ai, off::AI_Granny_IsBlind);
    const bool was = blind;
    blind = true;
    oEyesGranny(self, mi);
    blind = was;
}

void (*oEyesGrandpa)(void*, MI);
void hkEyesGrandpa(void* self, MI mi) {
    void* ai = g_cfg.invisible ? FieldAt<void*>(self, off::Eyes_Grandpa_AI) : nullptr;
    if (!ai || off::AI_Grandpa_IsBlind < 0) return oEyesGrandpa(self, mi);
    bool& blind = FieldAt<bool>(ai, off::AI_Grandpa_IsBlind);
    const bool was = blind;
    blind = true;
    oEyesGrandpa(self, mi);
    blind = was;
}

void (*oEyesMom)(void*, MI);
void hkEyesMom(void* self, MI mi) {
    if (!g_cfg.invisible || off::Eyes_MomSpider_ViewRange < 0) return oEyesMom(self, mi);
    float& range = FieldAt<float>(self, off::Eyes_MomSpider_ViewRange);
    const float was = range;
    range = -1.f;
    oEyesMom(self, mi);
    range = was;
}

bool Hook(void* target, void* detour, void* original, const char* name) {
    if (!target) {
        LOG("hook skipped (no target): %s", name);
        return false;
    }
    MH_STATUS st = MH_CreateHook(target, detour, reinterpret_cast<void**>(original));
    LOG("hook %-28s %p -> %s", name, target, MH_StatusToString(st));
    return st == MH_OK;
}

template <int N> void HookDeath(const char* method) {
    DeathHook<N>::name = method;
    Hook(il2cpp::GetMethod(cls::PlayerStatus, method, 0), reinterpret_cast<void*>(&DeathHook<N>::Detour),
         &DeathHook<N>::orig, method);
}

// ---------------------------------------------------------------------------
// Command implementations (main thread)
// ---------------------------------------------------------------------------
bool PickRayReady() {
    if (il2cpp::Alive(g_pickRay) && m.CheckItemDropping) return true;
    state::Notify(L(u8"Сначала загрузитесь в игру"));
    return false;
}

void FinishPickup() {
    if (m.TurnOffItemTexts) m.TurnOffItemTexts(g_pickRay, nullptr);
    unity::GO_SetActive(PickRayObj(off::PickRay_Drop1), true);
    unity::GO_SetActive(PickRayObj(off::PickRay_Ring), false);
}

void DoGiveItem(const std::string& itemName) {
    if (!PickRayReady() || !m.PickupItem) return;
    void* inv = PickRayObj(off::PickRay_Inventory);
    if (!il2cpp::Alive(inv)) return;
    m.CheckItemDropping(g_pickRay, nullptr);
    m.PickupItem(inv, il2cpp::NewString(itemName.c_str()), nullptr);
    if (itemName == "RegShotgun" && m.PickShotgun) {
        m.PickShotgun(g_pickRay, nullptr);
        FieldAt<bool>(g_pickRay, off::PickRay_LoadedGun) = true;
    }
    FinishPickup();
    state::Notify(L(u8"Выдано: ") + ItemLabel(itemName));
}

void DoGiveHand(int handOff, const char* label) {
    if (!PickRayReady()) return;
    void* hand = PickRayObj(handOff);
    if (!il2cpp::Alive(hand)) {
        state::Notify(L(u8"Этого предмета нет в текущем режиме"));
        return;
    }
    m.CheckItemDropping(g_pickRay, nullptr);
    unity::GO_SetActive(hand, true);
    FinishPickup();
    state::Notify(std::string(L(u8"Выдано: ")) + label);
}

std::string ItemNameByHand(int handOff) {
    void* hand = PickRayObj(handOff);
    void* inv = PickRayObj(off::PickRay_Inventory);
    if (!hand || !il2cpp::Alive(inv)) return {};
    void* list = FieldAt<void*>(inv, off::Inventory_ItemDefs);
    auto* items = list ? FieldAt<Il2CppArray*>(list, 0x10) : nullptr;
    const int size = list ? FieldAt<int>(list, 0x18) : 0;
    for (int i = 0; items && i < size && i < static_cast<int>(items->max_length); ++i) {
        void* def = items->items[i];
        if (def && FieldAt<void*>(def, off::ItemDefs_handObject) == hand)
            return il2cpp::ToUtf8(FieldAt<Il2CppString*>(def, off::ItemDefs_itemName));
    }
    return {};
}

void DoGiveSpecial(Special which) {
    if (!PickRayReady()) return;
    switch (which) {
    case Special::Crossbow:
        DoGiveHand(off::PickRay_H_CR, L(u8"Арбалет"));
        FieldAt<bool>(g_pickRay, off::PickRay_IsCharged) = true;
        break;
    case Special::DoubleShotgun:
        DoGiveHand(off::PickRay_H_DSH, L(u8"Двустволка"));
        FieldAt<float>(g_pickRay, off::PickRay_AmmoAmount) = 2.f;
        break;
    case Special::Shotgun: {
        std::string n = ItemNameByHand(off::PickRay_H_SG);
        DoGiveItem(n.empty() ? "RegShotgun" : n);
        FieldAt<bool>(g_pickRay, off::PickRay_LoadedGun) = true;
        break;
    }
    case Special::PepperSpray: {
        std::string n = ItemNameByHand(off::PickRay_H_SPRAY);
        if (n.empty()) DoGiveHand(off::PickRay_H_SPRAY, L(u8"Перцовый баллончик"));
        else DoGiveItem(n);
        float& spray = FieldAt<float>(g_pickRay, off::PickRay_SprayLeft);
        if (spray < 3.f) spray = 3.f;
        break;
    }
    case Special::Baton:
        if (m.PickBaton) {
            m.PickBaton(g_pickRay, nullptr);
            state::Notify(L(u8"Выдано: Электрошокер"));
        }
        break;
    case Special::FreezeTrap: DoGiveHand(off::PickRay_H_FT, L(u8"Ледяная ловушка")); break;
    case Special::PoisonTrap: DoGiveHand(off::PickRay_H_PTRAP, L(u8"Ядовитая ловушка")); break;
    }
}

Tracked* FindTracked(std::unordered_map<void*, Tracked>& map, uintptr_t id) {
    auto it = map.find(reinterpret_cast<void*>(id));
    if (it == map.end() || !il2cpp::Alive(it->second.comp)) return nullptr;
    return &it->second;
}

void MoveEnemy(Tracked& t, Vec3 dst) {
    if (AgentUsable(t) && unity::AgentWarp(t.agent, dst)) return;
    unity::SetPosition(t.transform, dst);
}

void DoEnemy(uintptr_t id, EnemyAction action) {
    Tracked* t = FindTracked(g_enemies, id);
    if (!t) return;
    const EnemyType& et = g_types[t->type];
    switch (action) {
    case EnemyAction::Knockout: CallMI(et.miKnockout, t->comp); break;
    case EnemyAction::Pepper: CallMI(et.miPepper, t->comp); break;
    case EnemyAction::FreezeFx: CallMI(et.miFreezeFx, t->comp); break;
    case EnemyAction::Chase: CallMI(et.miChase, t->comp); break;
    case EnemyAction::StopChase: CallMI(et.miStopChase, t->comp); break;
    case EnemyAction::TeleportToMe: MoveEnemy(*t, PlayerPos() + CameraForwardFlat() * 3.f); break;
    case EnemyAction::TeleportToPoint:
        if (g_hasSaved) MoveEnemy(*t, g_savedPoint);
        else state::Notify(L(u8"Сначала сохраните точку во вкладке «Игрок»"));
        break;
    case EnemyAction::ToggleFreeze: t->freezeSingle = !t->freezeSingle; break;
    case EnemyAction::Kill: {
        void* ps = il2cpp::Alive(g_player) ? FieldAt<void*>(g_player, off::MobileFPS_PS) : nullptr;
        void* ec = il2cpp::Alive(ps) ? FieldAt<void*>(ps, off::PlayerStatus_EC) : nullptr;
        if (!il2cpp::Alive(ec)) return;
        if (et.kill == Kill::Granny && m.GrannyDeath) m.GrannyDeath(ec, false, nullptr);
        if (et.kill == Kill::Grandpa && m.GrandpaDeath) m.GrandpaDeath(ec, false, nullptr);
        break;
    }
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
bool Init() {
    if (!unity::Init()) return false;

#define RESOLVE_CLASS(name) cls::name = il2cpp::GetClass(kAsm, "", #name);
    GAME_CLASSES(RESOLVE_CLASS)
#undef RESOLVE_CLASS
#define RESOLVE_FIELD(c, f) off::c##_##f = il2cpp::GetFieldOffset(cls::c, #f);
    GAME_FIELDS(RESOLVE_FIELD)
#undef RESOLVE_FIELD

    auto set = [](auto& fn, void* p) { fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(p); };
    set(m.CheckItemDropping, il2cpp::GetMethod(cls::PickRay, "CheckItemDropping", 0));
    set(m.PickShotgun, il2cpp::GetMethod(cls::PickRay, "PickShotgun", 0));
    set(m.ShootGun1, il2cpp::GetMethod(cls::PickRay, "ShootGun1", 0));
    set(m.ShootGun2, il2cpp::GetMethod(cls::PickRay, "ShootGun2", 0));
    set(m.TurnOffItemTexts, il2cpp::GetMethod(cls::PickRay, "TurnOffItemTexts", 0));
    set(m.PickBaton, il2cpp::GetMethod(cls::PickRay, "PickBaton", 0));
    set(m.PickupItem, il2cpp::GetMethod(cls::Inventory, "PickupItem", 1));
    set(m.GrannyDeath, il2cpp::GetMethod(cls::EnemyController, "GrannyDeath", 1));
    set(m.GrandpaDeath, il2cpp::GetMethod(cls::EnemyController, "GrandpaDeath", 1));
    set(m.OutlineSetMode, il2cpp::GetMethod(cls::Outline, "set_OutlineMode", 1));
    set(m.OutlineSetColor, il2cpp::GetMethod(cls::Outline, "set_OutlineColor", 1));
    set(m.OutlineSetWidth, il2cpp::GetMethod(cls::Outline, "set_OutlineWidth", 1));
    g_pausedField = il2cpp::FindField(cls::Paused, "IsPaused");

    void* houseClasses[2] = { cls::AI_Granny, cls::AI_Grandpa };
    for (int i = 0; i < 2; ++i) {
        HouseAI& ai = g_houseAI[i];
        ai.klass = houseClasses[i];
        ai.caught = il2cpp::GetFieldOffset(ai.klass, "CaughtPlayer");
        ai.timerBed = il2cpp::GetFieldOffset(ai.klass, "TimerBedCaught");
        ai.catchRadius = il2cpp::GetFieldOffset(ai.klass, "DistanceJumpscare");
        ai.agent = il2cpp::GetFieldOffset(ai.klass, "Agent");
        ai.bc = il2cpp::GetFieldOffset(ai.klass, "BC");
        ai.miReset = il2cpp::FindMethodInfo(ai.klass, "ResetAIDecision", 0);
        ai.miStopChase = il2cpp::FindMethodInfo(ai.klass, "StopChase", 0);
    }
    g_miMomStopChase = il2cpp::FindMethodInfo(cls::AI_MomSpider, "StopChase", 0);

    il2cpp::ForEachField(cls::PickRay, [](const char* name, int offset, void*) {
        if (name && name[0] == 'H' && name[1] == '_') g_handFields.emplace_back(offset, name);
    }, nullptr);
    LOG("PickRay hand fields: %zu", g_handFields.size());

    for (EnemyType& et : g_types) {
        et.klass = il2cpp::GetClass(kAsm, "", et.cls);
        if (!et.klass) continue;
        auto fld = [&](const char* n) { return il2cpp::FindFieldOffset(et.klass, n); };
        auto mth = [&](const char* n) { return il2cpp::FindMethodInfo(et.klass, n, 0); };
        et.offAgent = fld("Agent");
        if (et.offAgent < 0) et.offAgent = fld("agent");
        et.offAnim = fld("Anim");
        int k = 0;
        for (const char* n : { "Walk_Speed", "Run_Speed", "WalkSpeed", "RunSpeed", "WanderSpeed", "LungeSpeed" }) {
            int o = fld(n);
            if (o >= 0 && k < 4) et.offSpeed[k++] = o;
        }
        et.offChasing = fld("IsChasing");
        et.offSearching = fld("IsSearching");
        et.offAngry = fld("IsAngry");
        et.offIdle = fld("IsIdle");
        et.offDying = fld("IsDying");
        et.offTargeting = fld("IsTargetingPlayer");
        et.offLunging = fld("IsLunging");
        et.offEating = fld("IsEating");
        et.offHit = fld("IsHit");
        for (const char* n : { "ShootDart", "HitMom", "Dies", "KillRobot", "KnockedOut", "HurtDart", "HitSnowBall" })
            if (!et.miKnockout) et.miKnockout = mth(n);
        et.miPepper = mth("PepperHit");
        et.miFreezeFx = mth("FrozenEnemy");
        et.miChase = mth("ChaseAction");
        et.miStopChase = mth("StopChase");
    }
    LOG("game::Init done: PickRay=%p ammoOff=0x%X", cls::PickRay, off::PickRay_AmmoAmount);
    return cls::MobileFPS && cls::PickRay && off::MobileFPS_characterController >= 0;
}

bool InstallHooks() {
    using il2cpp::GetMethod;
    void* cInput = il2cpp::GetClass("UnityEngine.InputLegacyModule.dll", "UnityEngine", "Input");
    void* cCC = il2cpp::GetClass("UnityEngine.PhysicsModule.dll", "UnityEngine", "CharacterController");

    bool ok = Hook(GetMethod(cls::MobileFPS, "Update", 0), reinterpret_cast<void*>(&hkMobileFPS_Update), &oMobileFPS_Update, "MobileFPS.Update");
    g_pickRayUpdate = GetMethod(cls::PickRay, "Update", 0);
    Hook(g_pickRayUpdate, reinterpret_cast<void*>(&hkPickRay_Update), &oPickRay_Update, "PickRay.Update");
    void* cGameObject = il2cpp::GetClass("UnityEngine.CoreModule.dll", "UnityEngine", "GameObject");
    Hook(GetMethod(cGameObject, "get_activeSelf", 0), reinterpret_cast<void*>(&hkGoActiveSelf), &oGoActiveSelf, "GameObject.get_activeSelf");
    Hook(GetMethod(cGameObject, "SetActive", 1), reinterpret_cast<void*>(&hkGoSetActive), &oGoSetActive, "GameObject.SetActive");
    Hook(GetMethod(cInput, "GetKeyDown", 1, { "UnityEngine.KeyCode" }), reinterpret_cast<void*>(&hkInput_GetKeyDown), &oInput_GetKeyDown, "Input.GetKeyDown");
    Hook(GetMethod(cCC, "Move", 1), reinterpret_cast<void*>(&hkCC_Move), &oCC_Move, "CharacterController.Move");
    Hook(GetMethod(cls::FallingHolder, "Update", 0), reinterpret_cast<void*>(&hkFalling_Update), &oFalling_Update, "FallingHolder.Update");
    Hook(GetMethod(cls::Eyes_Granny, "FixedUpdate", 0), reinterpret_cast<void*>(&hkEyesGranny), &oEyesGranny, "Eyes_Granny.FixedUpdate");
    Hook(GetMethod(cls::Eyes_Grandpa, "FixedUpdate", 0), reinterpret_cast<void*>(&hkEyesGrandpa), &oEyesGrandpa, "Eyes_Grandpa.FixedUpdate");
    Hook(GetMethod(cls::Eyes_MomSpider, "FixedUpdate", 0), reinterpret_cast<void*>(&hkEyesMom), &oEyesMom, "Eyes_MomSpider.FixedUpdate");

    void* cCursor = il2cpp::GetClass("UnityEngine.CoreModule.dll", "UnityEngine", "Cursor");
    Hook(GetMethod(cCursor, "set_lockState", 1), reinterpret_cast<void*>(&hkCursorSetLock), &oCursorSetLock, "Cursor.set_lockState");
    Hook(GetMethod(cCursor, "set_visible", 1), reinterpret_cast<void*>(&hkCursorSetVisible), &oCursorSetVisible, "Cursor.set_visible");
    Hook(GetMethod(cls::PickRay, "ShootGun1", 0), reinterpret_cast<void*>(&GunHook<1>::Detour), &GunHook<1>::orig, "PickRay.ShootGun1");
    Hook(GetMethod(cls::PickRay, "ShootGun2", 0), reinterpret_cast<void*>(&GunHook<2>::Detour), &GunHook<2>::orig, "PickRay.ShootGun2");
    Hook(GetMethod(cls::AI_Granny, "SensorPlayer", 0), reinterpret_cast<void*>(&hkGrannySensor), &oGrannySensor, "AI_Granny.SensorPlayer");
    Hook(GetMethod(cls::AI_Grandpa, "SensorPlayer", 0), reinterpret_cast<void*>(&hkGrandpaSensor), &oGrandpaSensor, "AI_Grandpa.SensorPlayer");
    Hook(GetMethod(cls::AI_Granny, "FixedUpdate", 0), reinterpret_cast<void*>(&hkGrannyFixed), &oGrannyFixed, "AI_Granny.FixedUpdate");
    Hook(GetMethod(cls::AI_Grandpa, "FixedUpdate", 0), reinterpret_cast<void*>(&hkGrandpaFixed), &oGrandpaFixed, "AI_Grandpa.FixedUpdate");
    Hook(GetMethod(cls::AI_MomSpider, "MomCaughtPlayer", 0), reinterpret_cast<void*>(&hkMomCaught), &oMomCaught, "AI_MomSpider.MomCaughtPlayer");

    HookDeath<0>("NormalDeath");
    HookDeath<1>("KnockDeath");
    HookDeath<2>("GrannyCaughtYou");
    HookDeath<3>("GrannyCaughtYouBed");
    HookDeath<4>("GrandpaCaughtYou");
    HookDeath<5>("GrandpaCaughtYouBed");
    HookDeath<6>("MomDeath");
    HookDeath<7>("DamageFeet");
    HookDeath<8>("DamageSpider");

    MH_STATUS st = MH_EnableHook(MH_ALL_HOOKS);
    LOG("MH_EnableHook(all): %s", MH_StatusToString(st));
    return ok && st == MH_OK;
}

void RequestCleanup() { g_cleanupRequested = true; }
bool CleanupDone() { return g_cleanupDone; }

void GiveItem(const std::string& itemName) { state::Post([itemName] { DoGiveItem(itemName); }); }
void GiveSpecial(Special which) { state::Post([which] { DoGiveSpecial(which); }); }

void DropHeld() {
    state::Post([] {
        if (!PickRayReady()) return;
        m.CheckItemDropping(g_pickRay, nullptr);
        unity::GO_SetActive(PickRayObj(off::PickRay_Drop1), false);
    });
}

void DoEnemyAction(uintptr_t id, EnemyAction action) { state::Post([id, action] { DoEnemy(id, action); }); }

void StopAllChases() {
    state::Post([] {
        for (auto& [k, t] : g_enemies)
            if (il2cpp::Alive(t.comp)) CallMI(g_types[t.type].miStopChase, t.comp);
        state::Notify(L(u8"Погоня сброшена"));
    });
}

void SavePoint() {
    state::Post([] {
        if (!il2cpp::Alive(g_player)) return;
        g_savedPoint = PlayerPos();
        g_hasSaved = true;
        state::Notify(L(u8"Точка сохранена"));
    });
}

void TeleportToSaved() {
    state::Post([] {
        if (g_hasSaved) SetPlayerPos(g_savedPoint);
    });
}

void TeleportToItem(uintptr_t id) {
    state::Post([id] {
        if (Tracked* t = FindTracked(g_items, id)) SetPlayerPos(unity::GetPosition(t->transform) + Vec3{ 0, 1.f, 0 });
    });
}

void BringItem(uintptr_t id) {
    state::Post([id] {
        if (Tracked* t = FindTracked(g_items, id))
            unity::SetPosition(t->transform, PlayerPos() + CameraForwardFlat() * 1.2f + Vec3{ 0, 0.4f, 0 });
    });
}

void TeleportToEnemy(uintptr_t id) {
    state::Post([id] {
        if (Tracked* t = FindTracked(g_enemies, id))
            SetPlayerPos(unity::GetPosition(t->transform) - CameraForwardFlat() * 2.f + Vec3{ 0, 1.f, 0 });
    });
}

} // namespace game
