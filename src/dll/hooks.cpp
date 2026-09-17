#include "dll/hooks.h"
#include "core/vmt.h"
#include "core/log.h"
#include "core/paths.h"
#include "core/memory.h"
#include "render/render_internal.h"
#include "ui/menu.h"
#include "ui/style.h"
#include "features/config.h"
#include "features/game.h"
#include "features/misc.h"
#include "features/createmove.h"
#include <d3d11.h>
#include <dxgi.h>
#include <imgui.h>

namespace nl {

namespace {
// IDXGISwapChain 虚表下标（固定值）
constexpr int kVtablePresent = 8;
constexpr int kVtableResizeBuffers = 13;

VmtHook  g_swapChainHook;
void*    g_originalPresent = nullptr;
void*    g_originalResize = nullptr;
WNDPROC  g_originalWndProc = nullptr;
HWND     g_hwnd = nullptr;
HMODULE  g_module = nullptr;
bool     g_menuVisible = true;
volatile LONG g_unloadRequested = 0;
bool     g_menuCloseSeen = false;

using PresentFn = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT);
using ResizeFn = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

bool InsidePanel(HWND hwnd, POINT ptClient)
{
    const ui::PanelRect r = ui::GetMenuPanelRect();
    if (r.w <= 0.0f)
        return false;
    POINT pt = ptClient;
    return pt.x >= r.x && pt.x <= r.x + r.w && pt.y >= r.y && pt.y <= r.y + r.h;
}

// ---------------------------------------------------------------------------
//  输入：不再让 ImGui 挂进游戏的窗口过程
//
//  真实泵（CS2 走 SDL3）里，窗口过程是所有消息的必经之路；以前我们把每条消息都
//  转给 ImGui_ImplWin32_WndProcHandler()，等于把界面库塞进了游戏的输入链路。
//  实测崩溃转储里出现 "SDL3 -> KERNELBASE -> ntdll 写访问违例"，就是这一类干扰。
//  现在改成：窗口过程只做三件事（INSERT 开关、ESC 关、F6 卸载）+ 记录滚轮/按键，
//  其余一律原样转发；鼠标位置/按键/滚轮/键盘在 Present 里自己喂给 ImGui。
// ---------------------------------------------------------------------------
constexpr int kKeyQueueSize = 64;
struct KeyEvent {
    int  vk = 0;
    bool down = false;
    unsigned int ch = 0;     // 非 0 表示 WM_CHAR
};
KeyEvent     g_keyQueue[kKeyQueueSize];
volatile LONG g_keyCount = 0;
volatile LONG g_wheelAccum = 0;

void PushKeyEvent(const KeyEvent& ev)
{
    LONG n = InterlockedIncrement(&g_keyCount);
    if (n <= kKeyQueueSize)
        g_keyQueue[n - 1] = ev;
    else
        InterlockedDecrement(&g_keyCount);
}

// VK -> ImGuiKey（后端没导出这张表，这里自己来，覆盖菜单里用得到的键）
ImGuiKey VirtualKeyToImGuiKey(int vk)
{
    if (vk >= 'A' && vk <= 'Z')
        return (ImGuiKey)(ImGuiKey_A + (vk - 'A'));
    if (vk >= '0' && vk <= '9')
        return (ImGuiKey)(ImGuiKey_0 + (vk - '0'));
    if (vk >= VK_F1 && vk <= VK_F12)
        return (ImGuiKey)(ImGuiKey_F1 + (vk - VK_F1));
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9)
        return (ImGuiKey)(ImGuiKey_Keypad0 + (vk - VK_NUMPAD0));
    switch (vk)
    {
    case VK_TAB: return ImGuiKey_Tab;
    case VK_LEFT: return ImGuiKey_LeftArrow;
    case VK_RIGHT: return ImGuiKey_RightArrow;
    case VK_UP: return ImGuiKey_UpArrow;
    case VK_DOWN: return ImGuiKey_DownArrow;
    case VK_PRIOR: return ImGuiKey_PageUp;
    case VK_NEXT: return ImGuiKey_PageDown;
    case VK_HOME: return ImGuiKey_Home;
    case VK_END: return ImGuiKey_End;
    case VK_DELETE: return ImGuiKey_Delete;
    case VK_BACK: return ImGuiKey_Backspace;
    case VK_SPACE: return ImGuiKey_Space;
    case VK_RETURN: return ImGuiKey_Enter;
    case VK_ESCAPE: return ImGuiKey_Escape;
    case VK_OEM_1: return ImGuiKey_Semicolon;
    case VK_OEM_PLUS: return ImGuiKey_Equal;
    case VK_OEM_COMMA: return ImGuiKey_Comma;
    case VK_OEM_MINUS: return ImGuiKey_Minus;
    case VK_OEM_PERIOD: return ImGuiKey_Period;
    case VK_OEM_2: return ImGuiKey_Slash;
    case VK_OEM_3: return ImGuiKey_GraveAccent;
    case VK_OEM_4: return ImGuiKey_LeftBracket;
    case VK_OEM_5: return ImGuiKey_Backslash;
    case VK_OEM_6: return ImGuiKey_RightBracket;
    case VK_OEM_7: return ImGuiKey_Apostrophe;
    case VK_SHIFT:
    case VK_LSHIFT: return ImGuiKey_LeftShift;
    case VK_RSHIFT: return ImGuiKey_RightShift;
    case VK_CONTROL:
    case VK_LCONTROL: return ImGuiKey_LeftCtrl;
    case VK_RCONTROL: return ImGuiKey_RightCtrl;
    case VK_MENU:
    case VK_LMENU: return ImGuiKey_LeftAlt;
    case VK_RMENU: return ImGuiKey_RightAlt;
    default: return ImGuiKey_None;
    }
}

