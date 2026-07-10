#include "hooks.h"
#include "features.h"
#include "renderer.h"
#include "globals.h"
#include <windows.h>
#include <MinHook.h>
#ifdef MH_Uninitialize
#undef MH_Uninitialize
#endif

static HANDLE g_EspThread = nullptr;

static DWORD WINAPI EspUpdateThread(LPVOID)
{
    // Wait for client.dll to load (it may not be loaded yet when we're injected)
    HMODULE clientDll = nullptr;
    for (int i = 0; i < 100 && !clientDll; i++)
    {
        clientDll = GetModuleHandleA("client.dll");
        if (!clientDll) Sleep(100);
    }
    if (!clientDll) return 0; // client.dll never loaded, nothing to do

    while (!g_BhopShouldExit.load())
    {
        if (g_EspEnabled.load() && g_LastWidth > 0 && g_LastHeight > 0)
        {
            // ESP is on and we know the screen size → update the render buffer
            UpdateEspData((float)g_LastWidth, (float)g_LastHeight);
            Sleep(1);  // ~1000 updates/sec max, plenty for smooth ESP
        }
        else
        {
            Sleep(10); // ESP is off or screen size unknown → chill
        }
    }
    return 0;
}

static DWORD WINAPI UnloadThread(LPVOID)
{
    Sleep(500); // brief delay so the last frame can render

    g_BhopShouldExit.store(true);
    if (g_EspThread)
    {
        WaitForSingleObject(g_EspThread, 1000); // wait up to 1 second
        CloseHandle(g_EspThread);
        g_EspThread = nullptr;
    }

    Sleep(100); // let any pending operations finish
    CleanupAll();   // release D3D11 / ImGui
    UnhookAll();    // remove all MinHook hooks
    MH_Uninitialize(); // shut down MinHook library

    // Free ourselves and exit cleanly
    if (g_hModule) FreeLibraryAndExitThread(g_hModule, 0);

    return 0;
}

static DWORD WINAPI HookThread(LPVOID)
{
    // Safety check: don't install hooks in the wrong process
    if (!IsTargetProcess()) return 0;

    // Initialize MinHook library (required before any hook creation)
    if (MH_Initialize() != MH_OK) return 0;

    // Wait for d3d11.dll to be loaded by the game engine
    for (int i = 0; i < 100 && !GetModuleHandleW(L"d3d11.dll"); i++)
        Sleep(50);
    if (!GetModuleHandleW(L"d3d11.dll"))
    {
        MH_Uninitialize();
        return 0;
    }

    // Find the Present function address and hook it
    void* presentAddr = GetPresentAddress();
    if (!presentAddr || !InstallPresentHook(presentAddr))
    {
        MH_Uninitialize();
        return 0;
    }

    // Wait for client.dll to load (contains game logic and entity data)
    HMODULE clientDll = nullptr;
    for (int i = 0; i < 100 && !clientDll; i++)
    {
        clientDll = GetModuleHandleA("client.dll");
        if (!clientDll) Sleep(100);
    }

    if (clientDll)
    {
        InstallCreateMovePatternHook(clientDll); // hook CreateMove for BunnyHop
    }

    // Main loop: wait until the user presses END to unload
    while (!g_ShouldUnload.load())
        Sleep(100);

    // Spawn the unload thread to clean up everything
    CreateThread(nullptr, 0, UnloadThread, nullptr, 0, nullptr);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_hModule = hModule;
        DisableThreadLibraryCalls(hModule); // we don't need thread notifications
        CreateThread(nullptr, 0, HookThread, nullptr, 0, nullptr);     // main hook setup
        g_EspThread = CreateThread(nullptr, 0, EspUpdateThread, nullptr, 0, nullptr); // ESP updater
    }
    return TRUE;
}