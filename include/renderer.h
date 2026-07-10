// ============================================================================
// renderer.h  -  D3D11 Render Target Helpers
// ============================================================================
// Minimal helpers for managing the backbuffer render target view (RTV) that
// ImGui draws into. The RTV must be recreated when the window resizes.
// ============================================================================
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>

// Create a RenderTargetView from the swapchain's backbuffer.
// Called once at init and again after every window resize.
bool CreateRenderTarget(IDXGISwapChain* pSwapChain);

// Release the current RTV so it can be recreated.
void CleanupRenderTarget();

// Hook the game's D3D11 swapchain creation so we can attach to the real Present callback.
bool InstallD3D11CreateDeviceAndSwapChainHook();