void DrainInputToImGui()
{
    if (!RenderInternalReady() || g_hwnd == nullptr)
        return;
    ImGuiIO& io = ImGui::GetIO();
    if (g_menuVisible)
    {
        POINT p{};
        GetCursorPos(&p);
        ScreenToClient(g_hwnd, &p);
        io.AddMousePosEvent((float)p.x, (float)p.y);
    }
    static bool prevDown[3] = { false, false, false };
    const int vks[3] = { VK_LBUTTON, VK_RBUTTON, VK_MBUTTON };
    for (int i = 0; i < 3; ++i)
    {
        const bool down = (GetAsyncKeyState(vks[i]) & 0x8000) != 0;
        if (down != prevDown[i])
        {
            io.AddMouseButtonEvent(i, down);
            prevDown[i] = down;
        }
    }
    const LONG wheel = InterlockedExchange(&g_wheelAccum, 0);
    if (wheel != 0)
        io.AddMouseWheelEvent(0.0f, (float)wheel / (float)WHEEL_DELTA);
    const LONG keys = InterlockedExchange(&g_keyCount, 0);
    for (LONG i = 0; i < keys && i < kKeyQueueSize; ++i)
    {
        const KeyEvent& ev = g_keyQueue[i];
        if (ev.ch != 0)
        {
            io.AddInputCharacter(ev.ch);
            continue;
        }
        if (ev.vk == 0)
            continue;
        const ImGuiKey key = VirtualKeyToImGuiKey(ev.vk);
        if (key != ImGuiKey_None)
            io.AddKeyEvent(key, ev.down);
    }
}

