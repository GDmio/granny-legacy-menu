#pragma once
#include "il2cpp.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Data captured on the Unity main thread and consumed by the render thread.

enum EnemyCaps : uint32_t {
    CAP_KNOCKOUT = 1 << 0,
    CAP_PEPPER = 1 << 1,
    CAP_FREEZEFX = 1 << 2,
    CAP_CHASE = 1 << 3,
    CAP_KILL = 1 << 4,
    CAP_MOVE = 1 << 5,
};

struct EnemyView {
    uintptr_t id = 0;
    std::string name;
    Vec3 pos{};
    Vec3 bmin{}, bmax{};
    bool hasBounds = false;
    float dist = 0.f;
    std::string state;
    bool frozen = false;
    bool frozenSingle = false;
    uint32_t caps = 0;
};

struct ItemView {
    uintptr_t id = 0;
    std::string name;
    Vec3 pos{};
    float dist = 0.f;
};

struct DoorView {
    uintptr_t id = 0;
    std::string name;
    float dist = 0.f;
    bool open = false;
};

struct GiveEntry {
    std::string itemName;   // internal name used by Inventory.PickupItem
    std::string label;      // text the game shows on highlight
};

struct Snapshot {
    uint64_t tickMs = 0;
    bool inGame = false;
    Matrix4x4 view{}, proj{};
    Vec3 camPos{}, camFwd{};
    Vec3 playerPos{};
    std::vector<EnemyView> enemies;
    std::vector<ItemView> items;
    std::vector<GiveEntry> giveList;
    std::vector<DoorView> doors;
    std::string heldItem;
    float ammo = 0.f;
    bool shotgunLoaded = false;
    bool crossbowCharged = false;
    float spray = 0.f;
    bool hasSavedPoint = false;
    Vec3 savedPoint{};
    float camFov = 60.f;
    bool aimHasTarget = false;
    Vec3 aimTarget{};
};

namespace state {

void Publish(Snapshot&& snap);
Snapshot Get();

void Post(std::function<void()> cmd);   // queue work for the main thread
void RunCommands();                     // main thread

void Notify(const std::string& message);
struct Toast { std::string text; uint64_t timeMs; };
std::vector<Toast> Toasts();

uint64_t NowMs();

} // namespace state
