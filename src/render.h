#pragma once
#include <atomic>

namespace render {

bool Init();        // hook IDXGISwapChain::Present / ResizeBuffers
void Shutdown();    // call after hooks are disabled

extern std::atomic<bool> g_unloadRequested;

} // namespace render
