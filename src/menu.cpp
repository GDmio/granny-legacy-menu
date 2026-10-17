#include "menu.h"
#include "config.h"
#include "game.h"
#include "render.h"
#include "state.h"
#include "theme.h"
#include "toggles.h"
#include "blur.h"
#include "motion.h"
#include "widgets.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <cctype>
#include <cstdio>
#include <string>
#include "imgui_internal.h"

extern std::wstring g_configPath;

using namespace ImGui;
using namespace theme;
using m3::Btn;

namespace menu {

namespace {

enum Page { PVisual, PPlayer, PWeapons, PItems, PEnemies, PWorld, PSettings, PCount };

struct PageInfo { const char* icon; const char* rail; const char* title; const char* subtitle; };
const PageInfo kPages[PCount] = {
    { icon::Eye, u8"Визуал", u8"Визуал", u8"ESP, chams и радар" },
    { icon::Person, u8"Игрок", u8"Игрок", u8"Выживание, движение и телепорт" },
    { icon::Bolt, u8"Оружие", u8"Оружие", u8"Выдача, патроны и скорострельность" },
    { icon::Package, u8"Предметы", u8"Предметы", u8"Любой предмет игры в один клик" },
    { icon::Bug, u8"Враги", u8"Враги", u8"Управление искусственным интеллектом" },
    { icon::Globe, u8"Мир", u8"Мир", u8"Свет, туман и течение времени" },
    { icon::Settings, u8"Опции", u8"Настройки", u8"Тема, горячие клавиши и конфиг" },
};

int g_page = PVisual;
char g_itemSearch[64] = "";
Snapshot g_snap;

bool InGame() { return g_snap.inGame && state::NowMs() - g_snap.tickMs < 1500; }

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool Matches(const std::string& text, const char* filter) {
    if (!filter[0]) return true;
    return Lower(text).find(Lower(filter)) != std::string::npos;
}

template <class L, class R> void Columns2(const char* id, L&& left, R&& right) {
    if (BeginTable(id, 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoPadOuterX)) {
        TableNextColumn();
        left();
        TableNextColumn();
        right();
        EndTable();
    }
}

void NotInGameHint() {
    if (InGame()) return;
    if (m3::BeginCard(u8"Нет активной игры", icon::Info, u8"Функции ниже применятся после загрузки уровня")) {
        ImGuiWindow* w = GetCurrentWindow();
        motion::LoadingIndicator(w->DrawList, ImVec2(w->Pos.x + w->Size.x - 42.f, w->Pos.y + 34.f), 12.f, S.onPrimaryContainer,
                                 S.primaryContainer);
        m3::Supporting(u8"Начните или продолжите игру — чит автоматически подключится к игроку, врагам и предметам.");
    }
    m3::EndCard();
}

// --------------------------------------------------------------------------- Chams editor
struct MaterialInfo { ChamsMode mode; const char* name; const char* hint; };
const MaterialInfo kMaterials[] = {
    { ChamsMode::Flat, u8"Заливка", u8"Плоский цвет поверх всего" },
    { ChamsMode::TwoTone, u8"Двухцветный", u8"Свой цвет, когда виден и когда за стеной" },
    { ChamsMode::Xqz, u8"Сквозь стены", u8"Обычный вид, подсветка только за препятствиями" },
    { ChamsMode::Ghost, u8"Призрак", u8"Полупрозрачная плёнка сквозь стены" },
    { ChamsMode::Glow, u8"Свечение", u8"Яркое аддитивное свечение сквозь стены" },
    { ChamsMode::Neon, u8"Неон", u8"Светящийся материал (bloom) + цвет за стеной" },
    { ChamsMode::Chrome, u8"Хром", u8"Глянцевый металл + цвет за стеной" },
};

void ChamsEditor(const char* id, int* mode, float color[4], float hidden[4], bool* rainbow, bool* pulse, float* width,
                 int* lastMaterial) {
    PushID(id);
    int group = *mode == 0 ? 0 : IsOutlineMode(*mode) ? 1 : 2;
    if (m3::Segmented("type", &group, { u8"Выкл", u8"Контур", u8"Материал" })) {
        if (IsMaterialMode(*mode)) *lastMaterial = *mode;
        *mode = group == 0 ? 0 : group == 1 ? static_cast<int>(ChamsMode::OutlineSilhouette) : *lastMaterial;
    }
    Dummy(ImVec2(0, 2));
    if (IsOutlineMode(*mode)) {
        int sub = *mode - 1;
        if (m3::Segmented("outline", &sub, { u8"Контур", u8"Силуэт", u8"Оба" })) *mode = sub + 1;
        Dummy(ImVec2(0, 2));
        m3::ColorRow(u8"Цвет подсветки", color);
        m3::Slider(u8"Толщина контура", width, 1.f, 10.f, "%.1f");
    } else if (IsMaterialMode(*mode)) {
        m3::SectionLabel(u8"МАТЕРИАЛ");
        m3::FlowBegin();
        for (const MaterialInfo& mi : kMaterials) {
            const bool sel = *mode == static_cast<int>(mi.mode);
            m3::FlowNext(m3::ChipWidth(mi.name, sel ? theme::icon::Check : nullptr));
            if (m3::Chip(mi.name, sel)) *mode = *lastMaterial = static_cast<int>(mi.mode);
            if (IsItemHovered()) SetTooltip("%s", mi.hint);
        }
        for (const MaterialInfo& mi : kMaterials)
            if (*mode == static_cast<int>(mi.mode)) m3::Supporting(mi.hint);
        Dummy(ImVec2(0, 2));
        m3::ColorRow(*mode == static_cast<int>(ChamsMode::Xqz) ? u8"Цвет за стеной" : u8"Основной цвет", color);
        if (UsesHiddenColor(*mode)) m3::ColorRow(u8"Цвет за стеной", hidden);
        if (*mode == static_cast<int>(ChamsMode::Glow) || *mode == static_cast<int>(ChamsMode::Neon))
            m3::Slider(u8"Яркость свечения", &g_cfg.chamsGlow, 0.5f, 8.f, "%.1f");
    }
    if (*mode != 0) {
        m3::Switch(u8"Радуга", rainbow, u8"Плавная смена оттенков");
        m3::Switch(u8"Пульсация", pulse, u8"Мерцание прозрачности");
    }
    PopID();
}

// --------------------------------------------------------------------------- Visual
void PageVisual() {
    Columns2("vis", [] {
        if (m3::BeginCard(u8"ESP врагов", icon::Eye, u8"Рамки, имена и дистанция")) {
            m3::Switch(u8"Включить", &g_cfg.espEnemies);
            m3::SectionLabel(u8"СТИЛЬ РАМКИ");
            m3::Segmented("box", &g_cfg.enemyBox, { u8"Нет", u8"Рамка", u8"Углы" });
            Dummy(ImVec2(0, 4));
            m3::Switch(u8"Имя", &g_cfg.enemyName);
            m3::Switch(u8"Дистанция", &g_cfg.enemyDistance);
            m3::Switch(u8"Состояние ИИ", &g_cfg.enemyState, u8"Погоня, поиск, заморозка");
            m3::Switch(u8"Линии к врагам", &g_cfg.enemySnaplines);
            m3::Switch(u8"Стрелки за экраном", &g_cfg.enemyOffscreen, u8"Указывают на врагов вне поля зрения");
            m3::ColorRow(u8"Цвет", g_cfg.enemyColor);
            m3::Slider(u8"Дальность", &g_cfg.enemyMaxDist, 10.f, 500.f, u8"%.0f м");
        }
        m3::EndCard();
        if (m3::BeginCard(u8"Радар", icon::Location, u8"Вид сверху вокруг игрока")) {
            m3::Switch(u8"Мини-радар", &g_cfg.radar);
            m3::Slider(u8"Размер", &g_cfg.radarSize, 120.f, 320.f, "%.0f px");
            m3::Slider(u8"Охват", &g_cfg.radarRange, 10.f, 120.f, u8"%.0f м");
        }
        m3::EndCard();
    }, [] {
        if (m3::BeginCard(u8"Chams врагов", icon::Palette, u8"Подсветка моделей сквозь стены")) {
            static int lastMat = static_cast<int>(ChamsMode::TwoTone);
            ChamsEditor("ech", &g_cfg.enemyChams, g_cfg.enemyChamsColor, g_cfg.enemyChamsHidden, &g_cfg.enemyChamsRainbow,
                        &g_cfg.enemyChamsPulse, &g_cfg.enemyOutlineWidth, &lastMat);
        }
        m3::EndCard();
        if (m3::BeginCard(u8"ESP предметов", icon::Package, u8"Ключи, инструменты и оружие на карте")) {
            m3::Switch(u8"Включить", &g_cfg.espItems);
            m3::Switch(u8"Дистанция", &g_cfg.itemDistance);
            m3::ColorRow(u8"Цвет", g_cfg.itemColor);
            m3::Slider(u8"Дальность", &g_cfg.itemMaxDist, 5.f, 200.f, u8"%.0f м");
            m3::SearchField("##itemfilter", u8"Фильтр по названию", g_cfg.itemFilter, sizeof(g_cfg.itemFilter));
        }
        m3::EndCard();
        if (m3::BeginCard(u8"Chams предметов", icon::Palette, u8"Работает в пределах дальности ESP")) {
            static int lastMat = static_cast<int>(ChamsMode::Glow);
            ChamsEditor("ich", &g_cfg.itemChams, g_cfg.itemChamsColor, g_cfg.itemChamsHidden, &g_cfg.itemChamsRainbow,
                        &g_cfg.itemChamsPulse, &g_cfg.itemOutlineWidth, &lastMat);
        }
        m3::EndCard();
    });
}

// --------------------------------------------------------------------------- Player
void PagePlayer() {
    NotInGameHint();
    Columns2("ply", [] {
        if (m3::BeginCard(u8"Выживание", icon::Shield)) {
            m3::Switch(u8"Бессмертие", &g_cfg.godmode, u8"Отменяет поимку, удары и ловушки");
            m3::Switch(u8"Невидимость", &g_cfg.invisible, u8"Granny, Grandpa и мама-паук вас не видят");
            Dummy(ImVec2(0, 2));
            if (m3::Button(u8"Сбросить погоню", Btn::Tonal, 0, icon::Refresh)) game::StopAllChases();
        }
        m3::EndCard();
        if (m3::BeginCard(u8"Камера", icon::Camera)) {
            m3::Switch(u8"Свой угол обзора", &g_cfg.fovOn);
            m3::Slider("FOV", &g_cfg.fov, 50.f, 130.f, u8"%.0f°");
        }
        m3::EndCard();
    }, [] {
        if (m3::BeginCard(u8"Движение", icon::Run)) {
            m3::Switch("Speedhack", &g_cfg.speedhack, u8"Ускоряет ходьбу и бег");
            m3::Slider(u8"Множитель", &g_cfg.speedMult, 1.f, 6.f, "x%.1f");
            m3::Switch("Noclip", &g_cfg.noclip, u8"Полёт сквозь стены");
            m3::Slider(u8"Скорость полёта", &g_cfg.noclipSpeed, 2.f, 30.f, u8"%.0f м/с");
            m3::Supporting(u8"WASD — направление, Space/E — вверх, Ctrl/Q — вниз, Shift — быстрее.");
        }
        m3::EndCard();
        if (m3::BeginCard(u8"Телепорт", icon::Pin)) {
            if (g_snap.hasSavedPoint) {
                char buf[96];
                std::snprintf(buf, sizeof(buf), u8"Точка: %.1f, %.1f, %.1f", g_snap.savedPoint.x, g_snap.savedPoint.y, g_snap.savedPoint.z);
                m3::Supporting(buf);
            } else {
                m3::Supporting(u8"Точка не сохранена");
            }
            Dummy(ImVec2(0, 2));
            if (m3::Button(u8"Сохранить точку", Btn::Tonal, 0, icon::Save)) game::SavePoint();
            SameLine();
            if (m3::Button(u8"Телепорт", Btn::Filled, 0, icon::Location)) game::TeleportToSaved();
        }
        m3::EndCard();
    });
}

// --------------------------------------------------------------------------- Weapons
void PageWeapons() {
    NotInGameHint();
    Columns2("wpn", [] {
        if (m3::BeginCard(u8"Выдать оружие", icon::Bolt, u8"Текущий предмет будет выброшен")) {
            struct W { const char* label; game::Special s; };
            const W weapons[] = {
                { u8"Арбалет", game::Special::Crossbow },
                { u8"Двустволка", game::Special::DoubleShotgun },
                { u8"Дробовик", game::Special::Shotgun },
                { u8"Перцовый баллончик", game::Special::PepperSpray },
                { u8"Электрошокер", game::Special::Baton },
                { u8"Ледяная ловушка", game::Special::FreezeTrap },
                { u8"Ядовитая ловушка", game::Special::PoisonTrap },
            };
            m3::FlowBegin();
            for (const W& w : weapons) {
                m3::FlowNext(m3::ButtonWidth(w.label, icon::Add));
                if (m3::Button(w.label, Btn::Tonal, 0, icon::Add)) game::GiveSpecial(w.s);
            }
        }
        m3::EndCard();
        if (m3::BeginCard(u8"В руках", icon::Package)) {
            m3::Badge(g_snap.heldItem.empty() ? u8"Пусто" : g_snap.heldItem.c_str(), S.primaryContainer, S.onPrimaryContainer);
            char buf[160];
            std::snprintf(buf, sizeof(buf), u8"Двустволка: %.0f/2   ·   Дробовик: %s   ·   Арбалет: %s   ·   Баллончик: %.0f",
                          g_snap.ammo, g_snap.shotgunLoaded ? u8"заряжен" : u8"пуст", g_snap.crossbowCharged ? u8"заряжен" : u8"пуст",
                          g_snap.spray);
            m3::Supporting(buf);
        }
        m3::EndCard();
    }, [] {
        if (m3::BeginCard(u8"Боезапас", icon::Shield)) {
            m3::Switch(u8"Бесконечные патроны", &g_cfg.infiniteAmmo, u8"Арбалет, оба дробовика и баллончик");
            m3::Switch(u8"Заряженные дротики", &g_cfg.electricDarts, u8"Дротики арбалета бьют током");
        }
        m3::EndCard();
        if (m3::BeginCard("Aimbot", icon::Location, u8"Цель — враг ближе всего к прицелу")) {
            m3::Switch(u8"Наводка камеры", &g_cfg.aimCamera, u8"Пока зажата клавиша наводки");
            m3::KeyBind(u8"Клавиша наводки", &g_cfg.aimKey);
            m3::Slider(u8"Плавность", &g_cfg.aimSmooth, 1.f, 20.f, "%.1f");
            m3::Switch("Silent aim", &g_cfg.silentAim, u8"Пули и дротики летят в цель, камера не двигается");
            m3::SectionLabel(u8"ТОЧКА ПРИЦЕЛИВАНИЯ");
            m3::Segmented("bone", &g_cfg.aimBone, { u8"Голова", u8"Грудь", u8"Центр" });
            Dummy(ImVec2(0, 2));
            m3::Slider(u8"Угол захвата", &g_cfg.aimFov, 1.f, 90.f, u8"%.0f°");
            m3::Slider(u8"Дальность", &g_cfg.aimMaxDist, 5.f, 150.f, u8"%.0f м");
            m3::Switch(u8"Показывать круг", &g_cfg.aimDrawFov);
            m3::Switch(u8"Пропускать замороженных", &g_cfg.aimIgnoreFrozen);
        }
        m3::EndCard();
        if (m3::BeginCard(u8"Скорострельность", icon::Clock)) {
            m3::Switch("Rapid fire", &g_cfg.rapidFire, u8"Зажмите кнопку выстрела");
            m3::SliderInt(u8"Интервал", &g_cfg.rapidIntervalMs, 40, 800, u8"%d мс");
            m3::Supporting(u8"Стреляет без анимации перезарядки. Для арбалета — без ожидания заряда.");
        }
        m3::EndCard();
    });
}

// --------------------------------------------------------------------------- Items
void PageItems() {
    NotInGameHint();
    if (m3::BeginCard(u8"Выдача предметов", icon::Package, u8"Список берётся прямо из инвентаря игры")) {
        m3::Badge(g_snap.heldItem.empty() ? u8"В руках: пусто" : (u8"В руках: " + g_snap.heldItem).c_str(), S.primaryContainer,
                  S.onPrimaryContainer);
        SameLine();
        if (m3::Button(u8"Выбросить", Btn::Text)) game::DropHeld();
        m3::Switch(u8"Автоприменение", &g_cfg.autoUse,
                   u8"Нужный ключ или инструмент применяется сам, если его нет в руках");
        m3::SearchField("##itemsearch", u8"Поиск предмета", g_itemSearch, sizeof(g_itemSearch));
        Dummy(ImVec2(0, 2));
        if (g_snap.giveList.empty()) m3::Supporting(u8"Список появится после загрузки уровня.");
        m3::FlowBegin();
        for (const GiveEntry& e : g_snap.giveList) {
            if (!Matches(e.label, g_itemSearch) && !Matches(e.itemName, g_itemSearch)) continue;
            PushID(e.itemName.c_str());
            const char* label = e.label.c_str();
            m3::FlowNext(m3::ChipWidth(label, icon::Add));
            if (m3::Chip(label, false, icon::Add)) game::GiveItem(e.itemName);
            if (IsItemHovered()) SetTooltip("%s", e.itemName.c_str());
            PopID();
        }
    }
    m3::EndCard();

    if (m3::BeginCard(u8"Предметы на карте", icon::Location, u8"Ближайшие к вам — телепорт или притянуть к себе")) {
        int shown = 0;
        if (BeginTable("worlditems", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
            TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch, 1.f);
            TableSetupColumn("dist", ImGuiTableColumnFlags_WidthFixed, 70.f);
            TableSetupColumn("act", ImGuiTableColumnFlags_WidthFixed, 92.f);
            PushStyleColor(ImGuiCol_TableRowBg, ToVec4(IM_COL32(0, 0, 0, 0)));
            PushStyleColor(ImGuiCol_TableRowBgAlt, ToVec4(WithAlpha(S.onSurface, 0.03f)));
            for (const ItemView& it : g_snap.items) {
                if (!Matches(it.name, g_itemSearch)) continue;
                if (++shown > 60) break;
                PushID(reinterpret_cast<void*>(it.id));
                TableNextRow(0, 44.f);
                TableNextColumn();
                AlignTextToFramePadding();
                Text("%s", it.name.c_str());
                TableNextColumn();
                AlignTextToFramePadding();
                TextColored(ToVec4(S.onSurfaceVariant), u8"%.0f м", it.dist);
                TableNextColumn();
                if (m3::IconButton("tp", icon::Location, false, u8"Телепорт к предмету")) game::TeleportToItem(it.id);
                SameLine(0, 4);
                if (m3::IconButton("bring", icon::Download, true, u8"Притянуть к себе")) game::BringItem(it.id);
                PopID();
            }
            PopStyleColor(2);
            EndTable();
        }
        if (!shown) m3::Supporting(u8"Предметов поблизости нет.");
    }
    m3::EndCard();
}

// --------------------------------------------------------------------------- Enemies
void EnemyCard(const EnemyView& e) {
    PushID(reinterpret_cast<void*>(e.id));
    char sub[96];
    std::snprintf(sub, sizeof(sub), u8"%s%s%.0f м", e.state.c_str(), e.state.empty() ? "" : u8"  ·  ", e.dist);
    char title[96];
    std::snprintf(title, sizeof(title), "%s##card", e.name.c_str());
    if (m3::BeginCard(title, icon::Bug, sub)) {
        using A = game::EnemyAction;
        struct Act { const char* label; A action; uint32_t cap; Btn style; const char* ic; };
        const Act acts[] = {
            { e.frozenSingle ? u8"Разморозить" : u8"Заморозить", A::ToggleFreeze, 0, e.frozenSingle ? Btn::Filled : Btn::Tonal, icon::Snow },
            { u8"Нокаут", A::Knockout, CAP_KNOCKOUT, Btn::Tonal, nullptr },
            { u8"Перец", A::Pepper, CAP_PEPPER, Btn::Tonal, nullptr },
            { u8"Лёд", A::FreezeFx, CAP_FREEZEFX, Btn::Tonal, nullptr },
            { u8"Погоня", A::Chase, CAP_CHASE, Btn::Outlined, nullptr },
            { u8"Стоп", A::StopChase, CAP_CHASE, Btn::Outlined, nullptr },
            { u8"Ко мне", A::TeleportToMe, CAP_MOVE, Btn::Outlined, icon::Location },
            { u8"На точку", A::TeleportToPoint, CAP_MOVE, Btn::Outlined, icon::Pin },
            { u8"Убить", A::Kill, CAP_KILL, Btn::Error, nullptr },
        };
        m3::FlowBegin();
        for (const Act& a : acts) {
            if (a.cap && !(e.caps & a.cap)) continue;
            m3::FlowNext(m3::ButtonWidth(a.label, a.ic));
            if (m3::Button(a.label, a.style, 0, a.ic)) game::DoEnemyAction(e.id, a.action);
        }
        m3::FlowNext(m3::ButtonWidth(u8"ТП к нему", icon::Location));
        if (m3::Button(u8"ТП к нему", Btn::Text, 0, icon::Location)) game::TeleportToEnemy(e.id);
    }
    m3::EndCard();
    PopID();
}

void PageEnemies() {
    NotInGameHint();
    if (m3::BeginCard(u8"Глобально", icon::Bug, u8"Действует на всех врагов уровня")) {
        Columns2("aiglob", [] {
            m3::Switch(u8"Заморозить всех", &g_cfg.freezeAll, u8"ИИ, анимации и навигация на паузе");
            m3::Switch(u8"Невидимость", &g_cfg.invisible, u8"Враги не замечают игрока");
        }, [] {
            m3::Switch(u8"Скорость врагов", &g_cfg.enemySpeedOn, u8"Множитель ходьбы и бега");
            m3::Slider(u8"Множитель", &g_cfg.enemySpeedMult, 0.f, 3.f, "x%.2f");
        });
        if (m3::Button(u8"Сбросить погоню", Btn::Tonal, 0, icon::Refresh)) game::StopAllChases();
    }
    m3::EndCard();

    if (g_snap.enemies.empty()) {
        if (m3::BeginCard(u8"Врагов не найдено", icon::Info)) m3::Supporting(u8"На уровне сейчас нет активных врагов.");
        m3::EndCard();
        return;
    }
    // two columns of enemy cards
    if (BeginTable("enemies", 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoPadOuterX)) {
        for (size_t i = 0; i < g_snap.enemies.size(); ++i) {
            if (i % 2 == 0) TableNextRow();
            TableNextColumn();
            EnemyCard(g_snap.enemies[i]);
        }
        EndTable();
    }
}

// --------------------------------------------------------------------------- World
void PageWorld() {
    NotInGameHint();
    Columns2("world", [] {
        if (m3::BeginCard(u8"Освещение", icon::Brightness)) {
            m3::Switch(u8"Яркий мир", &g_cfg.fullbright, u8"Равномерный свет без теней-ловушек");
            m3::Slider(u8"Яркость", &g_cfg.fullbrightLevel, 0.1f, 1.5f, "%.2f");
            m3::Switch(u8"Без тумана", &g_cfg.noFog);
        }
        m3::EndCard();
    }, [] {
        if (m3::BeginCard(u8"Время", icon::Clock)) {
            m3::Switch(u8"Скорость игры", &g_cfg.timescaleOn, u8"Замедление или ускорение всего мира");
            m3::Slider(u8"Множитель", &g_cfg.timescale, 0.1f, 5.f, "x%.2f");
        }
        m3::EndCard();
    });
}

// --------------------------------------------------------------------------- Settings
void SeedSwatches() {
    const float hues[] = { 265.f, 290.f, 320.f, 345.f, 15.f, 35.f, 55.f, 95.f, 130.f, 160.f, 195.f, 225.f };
    ImGuiWindow* w = GetCurrentWindow();
    m3::FlowBegin();  // wraps to the next row instead of running off the card
    for (int i = 0; i < static_cast<int>(sizeof(hues) / sizeof(hues[0])); ++i) {
        m3::FlowNext(40.f);
        PushID(i);
        const ImVec2 pos = GetCursorScreenPos();
        if (InvisibleButton("seed", ImVec2(40, 40))) g_cfg.seedHue = hues[i];
        const ImGuiID id = GetItemID();
        const bool sel = std::fabs(g_cfg.seedHue - hues[i]) < 0.5f;
        const float st = motion::SpringTo(id, sel ? 1.f : 0.f, motion::kSpatialFast);
        const float hv = motion::SpringTo(id + 1, IsItemHovered() ? 1.f : 0.f, motion::kSpatialFast);
        const ImVec2 c = pos + ImVec2(20, 20);
        // selected seed morphs from a cookie into a flower and turns
        motion::FillShape(w->DrawList, c, 16.f + 2.f * hv, motion::Shape::Cookie9, motion::Shape::Flower, motion::Clamp01(st), st * 0.9f,
                          theme::Tone(hues[i], 0.26f * g_cfg.seedChroma + 0.02f, 70));
        w->DrawList->AddCircleFilled(c, 7.f * (0.6f + 0.4f * st), theme::Tone(hues[i] + 60.f, 0.18f * g_cfg.seedChroma, 88), 24);
        if (st > 0.02f) w->DrawList->AddCircle(c, 21.f, WithAlpha(S.onSurface, motion::Clamp01(st)), 40, 2.f);
        PopID();
    }
}

void PageSettings() {
    Columns2("set", [] {
        if (m3::BeginCard(u8"Тема Material You", icon::Palette, u8"Палитра строится из одного цвета")) {
            SeedSwatches();
            Dummy(ImVec2(0, 4));
            m3::Slider(u8"Оттенок", &g_cfg.seedHue, 0.f, 360.f, u8"%.0f°");
            m3::Slider(u8"Насыщенность", &g_cfg.seedChroma, 0.f, 1.f, "%.2f");
            m3::Switch(u8"Светлая тема", &g_cfg.lightTheme);
            m3::SectionLabel(u8"УВЕДОМЛЕНИЯ");
            m3::Segmented("toast", &g_cfg.toastStyle, { u8"Как тема", u8"Контрастные" });
        }
        m3::EndCard();
        if (m3::BeginCard(u8"Материал окна", icon::Brightness, u8"Прозрачность и размытие фона под меню")) {
            m3::Segmented("glass", &g_cfg.glassMode, { u8"Нет", u8"Стекло", u8"Матовое", u8"Акрил" });
            const char* hints[] = { u8"Просто полупрозрачный фон — игра видна чётко",
                                    u8"Лёгкое размытие, блик сверху и светлый ободок",
                                    u8"Сильное размытие с мелким зерном",
                                    u8"Размытие, зерно и выравнивающий тон, как в Windows 11" };
            m3::Supporting(hints[ImClamp(g_cfg.glassMode, 0, 3)]);
            Dummy(ImVec2(0, 2));
            float transparency = g_cfg.menuTransparency * 100.f;
            if (m3::Slider(u8"Прозрачность", &transparency, 0.f, 80.f, "%.0f%%")) g_cfg.menuTransparency = transparency / 100.f;
            if (g_cfg.glassMode != 0) m3::Slider(u8"Сила размытия", &g_cfg.blurStrength, 0.f, 1.f, "%.2f");
        }
        m3::EndCard();
        if (m3::BeginCard(u8"Конфигурация", icon::Folder, u8"granny_menu.ini рядом с DLL")) {
            if (m3::Button(u8"Сохранить", Btn::Filled, 0, icon::Save))
                state::Notify(SaveConfig(g_configPath) ? u8"Конфиг сохранён" : u8"Не удалось сохранить конфиг");
            SameLine();
            if (m3::Button(u8"Загрузить", Btn::Outlined, 0, icon::Folder))
                state::Notify(LoadConfig(g_configPath) ? u8"Конфиг загружен" : u8"Файл конфига не найден");
        }
        m3::EndCard();
    }, [] {
        if (m3::BeginCard(u8"Горячие клавиши", icon::Keyboard, u8"Esc или Backspace — сбросить")) {
            m3::KeyBind(u8"Меню", &g_cfg.keyMenu);
            m3::KeyBind(u8"Выгрузить чит", &g_cfg.keyUnload);
        }
        m3::EndCard();
        if (m3::BeginCard(u8"Быстрый доступ", icon::Bolt, u8"Кнопка-панель и горячие клавиши функций")) {
            m3::Switch(u8"Кнопка быстрого доступа", &g_cfg.fabEnabled, u8"Круглая кнопка в углу меню");
            m3::Supporting(u8"«Панель» — показывать функцию на кнопке (до 8). Клавиша переключает функцию в любой момент.");
            Dummy(ImVec2(0, 2));
            if (BeginTable("quick", 2, ImGuiTableFlags_SizingStretchProp)) {
                TableSetupColumn("bind", ImGuiTableColumnFlags_WidthStretch, 1.f);
                TableSetupColumn("panel", ImGuiTableColumnFlags_WidthFixed, 96.f);
                for (int i = 0; i < ToggleCount(); ++i) {
                    const ToggleDef& t = Toggles()[i];
                    PushID(i);
                    TableNextRow();
                    TableNextColumn();
                    const std::string label = std::string(t.icon) + "   " + t.label;
                    m3::KeyBind(label.c_str(), &g_cfg.toggleKeys[i], 96.f);
                    TableNextColumn();
                    SetCursorPosY(GetCursorPosY() + 4.f);
                    const bool inPanel = (g_cfg.quickMask & (1 << i)) != 0;
                    if (m3::Chip(u8"Панель", inPanel)) g_cfg.quickMask ^= (1 << i);
                    PopID();
                }
                EndTable();
            }
        }
        m3::EndCard();
        if (m3::BeginCard("Granny Legacy Menu", icon::Info, u8"IL2CPP · Unity 2022.3 · D3D11")) {
            m3::Supporting(u8"Все функции работают через имена классов и методов игры, без жёстко прописанных адресов.");
            Dummy(ImVec2(0, 2));
            if (m3::Button(u8"Выгрузить чит", Btn::Error, 0, icon::Power)) render::g_unloadRequested = true;
        }
        m3::EndCard();
    });
}

void DrawRail(float height) {
    PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 4));
    BeginChild("##rail", ImVec2(96, height), 0, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);
    ImGuiWindow* w = GetCurrentWindow();
    // logo: a slowly turning cookie that blooms into a flower on hover
    const ImVec2 lp = GetCursorScreenPos() + ImVec2(48, 30);
    const float hv = motion::SpringTo(GetID("##logo"), IsMouseHoveringRect(lp - ImVec2(26, 26), lp + ImVec2(26, 26)) ? 1.f : 0.f, motion::kSpatialFast);
    motion::FillShape(w->DrawList, lp, 25.f + 2.f * hv, motion::Shape::Cookie9, motion::Shape::Flower, motion::Clamp01(hv),
                      motion::Time() * 0.25f, S.primaryContainer);
    const ImVec2 gs = fontBold->CalcTextSizeA(24.f, FLT_MAX, 0, "G");
    w->DrawList->AddText(fontBold, 24.f, lp - gs * 0.5f, S.onPrimaryContainer, "G");
    Dummy(ImVec2(0, 74));
    const ImVec2 itemsTop = GetCursorScreenPos();
    m3::RailIndicator(w->DrawList, ImVec2(itemsTop.x + 48.f, itemsTop.y + g_page * (62.f + 4.f)));
    for (int i = 0; i < PCount; ++i)
        if (m3::RailItem(kPages[i].icon, kPages[i].rail, g_page == i)) g_page = i;
    SetCursorPosY(height - 56.f);
    SetCursorPosX(28.f);
    if (m3::IconButton("##unload", icon::Power, false, u8"Выгрузить чит")) render::g_unloadRequested = true;
    EndChild();
    PopStyleVar();
}

