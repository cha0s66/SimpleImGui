#pragma once

// ============================================================================
// globals.h  -  Shared Global State (declarations)
// ============================================================================
// All shared mutable state lives here. Every module includes this header
// and gets `extern` declarations. The actual storage is in globals.cpp.
//
// Rule of thumb: if two or more .cpp files need the same variable, put it here.
// This prevents "multiple definition" linker errors while keeping code readable.
// ============================================================================

#include <windows.h>
#include <d3d11.h>
#include <atomic>
#include <vector>
#include "sdk.h"
#include "offsets/offsets.hpp"
#include "offsets/client_dll.hpp"

// ============================================================================
// CS2 Offset Aliases
// ============================================================================
// These pull the raw offsets from cs2-dumper and give them readable names.
// Using a namespace keeps them organized and prevents naming conflicts.
// ============================================================================
namespace cs2_offsets {
    static constexpr std::uintptr_t dwEntityList           = cs2_dumper::offsets::client_dll::dwEntityList;
    static constexpr std::uintptr_t dwLocalPlayerPawn      = cs2_dumper::offsets::client_dll::dwLocalPlayerPawn;
    static constexpr std::uintptr_t dwViewMatrix           = cs2_dumper::offsets::client_dll::dwViewMatrix;
    static constexpr std::uintptr_t dwViewAngles           = cs2_dumper::offsets::client_dll::dwViewAngles;
    static constexpr std::uintptr_t m_iHealth            = cs2_dumper::schemas::client_dll::C_BaseEntity::m_iHealth;
    static constexpr std::uintptr_t m_iMaxHealth           = cs2_dumper::schemas::client_dll::C_BaseEntity::m_iMaxHealth;
    static constexpr std::uintptr_t m_iTeamNum           = cs2_dumper::schemas::client_dll::C_BaseEntity::m_iTeamNum;
    static constexpr std::uintptr_t m_lifeState            = cs2_dumper::schemas::client_dll::C_BaseEntity::m_lifeState;
    static constexpr std::uintptr_t m_pGameSceneNode       = cs2_dumper::schemas::client_dll::C_BaseEntity::m_pGameSceneNode;
    static constexpr std::uintptr_t m_hPlayerPawn          = cs2_dumper::schemas::client_dll::CCSPlayerController::m_hPlayerPawn;
    static constexpr std::uintptr_t m_sSanitizedPlayerName = cs2_dumper::schemas::client_dll::CCSPlayerController::m_sSanitizedPlayerName;
    static constexpr std::uintptr_t m_vOldOrigin           = cs2_dumper::schemas::client_dll::C_BasePlayerPawn::m_vOldOrigin;
    static constexpr std::uintptr_t m_fFlags               = cs2_dumper::schemas::client_dll::C_BaseEntity::m_fFlags;
    static constexpr std::uintptr_t m_vecAbsVelocity       = cs2_dumper::schemas::client_dll::C_BaseEntity::m_vecAbsVelocity;
    static constexpr std::uintptr_t m_hGroundEntity        = cs2_dumper::schemas::client_dll::C_BaseEntity::m_hGroundEntity;
    static constexpr std::uintptr_t m_MoveType             = cs2_dumper::schemas::client_dll::C_BaseEntity::m_MoveType;
    static constexpr std::uintptr_t m_flWaterLevel         = cs2_dumper::schemas::client_dll::C_BaseEntity::m_flWaterLevel;
}

// ============================================================================
// Core Feature Toggles
// ============================================================================
// Atomic bools so multiple threads can safely read/write feature states
// without data races. `std::atomic::load()` / `store()` are used everywhere.
// ============================================================================
extern std::atomic<bool> g_ImGuiInitialized;   // true once ImGui is ready to draw
extern std::atomic<bool> g_BhopEnabled;         // BunnyHop toggle
extern bool g_BhopLastJumpState;                // tracks last jump state for subtick transition signaling
extern std::atomic<bool> g_BhopShouldExit;      // signal ESP/Bhop threads to stop
extern std::atomic<bool> g_ShouldUnload;        // signal full cheat unload

