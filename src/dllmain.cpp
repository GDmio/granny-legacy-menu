#include "config.h"
#include "game.h"
#include "il2cpp.h"
#include "log.h"
#include "render.h"
#include <MinHook.h>
#include <Windows.h>
#include <string>

std::wstring g_configPath;

namespace {

HMODULE g_module = nullptr;
HANDLE g_instanceMutex = nullptr;   // owned by the active instance
HANDLE g_unloadEvent = nullptr;     // signalled by a newer instance to hot-reload

// Only one copy may hook the game: ask an older copy to unload and wait until it is gone
bool AcquireInstance() {
    const std::wstring pid = std::to_wstring(GetCurrentProcessId());
    g_instanceMutex = CreateMutexW(nullptr, FALSE, (L"Local\\GrannyLegacyMenu_" + pid).c_str());
    g_unloadEvent = CreateEventW(nullptr, TRUE, FALSE, (L"Local\\GrannyLegacyMenuUnload_" + pid).c_str());
    if (!g_instanceMutex || !g_unloadEvent) return false;
    DWORD r = WaitForSingleObject(g_instanceMutex, 0);
    if (r == WAIT_TIMEOUT) {
        SetEvent(g_unloadEvent);
        r = WaitForSingleObject(g_instanceMutex, 15000);
    }
    ResetEvent(g_unloadEvent);
    return r == WAIT_OBJECT_0 || r == WAIT_ABANDONED;
}

void ReleaseInstance() {
    if (g_instanceMutex) {
        ReleaseMutex(g_instanceMutex);
        CloseHandle(g_instanceMutex);
    }
    if (g_unloadEvent) CloseHandle(g_unloadEvent);
}

std::wstring ModuleDir() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(g_module, path, MAX_PATH);
    std::wstring dir(path);
    dir = dir.substr(0, dir.find_last_of(L"\\/") + 1);
    // inject.bat loads a copy from bin\loaded\ so the build output stays unlocked; keep log/config in bin\ though
    const std::wstring sub = L"\\loaded\\";
    if (dir.size() > sub.size() && _wcsicmp(dir.c_str() + dir.size() - sub.size(), sub.c_str()) == 0)
        dir.resize(dir.size() - sub.size() + 1);
    return dir;
}

bool WaitFor(bool (*cond)(), int timeoutMs) {
    for (int t = 0; t < timeoutMs; t += 250) {
        if (cond()) return true;
        Sleep(250);
    }
    return false;
}

void Unload() {
    LOG("Unload requested");
    game::g_menuOpen = false;
    game::RequestCleanup();
    // The main thread restores game state inside the next MobileFPS.Update (if a level is running)
    WaitFor([] { return game::CleanupDone(); }, 3000);
    MH_DisableHook(MH_ALL_HOOKS);
    Sleep(300);  // let any in-flight hooked call return
    render::Shutdown();
    MH_Uninitialize();
    LOG("Unloaded");
}

DWORD WINAPI MainThread(LPVOID) {
    if (!AcquireInstance()) {  // an older copy refused to unload: do nothing rather than double-hook
        ReleaseInstance();
        FreeLibraryAndExitThread(g_module, 0);
    }
    const std::wstring dir = ModuleDir();
    LogInit(dir + L"granny_menu.log");
    g_configPath = dir + L"granny_menu.ini";
    LOG("Granny Legacy Menu starting");
    if (LoadConfig(g_configPath)) LOG("config loaded");

    if (!WaitFor([] { return GetModuleHandleW(L"GameAssembly.dll") && GetModuleHandleW(L"UnityPlayer.dll"); }, 60000) ||
        !il2cpp::Init()) {
        LOG("GameAssembly / il2cpp API not available");
        ReleaseInstance();
        FreeLibraryAndExitThread(g_module, 0);
    }
    il2cpp::AttachThread();
    if (MH_Initialize() != MH_OK) {
        LOG("MH_Initialize failed");
        il2cpp::DetachThread();
        ReleaseInstance();
        FreeLibraryAndExitThread(g_module, 0);
    }

    bool ok = false;
    for (int attempt = 0; attempt < 30 && !ok; ++attempt) {  // metadata may still be loading if injected very early
        ok = game::Init();
        if (!ok) Sleep(1000);
    }
    if (!ok) {
        LOG("game::Init failed - wrong game version?");
        MH_Uninitialize();
        il2cpp::DetachThread();
        ReleaseInstance();
        FreeLibraryAndExitThread(g_module, 0);
    }
    game::InstallHooks();
    il2cpp::DetachThread();

    if (!render::Init()) LOG("render::Init failed - overlay unavailable");
    LOG("Ready. INSERT - menu, END - unload");

    while (!render::g_unloadRequested && WaitForSingleObject(g_unloadEvent, 100) == WAIT_TIMEOUT) {}
    render::g_unloadRequested = true;
    Unload();
    ReleaseInstance();
    FreeLibraryAndExitThread(g_module, 0);
}

} // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = module;
        DisableThreadLibraryCalls(module);
        if (HANDLE h = CreateThread(nullptr, 0, MainThread, nullptr, 0, nullptr)) CloseHandle(h);
    }
    return TRUE;
}