// ---------------------------------------------------------------------------
// Expressive FAB menu: quick toggles that fan out above the button with a staggered spring
// ---------------------------------------------------------------------------
bool g_fabOpen = false;
motion::Spring g_fab;

float g_lastScrollTime = -10.f;
motion::Spring g_fabHide;

void DrawFab(ImVec2 anchorBR) {
    // slides away while the page scrolls, comes back shortly after it stops
    const bool scrolling = motion::Time() - g_lastScrollTime < 0.6f;
    const float hide = g_fabHide.Update(scrolling ? 1.f : 0.f, motion::kSpatial);
    if (scrolling) g_fabOpen = false;
    anchorBR.y += motion::Clamp01(hide) * 96.f;
    if (!g_cfg.fabEnabled) {
        g_fabOpen = false;
        return;
    }
    int ids[kMaxToggles];
    int n = 0;
    for (int i = 0; i < ToggleCount() && n < 8; ++i)
        if (g_cfg.quickMask & (1 << i)) ids[n++] = i;
    const float t = g_fab.Update(g_fabOpen ? 1.f : 0.f, motion::kSpatial);
    const float itemH = 48.f, gap = 8.f, fab = 56.f;
    const float menuH = t > 0.01f ? n * (itemH + gap) + 12.f : 0.f;
    const ImVec2 regionMin(anchorBR.x - (t > 0.01f ? 300.f : fab + 24.f), anchorBR.y - fab - menuH - 24.f);  // collapsed: only the button
    SetCursorScreenPos(regionMin);
    BeginChild("##fab", anchorBR + ImVec2(24, 24) - regionMin, 0,  // margin so soft shadows are not clipped
               ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImDrawList* dl = GetWindowDrawList();

    for (int k = 0; k < n && t > 0.01f; ++k) {
        // item 0 sits right above the FAB; later items follow with a small delay
        const float ti = motion::Clamp01(t * (1.f + n * 0.1f) - k * 0.1f);
        if (ti <= 0.01f) continue;
        const ToggleDef& q = Toggles()[ids[k]];
        const int key = g_cfg.toggleKeys[ids[k]];
        const char* keyName = key ? m3::KeyName(key) : nullptr;
        const ImVec2 ts = fontBold->CalcTextSizeA(14.f, FLT_MAX, 0, q.label);
        const ImVec2 ks = keyName ? fontBold->CalcTextSizeA(11.5f, FLT_MAX, 0, keyName) : ImVec2(0, 0);
        const float wItem = ts.x + 68.f + (keyName ? ks.x + 22.f : 0.f);
        const float y = anchorBR.y - fab - (k + 1) * (itemH + gap) + (1.f - ti) * 22.f;
        const ImVec2 mn(anchorBR.x - wItem, y), mx(anchorBR.x, y + itemH);
        SetCursorScreenPos(mn);
        PushID(ids[k]);
        const bool clicked = InvisibleButton("quick", mx - mn) && ti > 0.6f;
        const bool hov = IsItemHovered();
        PopID();
        if (clicked) *q.value = !*q.value;
        const bool on = *q.value;
        const ImU32 bg = on ? S.primary : S.surfaceContainerHighest, fg = on ? S.onPrimary : S.onSurface;
        motion::SoftShadow(dl, mn, mx, itemH * 0.5f, 6.f, ti);
        dl->AddRectFilled(mn, mx, WithAlpha(bg, ti), itemH * 0.5f);
        if (hov) dl->AddRectFilled(mn, mx, WithAlpha(fg, 0.08f * ti), itemH * 0.5f);
        const ImVec2 icSz = fontRegular->CalcTextSizeA(18.f, FLT_MAX, 0, q.icon);
        dl->AddText(fontRegular, 18.f, ImVec2(mn.x + 20.f, mn.y + (itemH - icSz.y) * 0.5f), WithAlpha(fg, ti), q.icon);
        dl->AddText(fontBold, 14.f, ImVec2(mn.x + 48.f, mn.y + (itemH - ts.y) * 0.5f), WithAlpha(fg, ti), q.label);
        if (keyName) {  // assigned hotkey as a small badge
            const ImVec2 bMin(mx.x - ks.x - 30.f, mn.y + 13.f), bMax(mx.x - 12.f, mx.y - 13.f);
            dl->AddRectFilled(bMin, bMax, WithAlpha(fg, 0.14f * ti), 8.f);
            dl->AddText(fontBold, 11.5f, ImVec2(bMin.x + 9.f, bMin.y + (bMax.y - bMin.y - ks.y) * 0.5f), WithAlpha(fg, 0.9f * ti), keyName);
        }
    }

    // the FAB: rounded square that morphs into a circle while the menu is open
    const ImVec2 fMin(anchorBR.x - fab, anchorBR.y - fab), fMax = anchorBR;
    SetCursorScreenPos(fMin);
    if (InvisibleButton("##fabbtn", ImVec2(fab, fab))) g_fabOpen = !g_fabOpen;
    const bool hov = IsItemHovered(), held = IsItemActive();
    const float press = motion::SpringTo(GetID("##fabpress"), held ? 1.f : 0.f, motion::kSpatialFast);
    const float lift = motion::SpringTo(GetID("##fablift"), hov ? 1.f : 0.f, motion::kEffects);
    const float r = 16.f + 12.f * motion::Clamp01(t) - 4.f * press;
    const ImU32 bg = Mix(S.primaryContainer, S.primary, motion::Clamp01(t));
    const ImU32 fg = Mix(S.onPrimaryContainer, S.onPrimary, motion::Clamp01(t));
    motion::SoftShadow(dl, fMin, fMax, r, 8.f + 4.f * lift - 3.f * press);  // elevation rises on hover, drops on press
    dl->AddRectFilled(fMin, fMax, bg, r);
    if (hov) dl->AddRectFilled(fMin, fMax, WithAlpha(fg, held ? 0.12f : 0.08f), r);
    const ImVec2 c = (fMin + fMax) * 0.5f;
    const float a = motion::Clamp01(t);
    const float s1 = 22.f * (1.f - 0.4f * a), s2 = 20.f * (0.6f + 0.4f * a);
    const ImVec2 b1 = fontRegular->CalcTextSizeA(s1, FLT_MAX, 0, icon::Bolt), b2 = fontRegular->CalcTextSizeA(s2, FLT_MAX, 0, icon::Close);
    if (a < 0.99f) dl->AddText(fontRegular, s1, c - b1 * 0.5f, WithAlpha(fg, 1.f - a), icon::Bolt);
    if (a > 0.01f) dl->AddText(fontRegular, s2, c - b2 * 0.5f, WithAlpha(fg, a), icon::Close);
    if (n == 0 && hov) SetTooltip(u8"Добавьте функции в «Настройки → Быстрый доступ»");
    EndChild();
}

unsigned g_visit = 1;
int g_lastPage = -1;
bool g_wasOpen = false;
motion::Spring g_window;
ImVec2 g_winBase(-1.f, -1.f);

std::unordered_map<uint64_t, motion::Spring> g_toastSprings;

// ---------------------------------------------------------------------------
// Window materials: glass / frosted / acrylic over a GPU-blurred copy of the game frame
// ---------------------------------------------------------------------------
struct Glass { float tint; float pane; float card; bool blur; };

Glass GlassParams() {
    const float op = 1.f - ImClamp(g_cfg.menuTransparency, 0.f, 0.8f);
    // Legibility floor: Granny's world is mostly dark, so a light theme needs a denser surface for its dark text
    const bool light = g_cfg.lightTheme;
    const float floorTint = light ? 0.62f : 0.36f;
    float k = 1.f;
    bool blurred = true;
    switch (g_cfg.glassMode) {
    case 1: k = 0.55f; break;  // glass
    case 2: k = 0.78f; break;  // frosted
    case 3: k = 0.68f; break;  // acrylic
    default: blurred = false; break;
    }
    const float tint = ImMax(op * k, blurred ? floorTint : floorTint + 0.15f);
    return { tint, ImMin(1.f, tint + 0.12f), ImMin(1.f, tint + 0.24f), blurred };
}

// Paints the surface of a rounded panel: blurred backdrop, tint and the mode's highlights
void DrawSurface(ImDrawList* dl, ImVec2 mn, ImVec2 mx, float rounding, ImU32 tint, const Glass& g) {
    const ImVec2 disp = GetIO().DisplaySize;
    const ImTextureID tex = g.blur ? blur::Texture() : (ImTextureID)0;
    if (tex) dl->AddImageRounded(ImTextureRef(tex), mn, mx, mn / disp, mx / disp, IM_COL32_WHITE, rounding);
    dl->AddRectFilled(mn, mx, WithAlpha(tint, tex ? g.tint : (g.blur ? ImMax(g.tint, 0.85f) : g.tint)), rounding);
    if (tex && (g_cfg.glassMode == 3 || g_cfg.lightTheme)) {
        // luminosity layer: lifts dark backdrops under light surfaces (always in light theme, acrylic in both)
        const int lum = g_cfg.glassMode == 3 ? 46 : 30;
        dl->AddRectFilled(mn, mx, g_cfg.lightTheme ? IM_COL32(255, 255, 255, lum) : IM_COL32(0, 0, 0, 34), rounding);
    }
    if (g_cfg.glassMode == 1) {
        // glass: specular sheen across the top and a bright rim
        motion::GradientRoundedRect(dl, mn, ImVec2(mx.x, mn.y + (mx.y - mn.y) * 0.42f), rounding, IM_COL32(255, 255, 255, 34),
                                    IM_COL32(255, 255, 255, 0), ImDrawFlags_RoundCornersTop);
        dl->AddRect(mn, mx, IM_COL32(255, 255, 255, 70), rounding, 0, 1.3f);
        dl->AddRect(mn + ImVec2(1.3f, 1.3f), mx - ImVec2(1.3f, 1.3f), IM_COL32(0, 0, 0, 30), rounding - 1.3f, 0, 1.f);
    } else if (g.tint < 0.999f) {
        dl->AddRect(mn, mx, WithAlpha(S.outlineVariant, 0.55f), rounding, 0, 1.f);
    }
}

} // namespace

