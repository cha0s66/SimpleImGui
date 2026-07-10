#include "hooks.h"
#include "globals.h"
#include "renderer.h"
#include <MinHook.h>
#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_win32.h>
#include <imgui/backends/imgui_impl_dx11.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace {
struct SharedHookState {
    LONG instanceCounter;
    LONG ownerInstanceId;
    LONG activeInstances;
    LONG presentHookInstalled;
    ULONG_PTR hookedPresentPtr;
};

static HANDLE g_hSharedMap = nullptr;
static HANDLE g_hSharedMutex = nullptr;
static SharedHookState* g_pSharedState = nullptr;
static LONG g_InstanceId = 0;

void EnsureSharedState()
{
    if (g_pSharedState && g_hSharedMutex)
        return;

    g_hSharedMutex = CreateMutexW(nullptr, FALSE, L"Global\\TestItMenuHookMutex");
    g_hSharedMap = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(SharedHookState), L"Global\\TestItMenuHookState");
    if (!g_hSharedMap) {
        if (g_hSharedMutex) {
            CloseHandle(g_hSharedMutex);
            g_hSharedMutex = nullptr;
        }
        return;
    }

    g_pSharedState = static_cast<SharedHookState*>(MapViewOfFile(g_hSharedMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedHookState)));
    if (!g_pSharedState) {
        CloseHandle(g_hSharedMap);
        g_hSharedMap = nullptr;
        CloseHandle(g_hSharedMutex);
        g_hSharedMutex = nullptr;
        return;
    }

    if (WaitForSingleObject(g_hSharedMutex, 0) == WAIT_OBJECT_0) {
        if (g_pSharedState->instanceCounter == 0) {
            g_pSharedState->instanceCounter = 0;
            g_pSharedState->ownerInstanceId = 0;
            g_pSharedState->activeInstances = 0;
            g_pSharedState->presentHookInstalled = 0;
            g_pSharedState->hookedPresentPtr = 0;
        }
        ReleaseMutex(g_hSharedMutex);
    }
}
} // namespace

void RegisterHookOwner()
{
    EnsureSharedState();
    if (!g_hSharedMutex || !g_pSharedState)
        return;

    if (WaitForSingleObject(g_hSharedMutex, INFINITE) != WAIT_OBJECT_0)
        return;

    g_InstanceId = InterlockedIncrement(&g_pSharedState->instanceCounter);
    g_pSharedState->ownerInstanceId = g_InstanceId;
    if (g_pSharedState->activeInstances < 1)
        g_pSharedState->activeInstances = 1;
    else
        g_pSharedState->activeInstances++;

    ReleaseMutex(g_hSharedMutex);
}

void ReleaseHookOwner()
{
    if (!g_hSharedMutex || !g_pSharedState || g_InstanceId == 0)
        return;

    if (WaitForSingleObject(g_hSharedMutex, INFINITE) != WAIT_OBJECT_0)
        return;

    if (g_pSharedState->activeInstances > 0)
        g_pSharedState->activeInstances--;

    if (g_pSharedState->activeInstances <= 0) {
        g_pSharedState->ownerInstanceId = 0;
        g_pSharedState->activeInstances = 0;
    }

    ReleaseMutex(g_hSharedMutex);
    g_InstanceId = 0;
}

void PrepareHookInstall(void* presentPtr)
{
    EnsureSharedState();
    if (!g_hSharedMutex || !g_pSharedState || !presentPtr)
        return;

    if (WaitForSingleObject(g_hSharedMutex, INFINITE) != WAIT_OBJECT_0)
        return;

    if (g_pSharedState->presentHookInstalled && g_pSharedState->hookedPresentPtr) {
        MH_DisableHook(reinterpret_cast<void*>(g_pSharedState->hookedPresentPtr));
        MH_RemoveHook(reinterpret_cast<void*>(g_pSharedState->hookedPresentPtr));
    }

    g_pSharedState->presentHookInstalled = 0;
    g_pSharedState->hookedPresentPtr = reinterpret_cast<ULONG_PTR>(presentPtr);
    ReleaseMutex(g_hSharedMutex);
}

bool IsHookOwner()
{
    if (!g_hSharedMutex || !g_pSharedState || g_InstanceId == 0)
        return true;

    if (WaitForSingleObject(g_hSharedMutex, INFINITE) != WAIT_OBJECT_0)
        return true;

    const bool isOwner = (g_pSharedState->ownerInstanceId == g_InstanceId);
    ReleaseMutex(g_hSharedMutex);
    return isOwner;
}

LRESULT WINAPI HookedWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (!g_ImGuiInitialized.load())
        return CallWindowProcA(g_OriginalWndProc, hWnd, msg, wParam, lParam);

    if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) {
        if (wParam == VK_INSERT) {
            if (!g_InsertDown)
                g_MenuOpen = !g_MenuOpen;
            g_InsertDown = true;
            return 0;
        }

        if (wParam == VK_END) {
            if (!g_EndDown)
                g_ShouldUnload.store(true);
            g_EndDown = true;
            return 0;
        }
    } else if (msg == WM_KEYUP || msg == WM_SYSKEYUP) {
        if (wParam == VK_INSERT) {
            g_InsertDown = false;
            return 0;
        }

        if (wParam == VK_END) {
            g_EndDown = false;
            return 0;
        }
    }

    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return 0;

    if (g_MenuOpen) {
        switch (msg) {
            case WM_MOUSEMOVE:
            case WM_LBUTTONDOWN:
            case WM_LBUTTONUP:
            case WM_LBUTTONDBLCLK:
            case WM_RBUTTONDOWN:
            case WM_RBUTTONUP:
            case WM_RBUTTONDBLCLK:
            case WM_MBUTTONDOWN:
            case WM_MBUTTONUP:
            case WM_MBUTTONDBLCLK:
            case WM_XBUTTONDOWN:
            case WM_XBUTTONUP:
            case WM_XBUTTONDBLCLK:
            case WM_MOUSEWHEEL:
            case WM_MOUSEHWHEEL:
            case WM_MOUSEACTIVATE:
            case WM_SETCURSOR:
            case WM_INPUT:
                return 0;
        }
    }

    return CallWindowProcA(g_OriginalWndProc, hWnd, msg, wParam, lParam);
}

void UnhookAll()
{
    if (g_PresentHookInstalled && g_HookedPresentPtr && IsHookOwner()) {
        MH_DisableHook(g_HookedPresentPtr);
        MH_RemoveHook(g_HookedPresentPtr);
        g_PresentHookInstalled = false;
        g_HookedPresentPtr = nullptr;
    }

    if (g_hSharedMutex && g_pSharedState) {
        if (WaitForSingleObject(g_hSharedMutex, INFINITE) == WAIT_OBJECT_0) {
            if (g_pSharedState->hookedPresentPtr == reinterpret_cast<ULONG_PTR>(g_HookedPresentPtr)) {
                g_pSharedState->presentHookInstalled = 0;
                g_pSharedState->hookedPresentPtr = 0;
            }
            ReleaseMutex(g_hSharedMutex);
        }
    }
}

void CleanupAll()
{
    if (g_hWindow && g_OriginalWndProc && IsHookOwner()) {
        SetWindowLongPtrA(g_hWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_OriginalWndProc));
        g_OriginalWndProc = nullptr;
    }

    if (g_ImGuiInitialized.load()) {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        g_ImGuiInitialized.store(false);
    }

    CleanupRenderTarget();
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
    g_hWindow = nullptr;

    ReleaseHookOwner();
}