LRESULT CALLBACK HookedWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (g_unloadRequested != 0)
        return CallWindowProcW(g_originalWndProc, hwnd, msg, wParam, lParam);

    switch (msg)
    {
    case WM_MOUSEWHEEL:
        InterlockedExchangeAdd(&g_wheelAccum, (LONG)GET_WHEEL_DELTA_WPARAM(wParam));
        break;
    case WM_CHAR:
        if (g_menuVisible)
        {
            KeyEvent ev;
            ev.ch = (unsigned int)wParam;
            PushKeyEvent(ev);
        }
        break;
    case WM_KEYUP:
    case WM_SYSKEYUP:
        if (g_menuVisible)
        {
            KeyEvent ev;
            ev.vk = (int)wParam;
            ev.down = false;
            PushKeyEvent(ev);
        }
        break;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (g_menuVisible && wParam != VK_INSERT && wParam != VK_ESCAPE && wParam != VK_F6)
        {
            KeyEvent ev;
            ev.vk = (int)wParam;
            ev.down = true;
            PushKeyEvent(ev);
        }
        if (wParam == VK_INSERT)
        {
            g_menuVisible = !g_menuVisible;
            if (!g_menuVisible)
            {
                GetCursorPos(nullptr);   // 仅用于触发一次光标刷新
                while (ShowCursor(TRUE) < 0) {}
            }
            ui::LogPush(g_menuVisible ? "menu shown" : "menu hidden");
            if (g_menuVisible)
            {
                // 抢一次前台，保证能输入
                const HWND fg = GetForegroundWindow();
                const DWORD fgThread = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
                const DWORD myThread = GetCurrentThreadId();
                if (fgThread != 0 && fgThread != myThread)
                    AttachThreadInput(fgThread, myThread, TRUE);
                SetForegroundWindow(hwnd);
                SetFocus(hwnd);
                if (fgThread != 0 && fgThread != myThread)
                    AttachThreadInput(fgThread, myThread, FALSE);
            }
            return 0;
        }
        if (wParam == VK_F6)
        {
            ui::LogPush("F6 收到：准备卸载 DLL");
            InterlockedExchange(&g_unloadRequested, 1);
            return 0;
        }
        if (wParam == VK_ESCAPE && g_menuVisible)
        {
            g_menuVisible = false;
            return 0;
        }
        break;
    case WM_SETCURSOR:
        if (g_menuVisible && LOWORD(lParam) == HTCLIENT)
        {
            POINT pt{};
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            if (InsidePanel(hwnd, pt))
            {
                SetCursor(nullptr);
                return TRUE;
            }
        }
        break;
    default:
        break;
    }
    // 关键：不再把消息转给 ImGui（那会往游戏的输入链路里塞 SetCapture 等副作用）
    (void)hwnd;
    (void)msg;
    (void)wParam;
    (void)lParam;
    return CallWindowProcW(g_originalWndProc, hwnd, msg, wParam, lParam);
}

