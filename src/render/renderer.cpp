#include "render/renderer.h"
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <dwmapi.h>
#include <dxgi1_2.h>
#include <dcomp.h>
#include "core/log.h"
namespace nl {
namespace {
ID3D11Device*           g_device = nullptr;
ID3D11DeviceContext*    g_context = nullptr;
IDXGISwapChain*         g_swapChain = nullptr;
ID3D11RenderTargetView* g_rtv = nullptr;
IDCompositionDevice*    g_dcompDevice = nullptr;
IDCompositionTarget*    g_dcompTarget = nullptr;
IDCompositionVisual*    g_dcompVisual = nullptr;
HWND g_hwnd = nullptr;
bool g_transparent = false;
bool g_colorKey = false;
bool g_composition = false;
// 颜色键模式下的透明色（该颜色不参与 UI 绘制）
const COLORREF kColorKey = RGB(255, 0, 255);
void CreateRTV()
{
    ID3D11Texture2D* back = nullptr;
    if (SUCCEEDED(g_swapChain->GetBuffer(0, IID_PPV_ARGS(&back))) && back != nullptr)
    {
        g_device->CreateRenderTargetView(back, nullptr, &g_rtv);
        back->Release();
    }
}
void ReleaseRTV()
{
    if (g_rtv)
    {
        g_rtv->Release();
        g_rtv = nullptr;
    }
}
void ReleaseDeviceObjects()
{
    if (g_dcompVisual) { g_dcompVisual->Release(); g_dcompVisual = nullptr; }
    if (g_dcompTarget) { g_dcompTarget->Release(); g_dcompTarget = nullptr; }
    if (g_dcompDevice) { g_dcompDevice->Release(); g_dcompDevice = nullptr; }
    if (g_swapChain) { g_swapChain->Release(); g_swapChain = nullptr; }
    if (g_context) { g_context->Release(); g_context = nullptr; }
    if (g_device) { g_device->Release(); g_device = nullptr; }
}
bool GetFactory2(IDXGIFactory2** out)
{
    *out = nullptr;
    IDXGIDevice* dxgiDevice = nullptr;
    if (FAILED(g_device->QueryInterface(IID_PPV_ARGS(&dxgiDevice))) || dxgiDevice == nullptr)
        return false;
    IDXGIAdapter* adapter = nullptr;
    dxgiDevice->GetAdapter(&adapter);
    dxgiDevice->Release();
    if (adapter == nullptr)
        return false;
    const HRESULT hr = adapter->GetParent(IID_PPV_ARGS(out));
    adapter->Release();
    return SUCCEEDED(hr) && *out != nullptr;
}
bool TryCreateFlipSwapChain(DXGI_SWAP_EFFECT effect, DXGI_ALPHA_MODE alphaMode, UINT bufferCount,
                            bool explicitSize, const char* label)
{
    IDXGIFactory2* factory = nullptr;
    if (!GetFactory2(&factory))
    {
        FileLog("[render] GetParent(IDXGIFactory2) failed");
        return false;
    }
    DXGI_SWAP_CHAIN_DESC1 sd = {};
    if (explicitSize)
    {
        RECT cr = {};
        GetClientRect(g_hwnd, &cr);
        sd.Width = (UINT)((cr.right - cr.left) > 0 ? (cr.right - cr.left) : 1);
        sd.Height = (UINT)((cr.bottom - cr.top) > 0 ? (cr.bottom - cr.top) : 1);
    }
    sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = bufferCount;
    sd.Scaling = DXGI_SCALING_STRETCH;
    sd.SwapEffect = effect;
    sd.AlphaMode = alphaMode;
    IDXGISwapChain1* swapChain1 = nullptr;
    const HRESULT hr = factory->CreateSwapChainForHwnd(g_device, g_hwnd, &sd, nullptr, nullptr, &swapChain1);
    if (SUCCEEDED(hr))
    {
        g_swapChain = swapChain1;
        factory->MakeWindowAssociation(g_hwnd, DXGI_MWA_NO_ALT_ENTER);
        FileLog("[render] swapchain ok: %s", label);
    }
    else
    {
        FileLog("[render] swapchain failed (%s) hr=0x%08lX", label, (unsigned long)hr);
    }
    factory->Release();
    return SUCCEEDED(hr);
}
bool CreateClassicSwapChain(const char* label)
{
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = g_hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    ReleaseDeviceObjects();
    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL obtained = D3D_FEATURE_LEVEL_11_0;
    const HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2,
                                                     D3D11_SDK_VERSION, &sd, &g_swapChain, &g_device, &obtained, &g_context);
    if (FAILED(hr))
    {
        FileLog("[render] classic swapchain failed (%s) hr=0x%08lX", label, (unsigned long)hr);
        return false;
    }
    FileLog("[render] swapchain ok: %s", label);
    return true;
}
void EnableLayeredColorKey()
{
    LONG_PTR ex = GetWindowLongPtrW(g_hwnd, GWL_EXSTYLE);
    // 分层窗口要求有重定向表面
    ex &= ~(LONG_PTR)WS_EX_NOREDIRECTIONBITMAP;
    SetWindowLongPtrW(g_hwnd, GWL_EXSTYLE, ex | WS_EX_LAYERED);
    SetWindowPos(g_hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    SetLayeredWindowAttributes(g_hwnd, kColorKey, 0, LWA_COLORKEY);
}
// DirectComposition：交换链直接交给 DWM 合成，alpha 由合成器保证（最干净的逐像素透明）
bool CreateCompositionSwapChain()
{
    auto cleanup = []()
    {
        if (g_dcompVisual) { g_dcompVisual->Release(); g_dcompVisual = nullptr; }
        if (g_dcompTarget) { g_dcompTarget->Release(); g_dcompTarget = nullptr; }
        if (g_dcompDevice) { g_dcompDevice->Release(); g_dcompDevice = nullptr; }
    };
    IDXGIFactory2* factory = nullptr;
    if (!GetFactory2(&factory))
        return false;
    RECT cr = {};
    GetClientRect(g_hwnd, &cr);
    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.Width = (UINT)((cr.right - cr.left) > 0 ? (cr.right - cr.left) : 1);
    sd.Height = (UINT)((cr.bottom - cr.top) > 0 ? (cr.bottom - cr.top) : 1);
    sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.Scaling = DXGI_SCALING_STRETCH;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    sd.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    IDXGISwapChain1* swapChain1 = nullptr;
    HRESULT hr = factory->CreateSwapChainForComposition(g_device, &sd, nullptr, &swapChain1);
    factory->Release();
    if (FAILED(hr) || swapChain1 == nullptr)
    {
        FileLog("[render] CreateSwapChainForComposition failed hr=0x%08lX", (unsigned long)hr);
        return false;
    }
    IDXGIDevice* dxgiDevice = nullptr;
    if (FAILED(g_device->QueryInterface(IID_PPV_ARGS(&dxgiDevice))) || dxgiDevice == nullptr)
        return false;
    hr = DCompositionCreateDevice(dxgiDevice, __uuidof(IDCompositionDevice), reinterpret_cast<void**>(&g_dcompDevice));
    dxgiDevice->Release();
    if (FAILED(hr) || g_dcompDevice == nullptr)
    {
        FileLog("[render] DCompositionCreateDevice failed hr=0x%08lX", (unsigned long)hr);
        swapChain1->Release();
        return false;
    }
    hr = g_dcompDevice->CreateTargetForHwnd(g_hwnd, TRUE, &g_dcompTarget);
    if (FAILED(hr))
    {
        FileLog("[render] CreateTargetForHwnd failed hr=0x%08lX", (unsigned long)hr);
        cleanup();
        swapChain1->Release();
        return false;
    }
    hr = g_dcompDevice->CreateVisual(&g_dcompVisual);
    if (FAILED(hr))
    {
        FileLog("[render] CreateVisual failed hr=0x%08lX", (unsigned long)hr);
        cleanup();
        swapChain1->Release();
        return false;
    }
    g_dcompVisual->SetContent(swapChain1);
    g_dcompTarget->SetRoot(g_dcompVisual);
    hr = g_dcompDevice->Commit();
    if (FAILED(hr))
    {
        FileLog("[render] dcomp Commit failed hr=0x%08lX", (unsigned long)hr);
        cleanup();
        swapChain1->Release();
        return false;
    }
    g_swapChain = swapChain1;
    g_composition = true;
    FileLog("[render] swapchain ok: composition + directcomposition (per pixel alpha)");
    return true;
}
bool CreateDeviceTransparent()
{
    BOOL composition = FALSE;
    DwmIsCompositionEnabled(&composition);
    FileLog("[render] dwm composition: %s", composition ? "on" : "off");
    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL obtained = D3D_FEATURE_LEVEL_11_0;
    const HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                         levels, 3, D3D11_SDK_VERSION, &g_device, &obtained, &g_context);
    if (FAILED(hr))
    {
        FileLog("[render] D3D11CreateDevice failed hr=0x%08lX", (unsigned long)hr);
        return false;
    }
    IDXGIDevice* dxgiDevice = nullptr;
    if (SUCCEEDED(g_device->QueryInterface(IID_PPV_ARGS(&dxgiDevice))) && dxgiDevice)
    {
        IDXGIAdapter* adapter = nullptr;
        dxgiDevice->GetAdapter(&adapter);
        if (adapter)
        {
            DXGI_ADAPTER_DESC desc = {};
            adapter->GetDesc(&desc);
            char name[160] = { 0 };
            WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, name, sizeof(name) - 1, nullptr, nullptr);
            FileLog("[render] adapter: %s  vendor=0x%04X device=0x%04X", name, (unsigned)desc.VendorId, (unsigned)desc.DeviceId);
            adapter->Release();
        }
        dxgiDevice->Release();
    }
    // 1) DirectComposition + 预乘 alpha：逐像素透明，由 DWM 合成保证
    bool ok = CreateCompositionSwapChain();
    if (ok)
    {
        g_transparent = true;
        return true;
    }
    // 2) 窗口交换链 + 预乘 alpha（部分驱动支持）
    ok = TryCreateFlipSwapChain(DXGI_SWAP_EFFECT_FLIP_DISCARD, DXGI_ALPHA_MODE_PREMULTIPLIED, 2, false, "flip discard + premultiplied");
    if (!ok)
        ok = TryCreateFlipSwapChain(DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL, DXGI_ALPHA_MODE_PREMULTIPLIED, 2, false, "flip sequential + premultiplied");
    if (ok)
    {
        g_transparent = true;
        MARGINS margins = { -1, -1, -1, -1 };
        const HRESULT dwmHr = DwmExtendFrameIntoClientArea(g_hwnd, &margins);
        FileLog("[render] per pixel alpha enabled (dwm hr=0x%08lX)", (unsigned long)dwmHr);
        return true;
    }
    // 3) 颜色键：分层窗口 + 透明色（兼容性最好）
    EnableLayeredColorKey();
    if (TryCreateFlipSwapChain(DXGI_SWAP_EFFECT_FLIP_DISCARD, DXGI_ALPHA_MODE_UNSPECIFIED, 2, false, "layered color key + flip discard") ||
        CreateClassicSwapChain("layered color key + discard"))
    {
        g_transparent = true;
        g_colorKey = true;
        FileLog("[render] color key transparency enabled");
        return true;
    }
    return false;
}
}
bool RenderInit(HWND hwnd)
{
    g_hwnd = hwnd;
    if (!CreateDeviceTransparent())
    {
        ReleaseDeviceObjects();
        if (!CreateClassicSwapChain("opaque fallback"))
            return false;
        g_transparent = false;
        g_colorKey = false;
    }
    CreateRTV();
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    if (!ImGui_ImplWin32_Init(g_hwnd))
        return false;
    if (!ImGui_ImplDX11_Init(g_device, g_context))
        return false;
    return true;
}
void RenderShutdown()
{
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    ReleaseRTV();
    ReleaseDeviceObjects();
}
void RenderNewFrame()
{
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}
void RenderPresent()
{
    ImGui::Render();
    const float clearTransparent[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    const float clearColorKey[4] = { 1.0f, 0.0f, 1.0f, 1.0f };
    const float clearOpaque[4] = { 0.035f, 0.035f, 0.045f, 1.0f };
    const float* clear = g_colorKey ? clearColorKey : (g_transparent ? clearTransparent : clearOpaque);
    if (g_rtv == nullptr)
        CreateRTV();
    g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
    g_context->ClearRenderTargetView(g_rtv, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    g_swapChain->Present(1, 0);
}
void RenderResize(int width, int height)
{
    if (g_swapChain == nullptr || width <= 0 || height <= 0)
        return;
    ReleaseRTV();
    g_swapChain->ResizeBuffers(0, (UINT)width, (UINT)height, DXGI_FORMAT_UNKNOWN, 0);
    CreateRTV();
}
bool RenderIsTransparent() { return g_transparent; }
bool RenderUsesColorKey() { return g_colorKey; }
const char* RenderModeName()
{
    if (g_composition)
        return "directcomposition + premultiplied alpha (per pixel transparent)";
    if (g_colorKey)
        return "layered color key transparency (fallback)";
    if (g_transparent)
        return "flip model + premultiplied alpha (per pixel transparent)";
    return "opaque fallback";
}
COLORREF RenderColorKey() { return kColorKey; }
ID3D11Device* RenderDevice() { return g_device; }
ID3D11DeviceContext* RenderContext() { return g_context; }
}
