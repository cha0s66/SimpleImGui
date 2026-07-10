// ============================================================================
// globals.cpp  -  Global State Definitions
// ============================================================================
// This is the ONLY place where shared globals are actually allocated.
// Every other .cpp file includes globals.h and gets `extern` declarations.
// This pattern prevents linker "multiple definition" errors.
// ============================================================================

#include "globals.h"

// ============================================================================
// Feature Toggles (thread-safe via std::atomic)
// ============================================================================
std::atomic<bool> g_ImGuiInitialized    { false };
std::atomic<bool> g_BhopEnabled         { false };
bool g_BhopLastJumpState = false;               // tracks last jump state for subtick transition signaling
std::atomic<bool> g_BhopShouldExit      { false };
std::atomic<bool> g_ShouldUnload        { false };

// ============================================================================
// Menu State (only touched by the Present hook thread, so plain bools are fine)
// ============================================================================
bool g_MenuOpen    = true;   // menu starts visible so the user knows it's loaded
bool g_InsertDown  = false;  // edge-detection for INSERT key
bool g_EndDown     = false;  // edge-detection for END key

// ============================================================================
// Present Hook State
// ============================================================================
void* g_HookedPresentPtr     = nullptr;
bool  g_PresentHookInstalled = false;

// ============================================================================
// Input Tracking (for custom cursor in menu mode)
// ============================================================================
POINT g_LastMousePos    = { 0, 0 };
bool  g_LastMouseDown[5] = {};

// ============================================================================
// Screen Dimensions (updated every frame by the Present hook)
// ============================================================================
UINT g_LastWidth  = 0;
UINT g_LastHeight = 0;

// ============================================================================
// ESP Settings
// ============================================================================
std::atomic<bool> g_EspEnabled{ false };
std::atomic<bool> g_EspBoxes{ true };     // default: boxes on
std::atomic<bool> g_EspHealth{ true };    // default: health bars on
std::atomic<bool> g_EspNames{ false };    // default: names off (can be noisy)
std::atomic<bool> g_EspSkeleton{ false }; // default: skeleton off
std::atomic<int>  g_EspMaxDistance{ 5000 }; // 0 = unlimited

// ============================================================================
// Aimbot Settings
// ============================================================================
std::atomic<bool> g_AimbotEnabled{ false };
std::atomic<bool> g_AimbotOnKey{ true };
std::atomic<int>   g_AimbotKey{ VK_XBUTTON2 };       // Mouse5
std::atomic<float> g_AimbotSmoothness{ 5.0f };       // default smoothness
std::atomic<float> g_AimbotFOV{ 10.0f };             // 10 degree FOV
std::atomic<bool> g_AimbotTargetTeam{ false };       // enemies only

// ============================================================================
// Triggerbot Settings
// ============================================================================
std::atomic<bool> g_TriggerbotEnabled{ false };
std::atomic<bool> g_TriggerbotOnKey{ true };
std::atomic<int>   g_TriggerbotKey{ VK_XBUTTON1 };   // Mouse4
std::atomic<int>   g_TriggerbotDelay{ 0 };         // 0 = instant fire

// ============================================================================
// Debug Info
// ============================================================================
std::atomic<int>   g_DbgEntityCount{ 0 };
std::atomic<int>   g_DbgBoneReadOK{ 0 };
std::atomic<int>   g_DbgAimbotTarget{ 0 };
std::atomic<int>   g_DbgTriggerFired{ 0 };
std::atomic<float> g_DbgViewAnglesPitch{ 0.0f };
std::atomic<float> g_DbgViewAnglesYaw{ 0.0f };
std::atomic<int>   g_DbgAngleSource{ 0 };

// ============================================================================
// Skeleton Debug (manual override)
// ============================================================================
std::atomic<int>   g_DbgBoneOverride{ -1 };   // -1 = auto detect
std::atomic<bool>  g_DbgBoneUseChild{ false }; // false = direct modelState
std::atomic<int>   g_DbgBoneFormat{ 0 };      // 0 = auto, 1 = Matrix3x4, 2 = CTransform
std::atomic<float> g_DbgBoneHeadX{ 0.0f };
std::atomic<float> g_DbgBoneHeadY{ 0.0f };
std::atomic<float> g_DbgBoneHeadZ{ 0.0f };

// ============================================================================
// Bone Scan Results
// ============================================================================
std::atomic<int>   g_DbgBoneScanResult{ -1 };   // best offset found (or -1)
std::atomic<bool>  g_DbgBoneScanChild{ false }; // best result uses child?
std::atomic<int>   g_DbgBoneScanFormat{ 0 };   // best result format
std::atomic<bool>  g_DbgBoneScanning{ false }; // currently scanning?
std::atomic<bool>  g_DbgBoneExplorer{ false }; // draw bone index labels?
std::atomic<int>   g_DbgBoneExplorerTarget{ 0 }; // entity index to explore

// ============================================================================
// D3D11 / ImGui Handles
// ============================================================================
HWND                    g_hWindow                = nullptr;
ID3D11Device*           g_pd3dDevice             = nullptr;
ID3D11DeviceContext*    g_pd3dDeviceContext      = nullptr;
ID3D11RenderTargetView* g_pMainRenderTargetView  = nullptr;
WNDPROC                 g_OriginalWndProc        = nullptr;
HMODULE                 g_hModule                = nullptr;   // our own DLL handle

// ============================================================================
// ESP Double Buffer (read/write swap every frame)
// ============================================================================
std::vector<EspRenderData> g_EspRenderBuffer[2];
std::atomic<int>         g_EspWriteIdx{ 0 };     // which buffer is being written
std::atomic<int>         g_EspEntityCount{ 0 };   // how many entities in current read buffer

// ============================================================================
// CreateMove Hook State
// ============================================================================
void* g_OriginalCreateMove     = nullptr;  // trampoline to original
void* g_HookedCreateMoveAddr   = nullptr;  // address we hooked
bool  g_CreateMoveHookInstalled = false;   // true once installed successfully

SetCursorPosFn g_oSetCursorPos = nullptr;
SetCursorFn    g_oSetCursor    = nullptr;
ShowCursorFn   g_oShowCursor   = nullptr;
ClipCursorFn   g_oClipCursor   = nullptr;
