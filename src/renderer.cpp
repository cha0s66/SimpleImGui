// ============================================================================
// renderer.cpp  -  D3D11 Hook, ImGui Integration & Menu UI
// ============================================================================

#include "renderer.h"
#include "hooks.h"
#include "features.h"
#include "globals.h"
#include "input.h"
#include <MinHook.h>
#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_dx11.h>
#include <imgui/backends/imgui_impl_win32.h>

using PresentFn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT);
using D3D11CreateDeviceAndSwapChainFn = HRESULT(WINAPI*)(IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT,
    const D3D_FEATURE_LEVEL*, UINT, UINT, const DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**, ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);

static PresentFn g_oPresent = nullptr;
static D3D11CreateDeviceAndSwapChainFn g_oD3D11CreateDeviceAndSwapChain = nullptr;
static bool g_D3D11CreateDeviceAndSwapChainHookInstalled = false;

bool CreateRenderTarget(IDXGISwapChain* pSwapChain)
{
    ID3D11Texture2D* pBackBuffer = nullptr;
    HRESULT hr = pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer);
    if (FAILED(hr) || !pBackBuffer) return false;

    D3D11_TEXTURE2D_DESC desc;
    pBackBuffer->GetDesc(&desc);

    D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format = desc.Format;
    rtvDesc.ViewDimension = (desc.SampleDesc.Count > 1)
        ? D3D11_RTV_DIMENSION_TEXTURE2DMS
        : D3D11_RTV_DIMENSION_TEXTURE2D;

    hr = g_pd3dDevice->CreateRenderTargetView(pBackBuffer, &rtvDesc, &g_pMainRenderTargetView);
    pBackBuffer->Release();
    return SUCCEEDED(hr);
}

void CleanupRenderTarget()
{
    if (g_pMainRenderTargetView) {
        g_pMainRenderTargetView->Release();
        g_pMainRenderTargetView = nullptr;
    }
}

void* GetPresentAddress()
{
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc   = DefWindowProcW;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"DX11HookDummy";
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0, L"DX11HookDummy", L"", 0, 0, 0, 2, 2,
                                HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
    if (!hwnd) { UnregisterClassW(L"DX11HookDummy", wc.hInstance); return nullptr; }

    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 1;
    sd.BufferDesc.Width = 2; sd.BufferDesc.Height = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    ID3D11Device* pDevice = nullptr;
    ID3D11DeviceContext* pContext = nullptr;
    IDXGISwapChain* pSwapChain = nullptr;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        nullptr, 0, D3D11_SDK_VERSION, &sd,
        &pSwapChain, &pDevice, nullptr, &pContext);

    void* presentAddr = nullptr;
    if (SUCCEEDED(hr) && pSwapChain) {
        void** vtable = *reinterpret_cast<void***>(pSwapChain);
        presentAddr = vtable[VTABLE_PRESENT_INDEX];
    }

    if (pSwapChain) pSwapChain->Release();
    if (pContext) pContext->Release();
    if (pDevice) pDevice->Release();
    DestroyWindow(hwnd);
    UnregisterClassW(L"DX11HookDummy", wc.hInstance);
    return presentAddr;
}

HRESULT WINAPI hkD3D11CreateDeviceAndSwapChain(
    IDXGIAdapter* pAdapter,
    D3D_DRIVER_TYPE DriverType,
    HMODULE Software,
    UINT Flags,
    const D3D_FEATURE_LEVEL* pFeatureLevels,
    UINT FeatureLevels,
    UINT SDKVersion,
    const DXGI_SWAP_CHAIN_DESC* pSwapChainDesc,
    IDXGISwapChain** ppSwapChain,
    ID3D11Device** ppDevice,
    D3D_FEATURE_LEVEL* pFeatureLevel,
    ID3D11DeviceContext** ppImmediateContext)
{
    HRESULT hr = g_oD3D11CreateDeviceAndSwapChain(
        pAdapter, DriverType, Software, Flags,
        pFeatureLevels, FeatureLevels, SDKVersion,
        pSwapChainDesc, ppSwapChain, ppDevice,
        pFeatureLevel, ppImmediateContext);

    if (SUCCEEDED(hr) && ppSwapChain && *ppSwapChain && !g_PresentHookInstalled) {
        void** vtable = *reinterpret_cast<void***>(*ppSwapChain);
        if (vtable && vtable[VTABLE_PRESENT_INDEX]) {
            InstallPresentHook(vtable[VTABLE_PRESENT_INDEX]);
        }
    }

    return hr;
}

