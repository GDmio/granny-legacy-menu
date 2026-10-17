# Granny Legacy Menu

**English** | [Русский](README.ru.md)

An in-game trainer menu for **Granny Legacy** (Unity 2022.3, IL2CPP, DirectX 11) with a **Material 3 Expressive** interface: spring animations, shape morphing and glass surfaces with real GPU blur. The interface is available in **English and Russian**.

![Platform](https://img.shields.io/badge/platform-Windows%20x64-0078D6)
![Unity](https://img.shields.io/badge/Unity-2022.3%20IL2CPP-black)
![Graphics](https://img.shields.io/badge/D3D11-ImGui-6f42c1)
![License](https://img.shields.io/badge/license-MIT-green)

> **Single-player only.** This project is not affiliated with DVloper or the game's publishers. The repository contains no game files, assets, dumps or decompiled code — only the mod's own code, which finds the game's classes by name at runtime. Use at your own risk.

**[Download the latest release](https://github.com/GDmio/granny-legacy-menu/releases/latest)**

---

## Features

### Visuals
- **Enemy ESP** — box (full / corners), name, distance, AI state (chasing, searching, frozen…), snaplines, arrows pointing to off-screen enemies.
- **Item ESP** — labels and distance, name filter, range limit.
- **Chams** for enemies and items: outline / silhouette / both (using the game's own `Outline` component) and materials — flat, two-tone (visible / behind walls), behind-walls only, ghost, glow, neon (bloom), chrome. Rainbow and pulse modifiers. Occlusion culling is disabled so highlights don't vanish behind walls.
- **Mini radar.**

### Player
- **Godmode** — enemies can't start a catch (Granny, Grandpa, Mother Spider), hits and traps are cancelled, stuck states are reset automatically.
- **Invisibility** — Granny, Grandpa and the Mother Spider can't see the player.
- **Speedhack**, **noclip** (fly through walls), custom **FOV**, save point and teleport.

### Weapons
- Spawn the **crossbow, double-barrel shotgun, shotgun, pepper spray, stun baton and traps**.
- **Infinite ammo**, **rapid fire**, charged darts.
- **Aimbot**: smooth camera aim on a key and **silent aim** (shots and darts hit the target without turning the camera), aim point (head / chest / centre), aim FOV, FOV circle.

### Items
- Spawn **any item** in the game — the list comes straight from the game's inventory.
- **Auto-use** — if a lock needs a key or tool you're not holding, the mod uses the right item for you and gives your previous item back.
- List of items on the map: **teleport to an item** or **pull it to you**.

### Enemies
- Freeze everyone or one at a time, speed multiplier.
- Per enemy: knock out, pepper, ice, chase / stop, teleport to you or to a saved point, "kill" (Granny / Grandpa).

### World
- Fullbright, no fog, game speed.

### Interface
- **Material You**: the palette is generated from a single colour (OKLCH), light and dark themes with a smooth transition.
- **Material 3 Expressive motion**: overshooting springs, a gliding navigation indicator, ripples, buttons that squish when pressed, squash & stretch switches, staggered card entrances, a morphing loading indicator, wavy progress in notifications.
- **Window material**: none / glass / frosted / acrylic — the background blur is computed on the GPU; transparency and blur strength are adjustable.
- **Quick access (FAB)** — a button with your chosen features, plus **hotkeys for any feature**.
- **English and Russian** interface (picked from the Windows language on first start, switchable in Settings → Language).
- Config save / load (`granny_menu.ini`).

---

## Requirements

- Windows 10 / 11 x64
- Granny Legacy (Steam version)
- To build: **Visual Studio 2022 or newer** with the "Desktop development with C++" workload

## Building

```bash
git clone --recursive https://github.com/GDmio/granny-legacy-menu.git
cd granny-legacy-menu
build.bat
```

If you cloned without `--recursive`:

```bash
git submodule update --init
```

Output: `bin\GrannyLegacyMenu.dll` and `bin\injector.exe`.

## Running

1. Start the game.
2. Run `inject.bat` (it can also be started before the game — it waits for the game process).
3. In game: **Insert** — menu, **End** — unload the mod.

`inject.bat` injects a copy of the DLL from `bin\loaded\`, so you can rebuild without closing the game: running `inject.bat` again unloads the old version and loads the new one.

## Hotkeys

| Key | Action |
|---|---|
| Insert | open / close the menu |
| End | unload the mod |
| Right mouse button (default) | aimbot camera aim |
| any | toggle features — assign them in Settings → Quick access |

---

## How it works

- **No hard-coded addresses.** Every game class, field and method is looked up by name at runtime through the `il2cpp_*` exports (`il2cpp_class_from_name`, `il2cpp_class_get_method_from_name`, `il2cpp_field_get_offset`, `il2cpp_resolve_icall`…). Unity methods removed by code stripping are called directly through icalls in `UnityPlayer.dll`.
- **Hooks** — MinHook on game methods (`MobileFPS.Update`, `PickRay.Update`, `AI_Granny.SensorPlayer`, `PlayerStatus.*Death`…) and on `IDXGISwapChain::Present` / `ResizeBuffers`.
- **Threads.** All Unity work runs on the game's main thread (inside `MobileFPS.Update`); the render thread only draws a data snapshot and queues commands.
- **Interface** — Dear ImGui with custom Material 3 widgets (`widgets.cpp`), spring physics and shapes (`motion.cpp`), palette generation (`theme.cpp`), D3D11 shader blur (`blur.cpp`) and translations (`i18n.cpp`).

```
src/
  dllmain.cpp     entry point, hot reload, unloading
  il2cpp.*        IL2CPP API access
  unity.*         Unity method wrappers
  game.*          hooks and every game feature
  state.*         data snapshot and command queue between threads
  render.*        D3D11 / ImGui / WndProc, hotkeys
  menu.*          menu pages, FAB, notifications
  widgets.*       Material 3 widgets
  motion.*        springs, ripples, shape morphing
  theme.*         Material You palette and fonts
  blur.*          GPU blur for the glass effects
  esp.*           ESP, radar, FOV circle
  toggles.*       feature registry for quick access and hotkeys
  i18n.*          English / Russian translations
  config.*        settings persistence
injector/         simple LoadLibrary injector
third_party/      Dear ImGui, MinHook (git submodules)
```

## Troubleshooting

The mod writes a log to `bin\granny_menu.log` showing which classes, fields and hooks were found. After a game update names may change: `not found` lines in the log tell you exactly what.

## Credits

- [Dear ImGui](https://github.com/ocornut/imgui) — MIT
- [MinHook](https://github.com/TsudaKageyu/minhook) — BSD 2-Clause
- [Il2CppDumper](https://github.com/Perfare/Il2CppDumper) — used while researching the game
- [Material Design 3](https://m3.material.io/) — design system

## License

[MIT](LICENSE)
