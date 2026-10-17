#include "toggles.h"
#include "config.h"
#include "theme.h"

namespace {

using namespace theme;

// Order matters: Settings::quickMask bit i and Settings::toggleKeys[i] refer to entry i
const ToggleDef kToggles[] = {
    { "godmode", u8"Бессмертие", icon::Shield, &g_cfg.godmode },
    { "invisible", u8"Невидимость", icon::Eye, &g_cfg.invisible },
    { "noclip", "Noclip", icon::Run, &g_cfg.noclip },
    { "speedhack", "Speedhack", icon::Bolt, &g_cfg.speedhack },
    { "freezeAll", u8"Заморозить врагов", icon::Snow, &g_cfg.freezeAll },
    { "silentAim", "Silent aim", icon::Location, &g_cfg.silentAim },
    { "aimCamera", u8"Наводка камеры", icon::Camera, &g_cfg.aimCamera },
    { "infiniteAmmo", u8"Бесконечные патроны", icon::Package, &g_cfg.infiniteAmmo },
    { "rapidFire", "Rapid fire", icon::Clock, &g_cfg.rapidFire },
    { "autoUse", u8"Автоприменение", icon::Check, &g_cfg.autoUse },
    { "espEnemies", u8"ESP врагов", icon::Bug, &g_cfg.espEnemies },
    { "espItems", u8"ESP предметов", icon::Search, &g_cfg.espItems },
    { "radar", u8"Радар", icon::Pin, &g_cfg.radar },
    { "fullbright", u8"Яркий мир", icon::Brightness, &g_cfg.fullbright },
    { "timescale", u8"Скорость игры", icon::Refresh, &g_cfg.timescaleOn },
    { "enemySpeed", u8"Скорость врагов", icon::Globe, &g_cfg.enemySpeedOn },
};
static_assert(sizeof(kToggles) / sizeof(kToggles[0]) <= kMaxToggles, "raise kMaxToggles");

} // namespace

const ToggleDef* Toggles() { return kToggles; }
int ToggleCount() { return static_cast<int>(sizeof(kToggles) / sizeof(kToggles[0])); }
