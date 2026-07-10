#include "globals.h"

std::atomic<bool> g_ImGuiInitialized{ false };
std::atomic<bool> g_ShouldUnload{ false };

bool g_MenuOpen = true;
bool g_InsertDown = false;
bool g_EndDown = false;

void* g_HookedPresentPtr = nullptr;
bool g_PresentHookInstalled = false;

UINT g_LastWidth = 0;
UINT g_LastHeight = 0;

HWND g_hWindow = nullptr;
ID3D11Device* g_pd3dDevice  = nullptr;
ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
ID3D11RenderTargetView* g_pMainRenderTargetView = nullptr;
WNDPROC g_OriginalWndProc = nullptr;
HMODULE g_hModule = nullptr;
