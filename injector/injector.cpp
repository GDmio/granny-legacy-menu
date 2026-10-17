// Minimal LoadLibrary injector for "Granny Legacy.exe".
// Usage: injector.exe [path\to\GrannyLegacyMenu.dll]   (defaults to the DLL next to the injector)
#include <Windows.h>
#include <TlHelp32.h>
#include <cstdio>
#include <string>

static DWORD FindProcess(const wchar_t* name) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W pe{ sizeof(pe) };
    DWORD pid = 0;
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
        if (_wcsicmp(pe.szExeFile, name) == 0) {
            pid = pe.th32ProcessID;
            break;
        }
    }
    CloseHandle(snap);
    return pid;
}

int wmain(int argc, wchar_t** argv) {
    std::wstring dll;
    if (argc > 1) {
        dll = argv[1];
    } else {
        wchar_t self[MAX_PATH];
        GetModuleFileNameW(nullptr, self, MAX_PATH);
        dll = self;
        dll = dll.substr(0, dll.find_last_of(L"\\/") + 1) + L"GrannyLegacyMenu.dll";
    }
    wchar_t full[MAX_PATH];
    GetFullPathNameW(dll.c_str(), MAX_PATH, full, nullptr);
    if (GetFileAttributesW(full) == INVALID_FILE_ATTRIBUTES) {
        wprintf(L"DLL not found: %s\n", full);
        return 1;
    }

    wprintf(L"Waiting for Granny Legacy.exe ...\n");
    DWORD pid = 0;
    while (!(pid = FindProcess(L"Granny Legacy.exe"))) Sleep(500);
    Sleep(1500);  // give Unity a moment to load GameAssembly

    HANDLE proc = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE |
                                  PROCESS_VM_READ, FALSE, pid);
    if (!proc) {
        wprintf(L"OpenProcess failed (%lu). Run as the same user / as admin.\n", GetLastError());
        return 1;
    }
    const size_t bytes = (wcslen(full) + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(proc, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    WriteProcessMemory(proc, remote, full, bytes, nullptr);
    auto loadLib = reinterpret_cast<LPTHREAD_START_ROUTINE>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
    HANDLE th = CreateRemoteThread(proc, nullptr, 0, loadLib, remote, 0, nullptr);
    if (!th) {
        wprintf(L"CreateRemoteThread failed (%lu)\n", GetLastError());
        return 1;
    }
    WaitForSingleObject(th, 10000);
    DWORD code = 0;
    GetExitCodeThread(th, &code);
    VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
    CloseHandle(th);
    CloseHandle(proc);
    wprintf(code ? L"Injected (pid %lu). INSERT - menu, END - unload.\n" : L"LoadLibrary failed inside the game (pid %lu).\n", pid);
    return code ? 0 : 1;
}
