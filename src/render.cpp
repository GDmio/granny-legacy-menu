#include "render.h"
#include "config.h"
#include "blur.h"
#include "esp.h"
#include "game.h"
#include "log.h"
#include "menu.h"
#include "state.h"
#include "toggles.h"
#include <string>
#include <MinHook.h>
#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include "imgui.h"
#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace render {

std::atomic<bool> g_unloadRequested{ false };

namespace {

using Present_t = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT);
using ResizeBuffers_t = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

Present_t oPresent = nullptr;
ResizeBuffers_t oResizeBuffers = nullptr;
void* g_presentTarget = nullptr;
void* g_resizeTarget = nullptr;

HWND g_hwnd = nullptr;
WNDPROC g_origWndProc = nullptr;
ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
ID3D11RenderTargetView* g_rtv = nullptr;
bool g_imguiReady = false;
bool g_keyState[256]{};

bool KeyPressed(int vk) {
    if (vk <= 0 || vk > 255) return false;
    const bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
    const bool pressed = down && !g_keyState[vk];
    g_keyState[vk] = down;
    return pressed;
}

void CreateRTV(IDXGISwapChain* sc) {
    ID3D11Texture2D* back = nullptr;
    if (SUCCEEDED(sc->GetBuffer(0, IID_PPV_ARGS(&back))) && back) {
        D3D11_TEXTURE2D_DESC td{};
        back->GetDesc(&td);
        D3D11_RENDER_TARGET_VIEW_DESC rd{};
        rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        // Typeless back buffers need an explicit view format
        switch (td.Format) {
        case DXGI_FORMAT_R8G8B8A8_TYPELESS: rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM; break;
        case DXGI_FORMAT_B8G8R8A8_TYPELESS: rd.Format = DXGI_FORMAT_B8G8R8A8_UNORM; break;
        case DXGI_FORMAT_R10G10B10A2_TYPELESS: rd.Format = DXGI_FORMAT_R10G10B10A2_UNORM; break;
        case DXGI_FORMAT_R16G16B16A16_TYPELESS: rd.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; break;
        default: rd.Format = td.Format; break;
        }
        if (FAILED(g_device->CreateRenderTargetView(back, &rd, &g_rtv))) g_device->CreateRenderTargetView(back, nullptr, &g_rtv);
        back->Release();
    }
}

bool IsInputMessage(UINT msg) {
    return (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) || (msg >= WM_KEYFIRST && msg <= WM_KEYLAST) || msg == WM_INPUT;
}

LRESULT CALLBACK hkWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (g_imguiReady && game::g_menuOpen) {
        ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp);
        if (IsInputMessage(msg)) {
            if (msg == WM_INPUT) return DefWindowProcW(hwnd, msg, wp, lp);  // let Windows clean up raw input
            return 0;
        }
        if (msg == WM_SETCURSOR && LOWORD(lp) == HTCLIENT) {
            SetCursor(nullptr);  // ImGui draws the pointer itself; hide the OS one to avoid a double cursor
            return TRUE;
        }
    }
    return CallWindowProcW(g_origWndProc, hwnd, msg, wp, lp);
}

bool InitImGui(IDXGISwapChain* sc) {
    if (FAILED(sc->GetDevice(IID_PPV_ARGS(&g_device)))) return false;
    g_device->GetImmediateContext(&g_context);
    DXGI_SWAP_CHAIN_DESC desc{};
    sc->GetDesc(&desc);
    g_hwnd = desc.OutputWindow;

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    menu::LoadFonts();
    menu::ApplyTheme();
    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX11_Init(g_device, g_context);
    blur::Init(g_device);
    g_origWndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(hkWndProc)));
    LOG("ImGui initialised, hwnd=%p backbuffer=%ux%u fmt=%d", g_hwnd, desc.BufferDesc.Width, desc.BufferDesc.Height,
        desc.BufferDesc.Format);
    return true;
}