// ============================================================================
// Menu State
// ============================================================================
// Non-atomic because only the Present hook (main render thread) touches these.
// ============================================================================
extern bool g_MenuOpen;      // true = menu visible, false = hidden
extern bool g_InsertDown;      // prevents Insert key repeat-fire
extern bool g_EndDown;         // prevents End key repeat-fire

// ============================================================================
// Present Hook State
// ============================================================================
extern void* g_HookedPresentPtr;     // address of the hooked Present function
extern bool  g_PresentHookInstalled; // true if Present hook is active

// ============================================================================
// Mouse State (for custom cursor when menu is open)
// ============================================================================
extern POINT g_LastMousePos;       // last known mouse position
extern bool  g_LastMouseDown[5];   // track mouse button changes

// ============================================================================
// Screen Dimensions (used by ESP for world-to-screen scaling)
// ============================================================================
extern UINT g_LastWidth;   // current backbuffer width
extern UINT g_LastHeight;  // current backbuffer height

// ============================================================================
// ESP Settings
// ============================================================================
extern std::atomic<bool> g_EspEnabled;       // master ESP on/off
extern std::atomic<bool> g_EspBoxes;         // draw bounding boxes
extern std::atomic<bool> g_EspHealth;        // draw health bars
extern std::atomic<bool> g_EspNames;         // draw player names
extern std::atomic<bool> g_EspSkeleton;      // draw bone skeleton
extern std::atomic<int>  g_EspMaxDistance;   // max distance to draw (0 = unlimited)

// ============================================================================
// Aimbot Settings
// ============================================================================
extern std::atomic<bool> g_AimbotEnabled;    // master aimbot on/off
extern std::atomic<bool> g_AimbotOnKey;      // only active when key is held
extern std::atomic<int>   g_AimbotKey;       // VK code for aimbot activation (default: VK_XBUTTON2 / Mouse5)
extern std::atomic<float> g_AimbotSmoothness;// 1.0 = instant, higher = smoother
extern std::atomic<float> g_AimbotFOV;       // max angle distance to target (degrees)
extern std::atomic<bool> g_AimbotTargetTeam; // false = enemies only, true = everyone

// ============================================================================
// Debug Info (updated by features, read by renderer)
// ============================================================================
extern std::atomic<int>   g_DbgEntityCount;    // how many valid entities found last frame
extern std::atomic<int>   g_DbgBoneReadOK;     // 1 = bone read succeeded at least once, 0 = never
extern std::atomic<int>   g_DbgAimbotTarget;   // 1 = aimbot found a target last tick, 0 = none
extern std::atomic<int>   g_DbgTriggerFired;   // 1 = triggerbot fired last tick, 0 = no
extern std::atomic<float> g_DbgViewAnglesPitch; // last read view pitch (for debugging)
extern std::atomic<float> g_DbgViewAnglesYaw;   // last read view yaw (for debugging)
extern std::atomic<int>   g_DbgAngleSource;     // 1=pBaseCmd, 2=dwVA direct, 3=dwVA indirect, 0=fail

// ============================================================================
// Skeleton Debug (manual override for bone offset testing)
// ============================================================================
extern std::atomic<int>   g_DbgBoneOverride;     // -1 = auto, otherwise manual offset in hex
extern std::atomic<bool>  g_DbgBoneUseChild;     // false = direct, true = via m_pChild
extern std::atomic<int>   g_DbgBoneFormat;       // 0 = auto, 1 = Matrix3x4, 2 = CTransform
extern std::atomic<float> g_DbgBoneHeadX;        // last head bone X (for debugging)
extern std::atomic<float> g_DbgBoneHeadY;
extern std::atomic<float> g_DbgBoneHeadZ;
extern std::atomic<int>   g_DbgBoneScanResult;   // best offset found by scanner (or -1)
extern std::atomic<bool>  g_DbgBoneScanChild;    // best result uses child?
extern std::atomic<int>   g_DbgBoneScanFormat;   // best result format (1=Matrix3x4, 2=CTransform)
extern std::atomic<bool>  g_DbgBoneScanning;     // true while scanning
extern std::atomic<bool>  g_DbgBoneExplorer;     // draw bone index labels on screen for mapping
extern std::atomic<int>   g_DbgBoneExplorerTarget; // which entity index to explore (-1 = first enemy)

