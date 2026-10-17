#include "log.h"
#include <Windows.h>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <share.h>

namespace {
FILE* g_file = nullptr;
std::mutex g_mutex;
}

void LogInit(const std::wstring& path) {
    std::lock_guard lock(g_mutex);
    if (!g_file) g_file = _wfsopen(path.c_str(), L"w", _SH_DENYNO);  // readable while the game runs
}

void LogWrite(const char* fmt, ...) {
    std::lock_guard lock(g_mutex);
    if (!g_file) return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    std::fprintf(g_file, "[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_list args;
    va_start(args, fmt);
    std::vfprintf(g_file, fmt, args);
    va_end(args);
    std::fputc('\n', g_file);
    std::fflush(g_file);
}
