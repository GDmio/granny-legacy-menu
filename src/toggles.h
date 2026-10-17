#pragma once

// Registry of on/off features shared by the quick-access panel (FAB), hotkeys and the settings page.
struct ToggleDef {
    const char* id;      // stable config key
    const char* label;
    const char* icon;
    bool* value;
};

constexpr int kMaxToggles = 24;

const ToggleDef* Toggles();
int ToggleCount();
