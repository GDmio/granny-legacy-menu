#pragma once
#include <string>

void LogInit(const std::wstring& path);
void LogWrite(const char* fmt, ...);

#define LOG(...) LogWrite(__VA_ARGS__)