HRESULT __stdcall hkPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags)
{
    if (g_ShouldUnload.load()) return g_oPresent(pSwapChain, SyncInterval, Flags);

    if (!g_ImGuiInitialized.load())
    {

        ID3D11Device* pDevice = nullptr;
        if (FAILED(pSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&pDevice)) || !pDevice)
            goto render;

        if (pDevice != g_pd3dDevice) {
            if (g_pd3dDevice) g_pd3dDevice->Release();
            g_pd3dDevice = pDevice;
            g_pd3dDevice->AddRef();
        }
        pDevice->Release();

        g_pd3dDevice->GetImmediateContext(&g_pd3dDeviceContext);
        if (!g_pd3dDeviceContext) goto render;

        DXGI_SWAP_CHAIN_DESC sd = {};
        if (FAILED(pSwapChain->GetDesc(&sd))) goto render;
        g_hWindow = sd.OutputWindow;

        if (!g_hWindow || !IsWindow(g_hWindow)) {
            EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
                DWORD pid = 0;
                GetWindowThreadProcessId(hwnd, &pid);
                if (pid == GetCurrentProcessId() && IsWindowVisible(hwnd)) {
                    *reinterpret_cast<HWND*>(lParam) = hwnd;
                    return FALSE;
                }
                return TRUE;
            }, reinterpret_cast<LPARAM>(&g_hWindow));
        }

        CleanupRenderTarget();
        if (!CreateRenderTarget(pSwapChain)) goto render;

        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        ImGui::StyleColorsDark();

        bool win32Ok = ImGui_ImplWin32_Init(g_hWindow);
        bool dx11Ok = ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

        if (win32Ok && dx11Ok) {
            g_ImGuiInitialized.store(true);
        } else {
            ImGui_ImplDX11_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
            CleanupRenderTarget();
        }
    }

    if (g_ImGuiInitialized.load()) {
        DXGI_SWAP_CHAIN_DESC sd = {};
        if (SUCCEEDED(pSwapChain->GetDesc(&sd))) {
            if ((UINT)sd.BufferDesc.Width != g_LastWidth || (UINT)sd.BufferDesc.Height != g_LastHeight) {
                g_LastWidth = sd.BufferDesc.Width;
                g_LastHeight = sd.BufferDesc.Height;
                ImGuiIO& io = ImGui::GetIO();
                io.DisplaySize = ImVec2((float)g_LastWidth, (float)g_LastHeight);
                CleanupRenderTarget();
                CreateRenderTarget(pSwapChain);
                ImGui_ImplDX11_InvalidateDeviceObjects();
                ImGui_ImplDX11_CreateDeviceObjects();
            }
        }
    }

