#pragma once
#include "imgui.h"

struct ID3D11Device;
struct ID3D11DeviceContext;
struct IDXGISwapChain;

// GPU background blur for the glass / frosted / acrylic effects.
// The game frame is copied before the overlay is drawn, downsampled, gaussian-blurred and
// composited (with optional grain) into a full-screen texture that ImGui samples behind surfaces.
namespace blur {

bool Init(ID3D11Device* device);
void Shutdown();
void Invalidate();  // swap chain resized

// Call after ImGui::Render() and before the draw data is rendered.
// strength 0..1 (radius / iterations), grain = noise amplitude (0 = none)
void Process(ID3D11DeviceContext* ctx, IDXGISwapChain* sc, float strength, float grain);

ImTextureID Texture();  // 0 until the first successful Process
void Request();         // UI asks for a blurred frame this frame (cheap when unused)
bool Requested();

} // namespace blur