void LoadFonts() { theme::LoadFonts(); }
void ApplyTheme() { theme::Apply(); }
bool IsCapturingKey() { return m3::IsCapturingKey(); }

void Draw() {
    theme::Animate();
    const bool open = game::g_menuOpen;
    // opening: bouncy spatial spring; closing: quick effects spring
    const float vis = g_window.Update(open ? 1.f : 0.f, open ? motion::kSpatial : motion::kEffectsFast);
    if (!open && vis < 0.02f) {
        g_wasOpen = false;
        g_fabOpen = false;
        return;
    }
    if (open && !g_wasOpen) ++g_visit;  // replay card entrance on every open
    g_wasOpen = open;
    if (g_page != g_lastPage) {
        ++g_visit;
        g_lastPage = g_page;
    }

    g_snap = state::Get();
    const ImVec2 display = GetIO().DisplaySize;
    const ImVec2 size(ImMin(940.f, display.x - 40.f), ImMin(640.f, display.y - 40.f));
    if (g_winBase.x < 0.f) g_winBase = (display - size) * 0.5f;
    const bool settled = open && std::fabs(vis - 1.f) < 0.003f;
    const ImVec2 slide(0.f, (1.f - vis) * 48.f);
    if (!settled) SetNextWindowPos(g_winBase + slide, ImGuiCond_Always);  // leave the window draggable once settled
    SetNextWindowSize(size, ImGuiCond_Always);
    const Glass glass = GlassParams();
    if (glass.blur) blur::Request();
    m3::SetSurfaceAlpha(glass.card);
    PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));  // the surface is painted by DrawSurface
    PushFont(fontRegular, kBody);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
    if (!open) flags |= ImGuiWindowFlags_NoInputs;
    Begin("##grannymenu", nullptr, flags);
    const ImVec2 wp = GetWindowPos(), ws = GetWindowSize();
    if (settled) g_winBase = wp;
    ImDrawList* dl = GetWindowDrawList();
    ImGuiWindow* menuWindow = GetCurrentWindow();
    motion::SoftShadow(GetBackgroundDrawList(), wp, wp + ws, 28.f, 14.f, 0.8f * motion::Clamp01(vis));  // outside the window clip
    DrawSurface(dl, wp, wp + ws, 28.f, S.surface, glass);

    SetCursorPos(ImVec2(0, 12));
    DrawRail(ws.y - 24.f);

    // content pane
    const ImVec2 paneMin = wp + ImVec2(100, 12), paneMax = wp + ws - ImVec2(12, 12);
    dl->AddRectFilled(paneMin, paneMax, WithAlpha(S.surfaceContainerLow, glass.pane), 28.f);
    const PageInfo& pi = kPages[g_page];
    // emphasized headline slides in with every navigation
    const float ht = motion::SpringTo(ImHashData(&g_visit, sizeof(g_visit), GetID("##title")), 1.f, motion::kSpatial, 0.f);
    const float ha = motion::Clamp01(ht);
    dl->AddText(fontBold, 30.f, paneMin + ImVec2(28.f + (1.f - ht) * -28.f, 14.f), WithAlpha(S.onSurface, ha), pi.title);
    dl->AddText(fontRegular, kBody, paneMin + ImVec2(29.f + (1.f - ht) * -16.f, 52.f), WithAlpha(S.onSurfaceVariant, ha), pi.subtitle);

    // status + close in the header
    SetCursorScreenPos(ImVec2(paneMax.x - 196.f, paneMin.y + 26.f));
    if (InGame()) {
        m3::Badge(u8"●  В игре", S.primaryContainer, S.onPrimaryContainer);
    } else {
        const ImVec2 p = GetCursorScreenPos();
        motion::LoadingIndicator(dl, p + ImVec2(-18.f, 12.f), 8.f, S.primary);
        m3::Badge(u8"Ждём уровень", S.surfaceContainerHighest, S.onSurfaceVariant);
    }
    SetCursorScreenPos(ImVec2(paneMax.x - 60.f, paneMin.y + 18.f));
    if (m3::IconButton("##close", icon::Close, false, u8"Закрыть (Insert)")) game::g_menuOpen = false;

    SetCursorScreenPos(paneMin + ImVec2(16, 86));
    PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 4));
    PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(12, 12));
    BeginChild("##content", paneMax - paneMin - ImVec2(24, 96), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoBackground);
    m3::BeginPage(g_visit);
    switch (g_page) {
    case PVisual: PageVisual(); break;
    case PPlayer: PagePlayer(); break;
    case PWeapons: PageWeapons(); break;
    case PItems: PageItems(); break;
    case PEnemies: PageEnemies(); break;
    case PWorld: PageWorld(); break;
    case PSettings: PageSettings(); break;
    default: break;
    }
    Dummy(ImVec2(0, 80));  // room above the FAB
    static float lastScroll = 0.f;
    if (std::fabs(GetScrollY() - lastScroll) > 0.5f) g_lastScrollTime = motion::Time();
    lastScroll = GetScrollY();
    EndChild();
    PopStyleVar(2);

    DrawFab(paneMax - ImVec2(20.f, 20.f));

    End();
    PopFont();
    PopStyleColor();
    PopStyleVar();
    motion::FadeWindowTree(menuWindow, motion::Clamp01(vis));  // fades custom-drawn geometry too, children included
}