render:
    if (!g_ImGuiInitialized.load() || !g_pd3dDeviceContext || !g_pMainRenderTargetView)
        return g_oPresent(pSwapChain, SyncInterval, Flags);

    ID3D11RenderTargetView* pOldRTVs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
    ID3D11DepthStencilView* pOldDSV = nullptr;
    g_pd3dDeviceContext->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, pOldRTVs, &pOldDSV);

    D3D11_VIEWPORT oldViewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
    UINT numViewports = 0;
    g_pd3dDeviceContext->RSGetViewports(&numViewports, oldViewports);

    D3D11_VIEWPORT vp = {};
    vp.Width = (FLOAT)g_LastWidth; vp.Height = (FLOAT)g_LastHeight;
    vp.MinDepth = 0.0f; vp.MaxDepth = 1.0f;
    g_pd3dDeviceContext->RSSetViewports(1, &vp);
    g_pd3dDeviceContext->OMSetRenderTargets(1, &g_pMainRenderTargetView, nullptr);

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    bool insertPressed = (GetAsyncKeyState(VK_INSERT) & 0x8000) != 0;
    bool endPressed = (GetAsyncKeyState(VK_END) & 0x8000) != 0;

    if (insertPressed && !g_InsertDown) {
        g_MenuOpen = !g_MenuOpen;
        if (g_MenuOpen) ForceMenuCursorMode();
        else            ForceGameplayCursorMode();
    }
    g_InsertDown = insertPressed;

    if (endPressed && !g_EndDown) {
        g_ShouldUnload.store(true);
    }
    g_EndDown = endPressed;

    ImGuiIO& io = ImGui::GetIO();
    if (g_MenuOpen)
    {
        if (g_hWindow) {
            POINT pt;
            if (GetCursorPos(&pt) && ScreenToClient(g_hWindow, &pt)) {
                if (pt.x != g_LastMousePos.x || pt.y != g_LastMousePos.y) {
                    io.MousePos = ImVec2((float)pt.x, (float)pt.y);
                    g_LastMousePos = pt;
                }
            }
        }

        bool mouseDown[5] = {
            (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0,
            (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0,
            (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0,
            (GetAsyncKeyState(VK_XBUTTON1) & 0x8000) != 0,
            (GetAsyncKeyState(VK_XBUTTON2) & 0x8000) != 0,
        };
        for (int i = 0; i < 5; i++) {
            if (mouseDown[i] != g_LastMouseDown[i]) {
                io.MouseDown[i] = mouseDown[i];
                g_LastMouseDown[i] = mouseDown[i];
            }
        }
        io.MouseWheel = 0.0f; io.MouseWheelH = 0.0f;
        io.MouseDrawCursor = true;
    }
    else
    {
        io.MouseDrawCursor = false;
        io.MousePos = ImVec2(-FLT_MAX, -FLT_MAX);
        for (int i = 0; i < 5; i++) { io.MouseDown[i] = false; g_LastMouseDown[i] = false; }
        io.MouseWheel = 0.0f; io.MouseWheelH = 0.0f;
        g_LastMousePos = { 0, 0 };
    }

    // Draw menu
    if (g_MenuOpen)
    {
        ImGui::SetNextWindowPos(ImVec2(50, 50), ImGuiCond_Once);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 4.0f);
        ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.15f, 0.15f, 0.20f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.10f, 0.10f, 0.14f, 0.95f));

        ImGui::Begin("NeonHook", nullptr, ImGuiWindowFlags_NoCollapse);

        ImGui::Text("INSERT toggle | END unload");
        ImGui::Separator();

        bool bhop = g_BhopEnabled.load();
        if (ImGui::Checkbox("BunnyHop", &bhop)) g_BhopEnabled.store(bhop);

        ImGui::Separator();

        // ESP
        ImGui::Text("ESP");
        bool esp = g_EspEnabled.load();
        if (ImGui::Checkbox("Enable ESP", &esp)) g_EspEnabled.store(esp);

        if (esp) {
            bool boxes = g_EspBoxes.load();
            if (ImGui::Checkbox("Bounding Boxes", &boxes)) g_EspBoxes.store(boxes);
            bool health = g_EspHealth.load();
            if (ImGui::Checkbox("Health Bars", &health)) g_EspHealth.store(health);
            bool names = g_EspNames.load();
            if (ImGui::Checkbox("Player Names", &names)) g_EspNames.store(names);
            bool skeleton = g_EspSkeleton.load();
            if (ImGui::Checkbox("Skeleton", &skeleton)) g_EspSkeleton.store(skeleton);
            int maxDist = g_EspMaxDistance.load();
            if (ImGui::SliderInt("Max Distance", &maxDist, 0, 10000)) g_EspMaxDistance.store(maxDist);
        }

        ImGui::Separator();

        // Aimbot
        ImGui::Text("Aimbot");
        bool aimbot = g_AimbotEnabled.load();
        if (ImGui::Checkbox("Enable Aimbot", &aimbot)) g_AimbotEnabled.store(aimbot);

        if (aimbot) {
            bool aimOnKey = g_AimbotOnKey.load();
            if (ImGui::Checkbox("On Key##Aimbot", &aimOnKey)) g_AimbotOnKey.store(aimOnKey);
            float smooth = g_AimbotSmoothness.load();
            if (ImGui::SliderFloat("Smoothness", &smooth, 1.0f, 50.0f, "%.1f")) g_AimbotSmoothness.store(smooth);
            float fov = g_AimbotFOV.load();
            if (ImGui::SliderFloat("FOV", &fov, 1.0f, 90.0f, "%.1f deg")) g_AimbotFOV.store(fov);
        }

        ImGui::Separator();

        // // Triggerbot (W.I.P)
        // ImGui::Text("Triggerbot");
        // bool trigger = g_TriggerbotEnabled.load();
        // if (ImGui::Checkbox("Enable Triggerbot", &trigger)) g_TriggerbotEnabled.store(trigger);

        // if (trigger) {
        //     bool trigOnKey = g_TriggerbotOnKey.load();
        //     if (ImGui::Checkbox("On Key##Trigger", &trigOnKey)) g_TriggerbotOnKey.store(trigOnKey);
        //     int delay = g_TriggerbotDelay.load();
        //     if (ImGui::SliderInt("Delay (ms)", &delay, 0, 500)) g_TriggerbotDelay.store(delay);
        // }

        ImGui::End();

        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(3);
    }

    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    RenderEspFast(drawList);

    ImGui::Render();
    g_pd3dDeviceContext->OMSetRenderTargets(1, &g_pMainRenderTargetView, nullptr);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    g_pd3dDeviceContext->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, pOldRTVs, pOldDSV);
    if (numViewports > 0) g_pd3dDeviceContext->RSSetViewports(numViewports, oldViewports);

    for (int i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; i++)
        if (pOldRTVs[i]) pOldRTVs[i]->Release();
    if (pOldDSV) pOldDSV->Release();

    return g_oPresent(pSwapChain, SyncInterval, Flags);
}

bool InstallPresentHook(void* presentPtr)
{
    if (!presentPtr || g_PresentHookInstalled) return false;

    MH_STATUS status = MH_CreateHook(presentPtr, reinterpret_cast<LPVOID>(&hkPresent),
                                     reinterpret_cast<LPVOID*>(&g_oPresent));
    if (status != MH_OK) {
        if (status == MH_ERROR_ALREADY_CREATED) {
            MH_DisableHook(presentPtr);
            MH_RemoveHook(presentPtr);
            status = MH_CreateHook(presentPtr, reinterpret_cast<LPVOID>(&hkPresent),
                                   reinterpret_cast<LPVOID*>(&g_oPresent));
        }
        if (status != MH_OK)
            return false;
    }

    if (MH_EnableHook(presentPtr) != MH_OK) {
        MH_RemoveHook(presentPtr);
        return false;
    }
    g_HookedPresentPtr = presentPtr;
    g_PresentHookInstalled = true;
    return true;
}
