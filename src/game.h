#pragma once
#include "il2cpp.h"
#include <atomic>
#include <string>

namespace game {

enum class Special { Crossbow, DoubleShotgun, Shotgun, PepperSpray, Baton, FreezeTrap, PoisonTrap };

enum class EnemyAction { Knockout, Pepper, FreezeFx, Chase, StopChase, TeleportToMe, TeleportToPoint, ToggleFreeze, Kill };

bool Init();            // resolve game metadata (worker thread)
bool InstallHooks();    // create + enable MinHook hooks

// Unload handshake: the main thread restores everything it changed, then reports done.
void RequestCleanup();
bool CleanupDone();

extern std::atomic<bool> g_menuOpen;

// --- Commands. Safe to call from any thread: they are queued to the Unity main thread.
void GiveItem(const std::string& itemName);
void GiveSpecial(Special which);
void DropHeld();
void DoEnemyAction(uintptr_t id, EnemyAction action);
void StopAllChases();
void SavePoint();
void TeleportToSaved();
void TeleportToItem(uintptr_t id);
void BringItem(uintptr_t id);
void TeleportToEnemy(uintptr_t id);

} // namespace game
