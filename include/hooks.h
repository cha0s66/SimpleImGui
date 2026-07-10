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
