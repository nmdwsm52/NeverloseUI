// -----------------------------------------------------------------------------
// NLHookHost —— 用来验证注入版 DLL 的"假游戏"
//   自己开窗口 + D3D11 渲染循环（画一个旋转方块），然后 LoadLibrary 载入
//   NeverloseUI.dll。DLL 会挂钩 Present，界面就画在这个窗口里。
//   按 F6 应能安全卸载 DLL（本进程不崩、画面继续、模块列表里消失）。
//   用法: NLHookHost.exe [dll路径]
// -----------------------------------------------------------------------------
#include <windows.h>
#include <d3d11.h>
#include <cmath>
#include <cstdio>
#include <cstdarg>

#pragma comment(lib, "d3d11.lib")

namespace {
ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
IDXGISwapChain* g_swapChain = nullptr;
ID3D11RenderTargetView* g_rtv = nullptr;
UINT g_width = 1280;
UINT g_height = 720;
bool g_running = true;
HMODULE g_dll = nullptr;

void HostLog(const char* fmt, ...)
{
    FILE* fp = nullptr;
    if (fopen_s(&fp, "host_log.txt", "a") != 0 || fp == nullptr)
        return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(fp, fmt, ap);
    va_end(ap);
    fputc('\n', fp);
    fclose(fp);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_DESTROY)
    {
        g_running = false;
        PostQuitMessage(0);
        return 0;
    }
    if (msg == WM_KEYDOWN && wParam == VK_F7)
    {
        // F7：主程序主动 FreeLibrary，验证"外部卸载"也不会崩
        if (g_dll)
        {
            HostLog("host: FreeLibrary requested (F7)");
            HMODULE dll = g_dll;
            g_dll = nullptr;
            FreeLibrary(dll);
        }
        return 0;
    }
    if (msg == WM_SIZE && g_swapChain != nullptr)
    {
        if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
        g_width = LOWORD(lParam);
        g_height = HIWORD(lParam);
        if (g_width && g_height)
        {
            g_swapChain->ResizeBuffers(0, g_width, g_height, DXGI_FORMAT_UNKNOWN, 0);
            ID3D11Texture2D* back = nullptr;
            if (SUCCEEDED(g_swapChain->GetBuffer(0, IID_PPV_ARGS(&back))) && back)
            {
                g_device->CreateRenderTargetView(back, nullptr, &g_rtv);
                back->Release();
            }
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int)
{
    HostLog("--- host start ---");
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"NLHookHostWindow";
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));   // IDC_ARROW
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"NLHookHost (fake game)", WS_OVERLAPPEDWINDOW,
                                60, 60, g_width, g_height, nullptr, nullptr, wc.hInstance, nullptr);
    if (hwnd == nullptr)
    {
        HostLog("host: CreateWindow failed");
        return 1;
    }
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL obtained = D3D_FEATURE_LEVEL_11_0;
    if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2,
                                             D3D11_SDK_VERSION, &sd, &g_swapChain, &g_device, &obtained, &g_context)))
    {
        HostLog("host: D3D11 init failed");
        return 1;
    }
    {
        ID3D11Texture2D* back = nullptr;
        g_swapChain->GetBuffer(0, IID_PPV_ARGS(&back));
        g_device->CreateRenderTargetView(back, nullptr, &g_rtv);
        if (back) back->Release();
    }

    const char* dllPath = "NeverloseUI.dll";
    HostLog("host: loading %s", dllPath);
    g_dll = LoadLibraryA(dllPath);
    HostLog("host: LoadLibrary -> %s (base=%p)", g_dll ? "ok" : "failed", (void*)g_dll);
    HostLog("host: INSERT 菜单 / F6 DLL 自卸载 / F7 主程序 FreeLibrary / Alt+F4 退出");

    float t = 0.0f;
    while (g_running)
    {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
                g_running = false;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!g_running)
            break;
        t += 0.016f;
        // 假游戏画面：随时间变化的底色，方便确认 DLL 卸载后游戏仍在正常渲染
        const float clear[4] = { 0.10f + 0.08f * sinf(t * 0.5f), 0.12f + 0.08f * sinf(t * 0.7f + 1.0f), 0.16f + 0.10f * sinf(t * 0.3f + 2.0f), 1.0f };
        g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
        g_context->ClearRenderTargetView(g_rtv, clear);
        g_swapChain->Present(1, 0);
    }
    if (g_dll)
    {
        FreeLibrary(g_dll);
        g_dll = nullptr;
    }
    if (g_rtv) g_rtv->Release();
    if (g_context) g_context->Release();
    if (g_device) g_device->Release();
    if (g_swapChain) g_swapChain->Release();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    HostLog("--- host exit ---");
    return 0;
}
