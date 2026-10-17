#pragma once
#include <cstdint>
#include <string>

// 1..3 use the game's Outline component, 4+ are material chams
enum class ChamsMode : int { Off = 0, Outline, Silhouette, OutlineSilhouette, Flat, TwoTone, Xqz, Ghost, Glow, Neon, Chrome, Count };
inline bool IsOutlineMode(int m) { return m >= 1 && m <= 3; }
inline bool IsMaterialMode(int m) { return m >= static_cast<int>(ChamsMode::Flat) && m < static_cast<int>(ChamsMode::Count); }
// materials that also draw a separate colour behind walls
inline bool UsesHiddenColor(int m) {
    return m == static_cast<int>(ChamsMode::TwoTone) || m == static_cast<int>(ChamsMode::Neon) || m == static_cast<int>(ChamsMode::Chrome);
}
enum class BoxStyle : int { Off = 0, Full, Corners };

struct Settings {
    // --- Visuals: enemies
    bool espEnemies = true;
    int enemyBox = static_cast<int>(BoxStyle::Corners);
    bool enemyName = true;
    bool enemyDistance = true;
    bool enemyState = true;
    bool enemySnaplines = false;
    bool enemyOffscreen = true;
    float enemyColor[4] = { 1.00f, 0.42f, 0.42f, 1.f };
    float enemyMaxDist = 300.f;
    int enemyChams = static_cast<int>(ChamsMode::OutlineSilhouette);
    float enemyChamsColor[4] = { 1.00f, 0.30f, 0.35f, 0.9f };
    float enemyChamsHidden[4] = { 1.00f, 0.80f, 0.20f, 0.85f };
    bool enemyChamsRainbow = false;
    bool enemyChamsPulse = false;
    float enemyOutlineWidth = 4.f;

    // --- Visuals: items
    bool espItems = true;
    bool itemDistance = true;
    float itemColor[4] = { 0.55f, 0.85f, 1.00f, 1.f };
    float itemMaxDist = 60.f;
    char itemFilter[64] = "";
    int itemChams = static_cast<int>(ChamsMode::Off);
    float itemChamsColor[4] = { 0.35f, 0.80f, 1.00f, 0.9f };
    float itemChamsHidden[4] = { 0.80f, 0.45f, 1.00f, 0.85f };
    bool itemChamsRainbow = false;
    bool itemChamsPulse = false;
    float itemOutlineWidth = 3.f;
    float chamsGlow = 3.f;      // emission / additive intensity

    // --- Radar
    bool radar = false;
    float radarSize = 180.f;
    float radarRange = 40.f;

    // --- Player
    bool godmode = false;
    bool invisible = false;
    bool speedhack = false;
    float speedMult = 2.f;
    bool noclip = false;
    float noclipSpeed = 8.f;
    bool timescaleOn = false;
    float timescale = 1.f;
    bool fovOn = false;
    float fov = 90.f;

    // --- Weapons
    bool infiniteAmmo = false;
    bool rapidFire = false;
    int rapidIntervalMs = 120;
    bool electricDarts = false;

    // --- Aimbot
    bool aimCamera = false;     // turn the view toward the target while the key is held
    int aimKey = 0x02;          // VK_RBUTTON
    float aimSmooth = 6.f;      // 1 = instant snap, higher = softer
    bool silentAim = false;     // shots / darts fly to the target without moving the camera
    float aimFov = 25.f;        // degrees from the crosshair
    float aimMaxDist = 60.f;
    int aimBone = 1;            // 0 head, 1 chest, 2 centre
    bool aimDrawFov = true;
    bool aimIgnoreFrozen = true;

    // --- Items
    bool autoUse = false;       // apply the required item automatically when an interaction asks for it

    // --- Enemies (AI)
    bool freezeAll = false;
    bool enemySpeedOn = false;
    float enemySpeedMult = 0.5f;

    // --- World
    bool fullbright = false;
    float fullbrightLevel = 0.6f;
    bool noFog = false;

    // --- UI
    float seedHue = 265.f;      // Material You seed colour (hue in degrees)
    float seedChroma = 0.55f;
    bool lightTheme = false;
    int language = -1;          // 0 Russian, 1 English, -1 = pick from the Windows UI language
    int toastStyle = 0;         // 0 = follows the theme, 1 = M3 inverse (contrast) snackbar
    int glassMode = 2;          // 0 none, 1 glass, 2 frosted, 3 acrylic
    float menuTransparency = 0.25f;  // 0 = solid
    float blurStrength = 0.7f;
    int keyMenu = 0x2D;         // VK_INSERT
    int keyUnload = 0x23;       // VK_END

    // --- Quick access (see toggles.cpp for the order of entries)
    bool fabEnabled = true;
    int quickMask = 0x3F;       // features shown on the quick-access panel (bit i = Toggles()[i])
    int toggleKeys[24] = {};    // hotkey per feature (virtual-key code, 0 = none)
};

extern Settings g_cfg;

bool SaveConfig(const std::wstring& path);
bool LoadConfig(const std::wstring& path);
