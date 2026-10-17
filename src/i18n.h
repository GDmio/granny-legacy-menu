#pragma once

// UI language. Strings are written in Russian in the source; L() returns the English
// translation when English is selected (falls back to the original if a string is missing).
enum class Lang : int { Russian = 0, English = 1 };

const char* L(const char* ru);
int DefaultLanguage();  // from the Windows UI language
