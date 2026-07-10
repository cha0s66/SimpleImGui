#pragma once

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

LRESULT WINAPI HookedWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

void* GetPresentAddress();
bool InstallPresentHook(void* presentPtr);

void RegisterHookOwner();
void ReleaseHookOwner();
bool IsHookOwner();
void PrepareHookInstall(void* presentPtr);

HRESULT __stdcall hkPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);

void UnhookAll();
void CleanupAll();

// ============================================================================
// hooks.h  -  All Hook Functions & Installation Helpers
// ============================================================================
// This module declares every function we hook into the game and the helpers
// that install/remove those hooks. Split by subsystem:
//  - WndProc (input window messages)
//  - CreateMove (game input processing)
//  - Win32 API (cursor, SetCursorPos, etc.)
//  - D3D11 Present (render hook for ImGui overlay)
// ============================================================================

#pragma once

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include "sdk.h"

// ============================================================================
// Window Proc Hook
// ============================================================================
// Check if we're loaded into cs2.exe (safety check before installing hooks).
bool IsTargetProcess();

// Intercept window messages so ImGui can receive clicks/keystrokes while
// the menu is open. Passes through to the original WndProc for everything else.
LRESULT WINAPI HookedWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ============================================================================
// CreateMove Hook
// ============================================================================
// Our replacement for the engine's CreateMove function. This is where we get
// the CUserCmd pointer and can modify buttons / viewangles before the game
// processes them. The wrapper calls the original first, then runs our logic.
void __fastcall hkCreateMoveWrapper(void* a1, __int64 a2, CUserCmd* a3);

// Try to find CreateMove via pattern scanning and install a MinHook.
bool InstallCreateMovePatternHook(HMODULE clientDll);

// Safely disable and remove the CreateMove hook.
void UninstallCreateMoveHook();

// ============================================================================
// Win32 API Hooks (cursor manipulation for menu support)
// ============================================================================
BOOL WINAPI hkSetCursorPos(int X, int Y);
HCURSOR WINAPI hkSetCursor(HCURSOR hCursor);
int WINAPI hkShowCursor(BOOL bShow);
BOOL WINAPI hkClipCursor(const RECT* lpRect);

// ============================================================================
// D3D11 / Present Hook
// ============================================================================
// Create a dummy window + D3D11 swapchain so we can read the Present vtable
// address without touching the game's actual swapchain.
void* GetPresentAddress();

// Install the Present hook using MinHook.
bool InstallPresentHook(void* presentPtr);

// Our Present hook. Handles ImGui initialization, menu rendering, and ESP drawing.
HRESULT __stdcall hkPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);

// ============================================================================
// Cleanup
// ============================================================================
// Disable all hooks (Present, CreateMove, Win32 API) and restore originals.
void UnhookAll();

// Release all D3D11 objects, shut down ImGui, restore the original WndProc.
void CleanupAll();
