#include <windows.h>
#include <MinHook.h>
#include "hooks.h"
#include "renderer.h"
#include "globals.h"

static DWORD WINAPI UnloadThread(LPVOID)
{
    while (!g_ShouldUnload.load()) {
        Sleep(50);
    }

    Sleep(100);

    CleanupAll();
    UnhookAll();
    MH_Uninitialize();

    if (g_hModule) {
        FreeLibraryAndExitThread(g_hModule, 0);
    }

    return 0;
}

static DWORD WINAPI HookThread(LPVOID)
{
    if (MH_Initialize() != MH_OK) {
        MessageBoxW(nullptr, L"MinHook init failed.", L"Error", MB_OK);
        return 0;
    }

    if (!GetModuleHandleW(L"d3d11.dll")) {
        MessageBoxW(nullptr, L"D3D11.dll not found.", L"Error", MB_OK);
        MH_Uninitialize();
        return 0;
    }

    void* present = GetPresentAddress();
    if (!present) {
        MessageBoxW(nullptr, L"Failed to resolve Present.", L"Error", MB_OK);
        MH_Uninitialize();
        return 0;
    }

    RegisterHookOwner();

    if (!InstallPresentHook(present)) {
        ReleaseHookOwner();
        MessageBoxW(nullptr, L"Failed to install hook.", L"Error", MB_OK);
        MH_Uninitialize();
        return 0;
    }

    CreateThread(nullptr, 0, UnloadThread, nullptr, 0, nullptr);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_hModule = hModule;
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, HookThread, nullptr, 0, nullptr);
    }

    return TRUE;
}