void DrawToasts() {
    const auto toasts = state::Toasts();
    // forget springs of toasts that are gone
    for (auto it = g_toastSprings.begin(); it != g_toastSprings.end();) {
        bool alive = false;
        for (const auto& t : toasts) alive |= t.timeMs == it->first;
        it = alive ? std::next(it) : g_toastSprings.erase(it);
    }
    if (toasts.empty()) return;
    ImDrawList* dl = GetForegroundDrawList();
    const ImVec2 display = GetIO().DisplaySize;
    const uint64_t now = state::NowMs();
    float y = display.y - 32.f;
    for (auto it = toasts.rbegin(); it != toasts.rend(); ++it) {
        const float age = static_cast<float>(now - it->timeMs);
        // snackbar springs up from below the edge, fades out at the end of its life
        motion::Spring& sp = g_toastSprings[it->timeMs];
        if (!sp.init) sp.Snap(0.f);
        const float in = sp.Update(age < 3100.f ? 1.f : 0.f, age < 3100.f ? motion::kSpatial : motion::kEffectsFast);
        const float a = motion::Clamp01(in);
        const ImVec2 ts = fontRegular->CalcTextSizeA(kBody, FLT_MAX, 0, it->text.c_str());
        const float w = ImMax(300.f, ts.x + 48.f), h = 54.f;
        const ImVec2 mn(display.x * 0.5f - w * 0.5f, y - h + (1.f - in) * 70.f);
        // "as theme": tonal surface of the current scheme; "contrast": M3 inverse snackbar
        const bool inverse = g_cfg.toastStyle == 1;
        const ImU32 bg = inverse ? S.inverseSurface : S.surfaceContainerHigh;
        const ImU32 fg = inverse ? S.inverseOnSurface : S.onSurface;
        const ImU32 accent = inverse ? S.inversePrimary : S.primary;
        motion::SoftShadow(dl, mn, mn + ImVec2(w, h), 14.f, 8.f, a);
        const Glass glass = GlassParams();
        if (!inverse && glass.blur) {
            blur::Request();
            const int vtx = dl->VtxBuffer.Size;
            DrawSurface(dl, mn, mn + ImVec2(w, h), 14.f, bg, Glass{ ImMax(glass.tint, 0.55f), 0, 0, true });
            motion::TransformDrawList(dl, vtx, dl->CmdBuffer.Size, ImVec2(0, 0), a);
        } else {
            dl->AddRectFilled(mn, mn + ImVec2(w, h), WithAlpha(bg, a), 14.f);
            if (!inverse) dl->AddRect(mn, mn + ImVec2(w, h), WithAlpha(S.outlineVariant, 0.6f * a), 14.f, 0, 1.f);
        }
        dl->AddText(fontRegular, kBody, mn + ImVec2(22, (h - ts.y) * 0.5f - 4.f), WithAlpha(fg, a), it->text.c_str());
        // remaining time as an M3 Expressive wavy progress indicator
        motion::WavyProgress(dl, ImVec2(mn.x + 22.f, mn.y + h - 11.f), w - 44.f, 1.f - age / 3500.f, WithAlpha(accent, a),
                             WithAlpha(fg, 0.22f * a), motion::Time());
        y -= (h + 10.f) * a;
    }
}

} // namespace menu
