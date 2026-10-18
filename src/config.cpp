#include "config.h"
#include "toggles.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

Settings g_cfg;

namespace {

enum class T { Bool, Int, Float, Color, Str };
struct Entry { const char* name; T type; void* ptr; size_t size; };

std::vector<Entry> Entries() {
    Settings& s = g_cfg;
#define B(f) { #f, T::Bool, &s.f, 0 }
#define I(f) { #f, T::Int, &s.f, 0 }
#define F(f) { #f, T::Float, &s.f, 0 }
#define C(f) { #f, T::Color, s.f, 0 }
#define S(f) { #f, T::Str, s.f, sizeof(s.f) }
    std::vector<Entry> entries = {
        B(espEnemies), I(enemyBox), B(enemyName), B(enemyDistance), B(enemyState), B(enemySnaplines), B(enemyOffscreen),
        C(enemyColor), F(enemyMaxDist), I(enemyChams), C(enemyChamsColor), C(enemyChamsHidden), B(enemyChamsRainbow),
        B(enemyChamsPulse), F(enemyOutlineWidth),
        B(espItems), B(itemDistance), C(itemColor), F(itemMaxDist), S(itemFilter), I(itemChams), C(itemChamsColor),
        C(itemChamsHidden), B(itemChamsRainbow), B(itemChamsPulse), F(itemOutlineWidth), F(chamsGlow),
        B(radar), F(radarSize), F(radarRange),
        B(godmode), B(invisible), B(speedhack), F(speedMult), B(noclip), F(noclipSpeed), B(timescaleOn), F(timescale), B(fovOn), F(fov),
        B(infiniteAmmo), B(rapidFire), I(rapidIntervalMs), B(electricDarts), B(autoUse),
        B(aimCamera), I(aimKey), F(aimSmooth), B(silentAim), F(aimFov), F(aimMaxDist), I(aimBone), B(aimDrawFov), B(aimIgnoreFrozen),
        B(freezeAll), B(enemySpeedOn), F(enemySpeedMult),
        B(fullbright), F(fullbrightLevel), B(noFog),
        F(seedHue), F(seedChroma), B(lightTheme), I(language), I(toastStyle), I(glassMode), F(menuTransparency), F(blurStrength), I(keyMenu), I(keyUnload), I(keyOpenDoor), B(fabEnabled), I(quickMask),
    };
    // one hotkey entry per quick feature: key_<id>
    static std::vector<std::string> names;
    if (names.empty())
        for (int i = 0; i < ToggleCount(); ++i) names.push_back(std::string("key_") + Toggles()[i].id);
    for (int i = 0; i < ToggleCount(); ++i) entries.push_back({ names[i].c_str(), T::Int, &s.toggleKeys[i], 0 });
    return entries;
#undef B
#undef I
#undef F
#undef C
#undef S
}

} // namespace

bool SaveConfig(const std::wstring& path) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"w") || !f) return false;
    for (const Entry& e : Entries()) {
        switch (e.type) {
        case T::Bool: std::fprintf(f, "%s=%d\n", e.name, *static_cast<bool*>(e.ptr) ? 1 : 0); break;
        case T::Int: std::fprintf(f, "%s=%d\n", e.name, *static_cast<int*>(e.ptr)); break;
        case T::Float: std::fprintf(f, "%s=%.4f\n", e.name, *static_cast<float*>(e.ptr)); break;
        case T::Color: {
            const float* c = static_cast<float*>(e.ptr);
            std::fprintf(f, "%s=%.4f,%.4f,%.4f,%.4f\n", e.name, c[0], c[1], c[2], c[3]);
            break;
        }
        case T::Str: std::fprintf(f, "%s=%s\n", e.name, static_cast<char*>(e.ptr)); break;
        }
    }
    std::fclose(f);
    return true;
}

bool LoadConfig(const std::wstring& path) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"r") || !f) return false;
    std::vector<Entry> entries = Entries();
    char line[512];
    while (std::fgets(line, sizeof(line), f)) {
        char* eq = std::strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char* value = eq + 1;
        value[std::strcspn(value, "\r\n")] = '\0';
        for (const Entry& e : entries) {
            if (std::strcmp(e.name, line) != 0) continue;
            switch (e.type) {
            case T::Bool: *static_cast<bool*>(e.ptr) = std::atoi(value) != 0; break;
            case T::Int: *static_cast<int*>(e.ptr) = std::atoi(value); break;
            case T::Float: *static_cast<float*>(e.ptr) = static_cast<float>(std::atof(value)); break;
            case T::Color: {
                float* c = static_cast<float*>(e.ptr);
                std::sscanf(value, "%f,%f,%f,%f", &c[0], &c[1], &c[2], &c[3]);
                break;
            }
            case T::Str: strncpy_s(static_cast<char*>(e.ptr), e.size, value, _TRUNCATE); break;
            }
        }
    }
    std::fclose(f);
    return true;
}
