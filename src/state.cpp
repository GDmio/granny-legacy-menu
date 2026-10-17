#include "state.h"
#include <Windows.h>
#include <mutex>

namespace state {

namespace {
std::mutex g_snapMutex;
Snapshot g_snap;
std::mutex g_cmdMutex;
std::vector<std::function<void()>> g_cmds;
std::mutex g_toastMutex;
std::vector<Toast> g_toasts;
}

uint64_t NowMs() { return GetTickCount64(); }

void Publish(Snapshot&& snap) {
    std::lock_guard lock(g_snapMutex);
    g_snap = std::move(snap);
}

Snapshot Get() {
    std::lock_guard lock(g_snapMutex);
    return g_snap;
}

void Post(std::function<void()> cmd) {
    std::lock_guard lock(g_cmdMutex);
    g_cmds.push_back(std::move(cmd));
}

void RunCommands() {
    std::vector<std::function<void()>> cmds;
    {
        std::lock_guard lock(g_cmdMutex);
        cmds.swap(g_cmds);
    }
    for (auto& c : cmds) c();
}

void Notify(const std::string& message) {
    std::lock_guard lock(g_toastMutex);
    g_toasts.push_back({ message, NowMs() });
    if (g_toasts.size() > 4) g_toasts.erase(g_toasts.begin());
}

std::vector<Toast> Toasts() {
    std::lock_guard lock(g_toastMutex);
    const uint64_t now = NowMs();
    std::erase_if(g_toasts, [now](const Toast& t) { return now - t.timeMs > 3500; });
    return g_toasts;
}

} // namespace state