HRESULT WINAPI HookedPresent(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags)
{
    static LARGE_INTEGER freq{};
    static LARGE_INTEGER lastEnd{};
    static bool lastValid = false;
    if (freq.QuadPart == 0)
        QueryPerformanceFrequency(&freq);
    LARGE_INTEGER tStart, tBeforeGame, tAfterGame;
    QueryPerformanceCounter(&tStart);

    static bool initTried = false;
    if (g_unloadRequested == 0 && !RenderInternalReady() && !initTried)
    {
        initTried = true;
        DXGI_SWAP_CHAIN_DESC desc = {};
        if (SUCCEEDED(swapChain->GetDesc(&desc)))
        {
            g_hwnd = desc.OutputWindow;
            if (RenderInternalInit(swapChain, g_hwnd))
            {
                if (!nl::SwitchFlag("no_wndproc"))
                {
                    g_originalWndProc = (WNDPROC)SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, (LONG_PTR)HookedWndProc);
                    FileLog("[dll] Present 挂钩后完成初始化，wndproc hooked=%d", g_originalWndProc != nullptr ? 1 : 0);
                }
                else
                {
                    FileLog("[dll] Present 挂钩后完成初始化（no_wndproc：不接管窗口过程）");
                }
            }
        }
    }

    if (RenderInternalReady() && g_unloadRequested == 0)
    {
        // 交换链换了（游戏重建 swapchain / 多窗口）时，用旧的渲染目标继续画会画出花屏甚至崩在驱动里
        if (RenderInternalSwapChain() != nullptr && swapChain != RenderInternalSwapChain())
        {
            static int warned = 0;
            if (warned < 3)
            {
                ++warned;
                FileLog("[dll] 交换链已变化（0x%p -> 0x%p），本帧跳过绘制",
                        (void*)RenderInternalSwapChain(), (void*)swapChain);
            }
        }
        else
        {
            // 软件光标只在菜单可见且鼠标在面板内时绘制，避免与游戏光标重叠
            ImGuiIO& io = ImGui::GetIO();
            if (g_menuVisible)
            {
                POINT cur{};
                GetCursorPos(&cur);
                ScreenToClient(g_hwnd, &cur);
                const ui::PanelRect r = ui::GetMenuPanelRect();
                io.MouseDrawCursor = (r.w > 0.0f && cur.x >= r.x && cur.x <= r.x + r.w && cur.y >= r.y && cur.y <= r.y + r.h);
            }
            else
            {
                io.MouseDrawCursor = false;
            }
            if (!nl::SwitchFlag("no_draw"))
                RenderInternalFrame(g_settings, g_menuVisible, ImGui::GetIO().DeltaTime);
            if (ui::MenuCloseRequested())
                g_menuVisible = false;
        }
    }

    // 卸载请求：先摘钩子，再做清理，最后交给独立线程 FreeLibrary
    if (g_unloadRequested != 0)
        DllPerformUnload();

    QueryPerformanceCounter(&tBeforeGame);
    const PresentFn original = reinterpret_cast<PresentFn>(g_originalPresent);
    const HRESULT hr = original ? original(swapChain, syncInterval, flags) : S_OK;
    QueryPerformanceCounter(&tAfterGame);

    // 性能遥测：我们自己的耗时 / 帧间隔 / 游戏 Present 的耗时
    {
        const double toMs = 1000.0 / (double)freq.QuadPart;
        const double hookMs = (double)(tBeforeGame.QuadPart - tStart.QuadPart) * toMs;
        const double gameMs = (double)(tAfterGame.QuadPart - tBeforeGame.QuadPart) * toMs;
        const double frameDt = lastValid ? (double)(tStart.QuadPart - lastEnd.QuadPart) * toMs : 0.0;
        lastEnd = tAfterGame;
        lastValid = true;
        RenderInternalPerfNoteHook(hookMs, frameDt, gameMs, swapChain, GetCurrentThreadId());
    }
    return hr;
}

HRESULT WINAPI HookedResizeBuffers(IDXGISwapChain* swapChain, UINT count, UINT width, UINT height, DXGI_FORMAT format, UINT flags)
{
    RenderInternalOnResize(swapChain);
    const ResizeFn original = reinterpret_cast<ResizeFn>(g_originalResize);
    return original ? original(swapChain, count, width, height, format, flags) : S_OK;
}

DWORD WINAPI UnloadThread(LPVOID param)
{
    // 等几帧，确保 Present 已经返回、没有人还在执行本模块代码
    Sleep(400);
    HMODULE mod = (HMODULE)param;
    if (!nl::IsLoaderModule())
    {
        // 手动映射（内核驱动注入）的镜像不在模块链表里，FreeLibrary 会失败/崩，
        // 钩子已经摘干净了，就让这块内存留驻
        nl::FileLog("[dll] 手动映射镜像：跳过 FreeLibrary，钩子已摘除，模块留驻");
        return 0;
    }
    FreeLibraryAndExitThread(mod, 0);
    return 0;
}
}

void DllSetModule(HMODULE mod) { g_module = mod; }
void DllDrainInput() { DrainInputToImGui(); }
bool DllHooksInstalled() { return g_swapChainHook.Count() > 0; }
bool DllMenuVisible() { return g_menuVisible; }
void DllSetMenuVisible(bool visible) { g_menuVisible = visible; }
void DllRequestUnload() { InterlockedExchange(&g_unloadRequested, 1); }
bool DllUnloadRequested() { return g_unloadRequested != 0; }