// ============================================================================
// Triggerbot Settings
// ============================================================================
extern std::atomic<bool>    _TriggerbotEnabled;  // master triggerbot on/off
extern std::atomic<bool>    g_TriggerbotOnKey;   // only active when key is held
extern std::atomic<int>     g_TriggerbotKey;     // VK code for triggerbot activation (default: VK_XBUTTON1 / Mouse4)
extern std::atomic<int>     g_TriggerbotDelay;   // delay in ms before firing (0 = instant)

// ============================================================================
// D3D11 VTable Index
// ============================================================================
// Present sits at index 8 in the IDXGISwapChain vtable. This is constant
// across all Windows versions. We use it to find the Present function address.
// ============================================================================
inline constexpr int VTABLE_PRESENT_INDEX = 8;

// ============================================================================
// D3D11 / ImGui State
// ============================================================================
extern HWND                     g_hWindow;                // game window handle
extern ID3D11Device*            g_pd3dDevice;             // D3D11 device
extern ID3D11DeviceContext*     g_pd3dDeviceContext;      // immediate context
extern ID3D11RenderTargetView*  g_pMainRenderTargetView; // backbuffer RTV
extern WNDPROC                  g_OriginalWndProc;        // original WndProc before hook
extern HMODULE                  g_hModule;                // our DLL handle (for FreeLibrary)

// ============================================================================
// ESP Double-Buffered Render Data
// ============================================================================
// ESP has a two-stage pipeline to avoid stalling the render thread:
//  1. Update thread (EspUpdateThread) reads game memory and writes to buffer 0
//  2. Present hook reads from buffer 1 and draws to screen
//  g_EspWriteIdx toggles which buffer is the "write" buffer each frame.
//
//  This is the "render-side" data — pre-computed so the Present hook does
//  ZERO heavy work (no pointer chasing, no memory reads, no math).
// ============================================================================
struct EspBoneScreen {
    float x, y;
    bool visible;
};

struct EspBoneWorld {
    float x, y, z;
    bool visible;
};

struct EspRenderData {
    float boxX, boxY, boxW, boxH;      // bounding box in screen coords
    float barX, barY, barW, barH;      // health bar position
    float healthPct;                   // health as 0.0–1.0
    float healthBarFillH;              // filled height of health bar
    int   health;                      // raw health value
    bool  isEnemy;                     // true = enemy team
    bool  hasName;                     // true if name was read successfully
    char  name[32];                    // player name
    float nameX, nameY;              // name position on screen
    float distX, distY;              // distance label position
    char  distText[16];                // pre-formatted distance string
    // Skeleton ESP data
    EspBoneScreen bones[20];           // pre-computed bone screen positions (mapped)
    int boneCount;                     // number of valid mapped bones
    EspBoneScreen explorerBones[32];   // all bone indices 0-31 for explorer
    EspBoneWorld  explorerWorld[32];   // world positions for auto-skeleton
    int explorerBoneCount;             // how many valid explorer bones
};

extern std::vector<EspRenderData> g_EspRenderBuffer[2];  // double buffer
extern std::atomic<int>         g_EspWriteIdx;          // which buffer is being written
extern std::atomic<int>         g_EspEntityCount;       // how many entities in current buffer

// ============================================================================
// CreateMove Hook State
// ============================================================================
extern void* g_OriginalCreateMove;     // trampoline to original CreateMove
extern void* g_HookedCreateMoveAddr;   // address we hooked
extern bool  g_CreateMoveHookInstalled; // true once installed

// ============================================================================
// Win32 API Hook Function Pointers (original functions saved for trampolines)
// ============================================================================
using SetCursorPosFn = BOOL(WINAPI*)(int, int);
using SetCursorFn    = HCURSOR(WINAPI*)(HCURSOR);
using ShowCursorFn   = int(WINAPI*)(BOOL);
using ClipCursorFn   = BOOL(WINAPI*)(const RECT*);

extern SetCursorPosFn g_oSetCursorPos;
extern SetCursorFn    g_oSetCursor;
extern ShowCursorFn   g_oShowCursor;
extern ClipCursorFn   g_oClipCursor;
