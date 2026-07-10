#include "renderer.h"
#include "hooks.h"
#include "globals.h"
#include <cfloat>
#include <MinHook.h>
#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_dx11.h>
#include <imgui/backends/imgui_impl_win32.h>

using PresentFn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT);
static PresentFn g_oPresent = nullptr;

static void DrawUI()
{
    if (!g_MenuOpen)
        return;

    ImGui::SetNextWindowPos(ImVec2(50, 50), ImGuiCond_Once);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 4.0f);
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.15f, 0.15f, 0.20f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.10f, 0.10f, 0.14f, 0.95f));

    ImGui::Begin("ImGui Hook Template", nullptr, ImGuiWindowFlags_NoCollapse);
    ImGui::Text("INSERT toggle | END unload");
    ImGui::Separator();
    ImGui::Text("Replace this UI with your own content.");
    ImGui::End();

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
}

bool CreateRenderTarget(IDXGISwapChain* pSwapChain)
{
    ID3D11Texture2D* backBuffer = nullptr;
    HRESULT hr = pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backBuffer));
    if (FAILED(hr) || !backBuffer)
        return false;

    D3D11_TEXTURE2D_DESC desc;
    backBuffer->GetDesc(&desc);

    D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format = desc.Format;
    rtvDesc.ViewDimension = (desc.SampleDesc.Count > 1)
        ? D3D11_RTV_DIMENSION_TEXTURE2DMS
        : D3D11_RTV_DIMENSION_TEXTURE2D;

    hr = g_pd3dDevice->CreateRenderTargetView(backBuffer, &rtvDesc, &g_pMainRenderTargetView);
    backBuffer->Release();
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
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"DX11HookDummy";
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0, L"DX11HookDummy", L"", 0, 0, 0, 2, 2,
                                HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
    if (!hwnd) {
        UnregisterClassW(L"DX11HookDummy", wc.hInstance);
        return nullptr;
    }

    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 1;
    sd.BufferDesc.Width = 2;
    sd.BufferDesc.Height = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    IDXGISwapChain* swapChain = nullptr;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        nullptr, 0, D3D11_SDK_VERSION, &sd,
        &swapChain, &device, nullptr, &context);

    void* present = nullptr;
    if (SUCCEEDED(hr) && swapChain) {
        void** vtable = *reinterpret_cast<void***>(swapChain);
        present = vtable[VTABLE_PRESENT_INDEX];
    }

    if (swapChain) swapChain->Release();
    if (context) context->Release();
    if (device) device->Release();
    DestroyWindow(hwnd);
    UnregisterClassW(L"DX11HookDummy", wc.hInstance);

    return present;
}

HRESULT __stdcall hkPresent(IDXGISwapChain* pSwapChain, UINT syncInterval, UINT flags)
{
    if (g_ShouldUnload.load())
        return g_oPresent(pSwapChain, syncInterval, flags);

    static bool initAttempted = false;
    if (!g_ImGuiInitialized.load() && !initAttempted) {
        initAttempted = true;

        ID3D11Device* device = nullptr;
        if (FAILED(pSwapChain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&device))) || !device)
            goto render;

        if (device != g_pd3dDevice) {
            if (g_pd3dDevice) g_pd3dDevice->Release();
            g_pd3dDevice = device;
            g_pd3dDevice->AddRef();
        }
        device->Release();

        g_pd3dDevice->GetImmediateContext(&g_pd3dDeviceContext);
        if (!g_pd3dDeviceContext)
            goto render;

        DXGI_SWAP_CHAIN_DESC sd = {};
        if (FAILED(pSwapChain->GetDesc(&sd)))
            goto render;
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
        if (!CreateRenderTarget(pSwapChain))
            goto render;

        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        ImGui::StyleColorsDark();

        bool win32Ok = ImGui_ImplWin32_Init(g_hWindow);
        if (win32Ok && !g_OriginalWndProc)
            g_OriginalWndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrA(g_hWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(HookedWndProc)));

        bool dx11Ok = ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);
        if (win32Ok && dx11Ok)
            g_ImGuiInitialized.store(true);
        else {
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
        return g_oPresent(pSwapChain, syncInterval, flags);

    ID3D11RenderTargetView* oldRTVs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
    ID3D11DepthStencilView* oldDSV = nullptr;
    g_pd3dDeviceContext->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, oldRTVs, &oldDSV);

    D3D11_VIEWPORT oldViewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
    UINT viewportCount = 0;
    g_pd3dDeviceContext->RSGetViewports(&viewportCount, oldViewports);

    D3D11_VIEWPORT vp = {};
    vp.Width = (FLOAT)g_LastWidth;
    vp.Height = (FLOAT)g_LastHeight;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    g_pd3dDeviceContext->RSSetViewports(1, &vp);
    g_pd3dDeviceContext->OMSetRenderTargets(1, &g_pMainRenderTargetView, nullptr);

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    ImGuiIO& io = ImGui::GetIO();
    DrawUI();

    ImGui::Render();
    g_pd3dDeviceContext->OMSetRenderTargets(1, &g_pMainRenderTargetView, nullptr);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    g_pd3dDeviceContext->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, oldRTVs, oldDSV);
    if (viewportCount > 0) g_pd3dDeviceContext->RSSetViewports(viewportCount, oldViewports);

    for (int i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
        if (oldRTVs[i]) oldRTVs[i]->Release();
    if (oldDSV) oldDSV->Release();

    return g_oPresent(pSwapChain, syncInterval, flags);
}

bool InstallPresentHook(void* presentPtr)
{
    if (!presentPtr || g_PresentHookInstalled)
        return false;

    PrepareHookInstall(presentPtr);

    if (MH_CreateHook(presentPtr, reinterpret_cast<LPVOID>(&hkPresent),
                      reinterpret_cast<LPVOID*>(&g_oPresent)) != MH_OK)
        return false;

    if (MH_EnableHook(presentPtr) != MH_OK) {
        MH_RemoveHook(presentPtr);
        return false;
    }

    g_HookedPresentPtr = presentPtr;
    g_PresentHookInstalled = true;
    return true;
}