bool DllInstallHooks()
{
    // 一个进程里只允许挂一层：如果 Present 现在指向的是一个"不在模块链表里的镜像"
    // （= 另一个手动映射进来的实例，卸载不掉），就直接放弃本次挂载。
    // 这样既能挡住重复注入叠钩子，又不会像固定互斥体那样把 F6 之后的重注也一起挡掉。
    {
        void* current = nullptr;
        IDXGISwapChain* probe = nullptr;
        // 用临时设备+交换链取虚表（与下面同一套做法）
        DXGI_SWAP_CHAIN_DESC sd = {};
        sd.BufferCount = 1;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = GetForegroundWindow();
        if (sd.OutputWindow == nullptr)
            sd.OutputWindow = GetDesktopWindow();
        sd.SampleDesc.Count = 1;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        ID3D11Device* dev = nullptr;
        ID3D11DeviceContext* ctx = nullptr;
        D3D_FEATURE_LEVEL level{};
        const D3D_FEATURE_LEVEL want[] = { D3D_FEATURE_LEVEL_11_0 };
        if (SUCCEEDED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, want, 1,
                                                    D3D11_SDK_VERSION, &sd, &probe, &dev, &level, &ctx)) &&
            probe != nullptr)
        {
            void** vt = *reinterpret_cast<void***>(probe);
            current = vt[kVtablePresent];
            probe->Release();
            if (ctx) ctx->Release();
            if (dev) dev->Release();
        }
        if (current != nullptr)
        {
            HMODULE owner = nullptr;
            const BOOL inLoader = GetModuleHandleExA(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCSTR>(current), &owner);
            if (!inLoader)
            {
                FileLog("[dll] Present 已被另一个手动映射的实例接管（0x%p 不在模块链表里），本次不重复挂钩",
                        current);
                return false;
            }
        }
    }
    // 用一个临时设备+交换链拿到真正的 IDXGISwapChain 虚表（与游戏共用同一张表）
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"NLUIDummyWindowClass";
    RegisterClassExW(&wc);
    HWND dummy = CreateWindowExW(0, wc.lpszClassName, L"nlui-dummy", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);

    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 1;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = dummy;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    IDXGISwapChain* swapChain = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL obtained = D3D_FEATURE_LEVEL_11_0;
    const HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2,
                                                     D3D11_SDK_VERSION, &sd, &swapChain, &device, &obtained, &context);
    if (FAILED(hr))
    {
        FileLog("[dll] 临时设备创建失败 hr=0x%08lX", (unsigned long)hr);
        if (dummy) DestroyWindow(dummy);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return false;
    }

    bool ok = g_swapChainHook.Install(swapChain, kVtablePresent, reinterpret_cast<void*>(&HookedPresent), &g_originalPresent);
    if (ok)
        ok = g_swapChainHook.Install(swapChain, kVtableResizeBuffers, reinterpret_cast<void*>(&HookedResizeBuffers), &g_originalResize);

    context->Release();
    device->Release();
    swapChain->Release();
    if (dummy)
        DestroyWindow(dummy);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);

    FileLog("[dll] VMT hook installed=%d present=%p original=%p", ok ? 1 : 0,
            (void*)&HookedPresent, g_originalPresent);
    return ok;
}

void DllRemoveHooks()
{
    if (g_hwnd != nullptr && g_originalWndProc != nullptr)
    {
        SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, (LONG_PTR)g_originalWndProc);
        g_originalWndProc = nullptr;
    }
    g_swapChainHook.RemoveAll();
    FileLog("[dll] hooks removed");
}

void DllPerformUnload()
{
    static LONG once = 0;
    if (InterlockedExchange(&once, 1) != 0)
        return;
    ui::LogPush("unloading dll  ·  hooks -> imgui -> freellibrary");
    FileLog("[dll] unload begin");
    nl::CreateMoveRemove();
    nl::MiscShutdown();
    DllRemoveHooks();
    RenderInternalShutdown();
    FileLog("[dll] unload: hooks+imgui done, waiting for worker thread");
    HMODULE mod = g_module;
    if (mod != nullptr)
        CreateThread(nullptr, 0, UnloadThread, mod, 0, nullptr);
}
}