void HandleHotkeys() {
    const bool focused = GetForegroundWindow() == g_hwnd;
    // Always poll so edge detection stays consistent
    const bool menuKey = KeyPressed(g_cfg.keyMenu);
    const bool unloadKey = KeyPressed(g_cfg.keyUnload);
    bool toggled[kMaxToggles]{};
    for (int i = 0; i < ToggleCount(); ++i) toggled[i] = KeyPressed(g_cfg.toggleKeys[i]);
    if (!focused || menu::IsCapturingKey()) return;
    if (menuKey) game::g_menuOpen = !game::g_menuOpen;
    if (unloadKey) g_unloadRequested = true;
    for (int i = 0; i < ToggleCount(); ++i) {
        if (!toggled[i]) continue;
        const ToggleDef& t = Toggles()[i];
        *t.value = !*t.value;
        state::Notify(std::string(t.label) + (*t.value ? u8": вкл" : u8": выкл"));
    }
}

HRESULT WINAPI hkPresent(IDXGISwapChain* sc, UINT sync, UINT flags) {
    static bool failed = false;
    if (!g_imguiReady && !failed) {
        g_imguiReady = InitImGui(sc);
        failed = !g_imguiReady;
    }
    if (g_imguiReady && !g_unloadRequested) {
        if (!g_rtv) CreateRTV(sc);
        HandleHotkeys();

        ImGui::GetIO().MouseDrawCursor = game::g_menuOpen;  // the game hides the OS cursor, so draw our own
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        esp::Draw();
        menu::Draw();  // animates its own open / close
        menu::DrawToasts();
        ImGui::Render();
        // glass effects sample a blurred copy of this game frame (taken before the overlay is drawn)
        const float grain = g_cfg.glassMode == 2 ? 0.045f : g_cfg.glassMode == 3 ? 0.025f : 0.f;
        blur::Process(g_context, sc, g_cfg.blurStrength, grain);
        if (g_rtv) {
            g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        }
    }
    return oPresent(sc, sync, flags);
}

HRESULT WINAPI hkResizeBuffers(IDXGISwapChain* sc, UINT count, UINT w, UINT h, DXGI_FORMAT fmt, UINT flags) {
    if (g_rtv) {
        g_rtv->Release();
        g_rtv = nullptr;
    }
    blur::Invalidate();
    return oResizeBuffers(sc, count, w, h, fmt, flags);
}

bool FindSwapChainVtable(void**& vtable) {
    WNDCLASSEXW wc{ sizeof(wc), CS_CLASSDC, DefWindowProcW, 0, 0, GetModuleHandleW(nullptr), nullptr, nullptr, nullptr, nullptr,
                    L"GLMDummyWnd", nullptr };
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, nullptr, nullptr, wc.hInstance, nullptr);

    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 1;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    IDXGISwapChain* sc = nullptr;
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    D3D_FEATURE_LEVEL fl;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &sd,
                                               &sc, &dev, &fl, &ctx);
    if (FAILED(hr))
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &sd, &sc,
                                           &dev, &fl, &ctx);
    bool ok = SUCCEEDED(hr) && sc;
    if (ok) {
        void** vt = *reinterpret_cast<void***>(sc);
        g_presentTarget = vt[8];
        g_resizeTarget = vt[13];
        vtable = vt;
    }
    if (sc) sc->Release();
    if (ctx) ctx->Release();
    if (dev) dev->Release();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    LOG("dummy swapchain hr=0x%08X Present=%p ResizeBuffers=%p", static_cast<unsigned>(hr), g_presentTarget, g_resizeTarget);
    return ok;
}

} // namespace

bool Init() {
    void** vt = nullptr;
    if (!FindSwapChainVtable(vt)) return false;
    if (MH_CreateHook(g_presentTarget, reinterpret_cast<void*>(&hkPresent), reinterpret_cast<void**>(&oPresent)) != MH_OK) return false;
    if (MH_CreateHook(g_resizeTarget, reinterpret_cast<void*>(&hkResizeBuffers), reinterpret_cast<void**>(&oResizeBuffers)) != MH_OK)
        return false;
    return MH_EnableHook(g_presentTarget) == MH_OK && MH_EnableHook(g_resizeTarget) == MH_OK;
}

void Shutdown() {
    if (g_origWndProc && g_hwnd) SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_origWndProc));
    Sleep(200);  // the window thread may still be inside hkWndProc / ImGui's handler
    if (g_imguiReady) {
        blur::Shutdown();
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        g_imguiReady = false;
    }
    if (g_rtv) g_rtv->Release();
    if (g_context) g_context->Release();
    if (g_device) g_device->Release();
    g_rtv = nullptr;
    g_context = nullptr;
    g_device = nullptr;
}

} // namespace render
