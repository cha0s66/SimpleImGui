// ============================================================================
// hooks.cpp  —  Hook Functions & Installation
// ============================================================================
// Four subsystems:
//  1. WndProc    — intercepts window messages so ImGui can receive input
//  2. CreateMove — intercepts game input so we can modify CUserCmd
//  3. Win32 API  — intercepts cursor functions so the menu cursor stays visible
//  4. Cleanup    — safely uninstalls everything on unload
// ============================================================================

#include "hooks.h"
#include "input.h"
#include "memory.h"
#include "features.h"
#include "renderer.h"
#include "globals.h"
#include <MinHook.h>
#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_win32.h>
#include <imgui/backends/imgui_impl_dx11.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

// ============================================================================
// Window Proc Hook
// ============================================================================

bool IsTargetProcess()
{
    char path[MAX_PATH] = {};
    if (GetModuleFileNameA(NULL, path, MAX_PATH) == 0) return false;

    const char* exeName = strrchr(path, '\\');
    exeName = exeName ? exeName + 1 : path;
    return _stricmp(exeName, "cs2.exe") == 0;
}

LRESULT WINAPI HookedWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (!g_ImGuiInitialized.load())
        return CallWindowProcA(g_OriginalWndProc, hWnd, msg, wParam, lParam);

    if (!g_MenuOpen)
    {
        ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
        return CallWindowProcA(g_OriginalWndProc, hWnd, msg, wParam, lParam);
    }

    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return 0;

    // Swallow mouse/input messages so the game doesn't react while the menu is open
    switch (msg)
    {
        case WM_MOUSEMOVE:
        case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK:
        case WM_XBUTTONDOWN: case WM_XBUTTONUP: case WM_XBUTTONDBLCLK:
        case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL:
        case WM_MOUSEACTIVATE:
        case WM_SETCURSOR:
        case WM_INPUT:
            return 0;
    }

    return CallWindowProcA(g_OriginalWndProc, hWnd, msg, wParam, lParam);
}

// ============================================================================
// CreateMove Hook
// ============================================================================

static const char* CREATE_MOVE_PATTERNS[] = {
    "48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC 20 41 56 49 8B E8 48 8B F2",
    "48 8B C4 4C 89 40 ? 48 89 48 ? 55 53 41 54",
    "48 8B C4 4C 89 40 ? 48 89 48 ? 55 53 57",
    "85 D2 0F 85 ? ? ? ? 48 8B C4 44 88 40",
    "48 8B C4 48 89 58 10 48 89 48 08 55 56 57 41 54 41 55",
    nullptr
};

void __fastcall hkCreateMoveWrapper(void* a1, __int64 a2, CUserCmd* a3)
{
    // Validate BEFORE any modifications
    if (!a3 || !IsValidPtrFast(a3)) return;
    if (a3->command_number <= 0 || a3->command_number > 0x00FFFFFF) return;

    // Modify input BEFORE the game processes it (subtick precision)
    if (g_BhopEnabled.load())
        DoBhop(a3);

    if (g_AimbotEnabled.load())
        DoAimbot(a3);

    // if (g_TriggerbotEnabled.load())
    //     DoTriggerbot(a3);

    // Now call original with our modified cmd
    if (g_OriginalCreateMove)
    {
        using CreateMoveFn = void(__fastcall*)(void*, __int64, CUserCmd*);
        reinterpret_cast<CreateMoveFn>(g_OriginalCreateMove)(a1, a2, a3);
    }
}

bool InstallCreateMovePatternHook(HMODULE clientDll)
{
    if (!clientDll) return false;

    for (int p = 0; CREATE_MOVE_PATTERNS[p] != nullptr; p++)
    {
        auto matches = PatternScanAll("client.dll", CREATE_MOVE_PATTERNS[p]);

        for (size_t m = 0; m < matches.size(); m++)
        {
            void* createMoveAddr = reinterpret_cast<void*>(matches[m]);

            if (g_CreateMoveHookInstalled) return true;
            if (!IsValidPtrFast(createMoveAddr)) continue;

            MEMORY_BASIC_INFORMATION mbi;
            if (!VirtualQuery(createMoveAddr, &mbi, sizeof(mbi))) continue;
            if (!(mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE))) continue;

            if (MH_CreateHook(createMoveAddr, reinterpret_cast<LPVOID>(&hkCreateMoveWrapper),
                              reinterpret_cast<LPVOID*>(&g_OriginalCreateMove)) != MH_OK)
                continue;

            if (MH_EnableHook(createMoveAddr) != MH_OK)
            {
                MH_RemoveHook(createMoveAddr);
                continue;
            }

            g_HookedCreateMoveAddr = createMoveAddr;
            g_CreateMoveHookInstalled = true;
            return true;
        }
    }
    return false;
}

void UninstallCreateMoveHook()
{
    if (g_CreateMoveHookInstalled && g_HookedCreateMoveAddr)
    {
        MH_DisableHook(g_HookedCreateMoveAddr);
        MH_RemoveHook(g_HookedCreateMoveAddr);
        g_CreateMoveHookInstalled = false;
        g_OriginalCreateMove = nullptr;
        g_HookedCreateMoveAddr = nullptr;
    }
}

// ============================================================================
// Cleanup
// ============================================================================

void UnhookAll()
{
    if (g_PresentHookInstalled && g_HookedPresentPtr)
    {
        MH_DisableHook(g_HookedPresentPtr);
        MH_RemoveHook(g_HookedPresentPtr);
        g_PresentHookInstalled = false;
        g_HookedPresentPtr = nullptr;
    }

    UninstallCreateMoveHook();

    auto unhook = [](const char* name, auto& orig) {
        if (!orig) return;
        HMODULE user32 = GetModuleHandleA("user32.dll");
        if (!user32) return;
        void* ptr = reinterpret_cast<void*>(GetProcAddress(user32, name));
        if (ptr) { MH_DisableHook(ptr); MH_RemoveHook(ptr); }
        orig = nullptr;
    };
    unhook("SetCursorPos", g_oSetCursorPos);
    unhook("SetCursor",    g_oSetCursor);
    unhook("ShowCursor",   g_oShowCursor);
    unhook("ClipCursor",   g_oClipCursor);
}

void CleanupAll()
{
    if (g_hWindow && g_OriginalWndProc)
    {
        SetWindowLongPtrA(g_hWindow, GWLP_WNDPROC, (LONG_PTR)g_OriginalWndProc);
        g_OriginalWndProc = nullptr;
    }

    if (g_ImGuiInitialized.load())
    {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        g_ImGuiInitialized.store(false);
    }

    CleanupRenderTarget();
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
    g_hWindow = nullptr;
}