#pragma once

namespace menu {

void LoadFonts();
void ApplyTheme();
void Draw();          // main window (only while open)
void DrawToasts();    // snackbars (always)
bool IsCapturingKey();

} // namespace menu
