#pragma once

#include <windows.h>
#include <d3d11.h>
#include <atomic>

inline constexpr int VTABLE_PRESENT_INDEX = 8;

extern std::atomic<bool> g_ImGuiInitialized;
extern std::atomic<bool> g_ShouldUnload;

extern bool g_MenuOpen;
extern bool g_InsertDown;
extern bool g_EndDown;

extern void* g_HookedPresentPtr;
extern bool g_PresentHookInstalled;

extern UINT g_LastWidth;
extern UINT g_LastHeight;

extern HWND g_hWindow;
extern ID3D11Device* g_pd3dDevice;
extern ID3D11DeviceContext* g_pd3dDeviceContext;
extern ID3D11RenderTargetView* g_pMainRenderTargetView;
extern WNDPROC g_OriginalWndProc;
extern HMODULE g_hModule;