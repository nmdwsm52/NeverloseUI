#include "features/input.h"
#include "core/log.h"
#include "core/paths.h"

#include <windows.h>
#include <cstring>
#include <cstdio>

namespace nl {
namespace input {
namespace {

constexpr int kMaxKeys = 256;
bool  g_simDown[kMaxKeys] = {};
bool  g_mouseLeftDown = false;
bool  g_mouseRightDown = false;
int   g_sendCount = 0;
char  g_lastAction[96] = { 0 };

void Note(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_lastAction, sizeof(g_lastAction), fmt, ap);
    va_end(ap);
}

// 虚拟键 -> 扫描码（游戏基本都按扫描码认键，比 vk 可靠）
WORD ScanOf(int vk)
{
    const UINT sc = MapVirtualKeyA((UINT)vk, MAPVK_VK_TO_VSC);
    return (WORD)(sc & 0xFF);
}

bool SendKey(WORD scan, bool down)
{
    INPUT in = {};
    in.type = INPUT_KEYBOARD;
    in.ki.wScan = scan;
    in.ki.dwFlags = KEYEVENTF_SCANCODE | (down ? 0 : KEYEVENTF_KEYUP);
    const UINT sent = SendInput(1, &in, sizeof(in));
    if (sent == 1)
        ++g_sendCount;
    return sent == 1;
}

bool SendMouse(DWORD flag)
{
    INPUT in = {};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = flag;
    const UINT sent = SendInput(1, &in, sizeof(in));
    if (sent == 1)
        ++g_sendCount;
    return sent == 1;
}

} // namespace

void KeyDown(int vk)
{
    if (vk <= 0 || vk >= kMaxKeys)
        return;
    if (g_simDown[vk])
        return;
    g_simDown[vk] = true;
    SendKey(ScanOf(vk), true);
    Note("key down vk=%d", vk);
}

void KeyUp(int vk)
{
    if (vk <= 0 || vk >= kMaxKeys)
        return;
    if (!g_simDown[vk])
        return;
    g_simDown[vk] = false;
    SendKey(ScanOf(vk), false);
    Note("key up vk=%d", vk);
}

void KeyToggle(int vk, bool down)
{
    if (down)
        KeyDown(vk);
    else
        KeyUp(vk);
}

bool KeyDownNow(int vk)
{
    return (vk > 0 && vk < kMaxKeys) ? g_simDown[vk] : false;
}

void MouseLeft(bool down)
{
    if (down == g_mouseLeftDown)
        return;
    g_mouseLeftDown = down;
    SendMouse(down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP);
    Note("mouse left %s", down ? "down" : "up");
}

void MouseRight(bool down)
{
    if (down == g_mouseRightDown)
        return;
    g_mouseRightDown = down;
    SendMouse(down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP);
    Note("mouse right %s", down ? "down" : "up");
}

void MouseMove(int dx, int dy)
{
    if (dx == 0 && dy == 0)
        return;
    INPUT in = {};
    in.type = INPUT_MOUSE;
    in.mi.dx = dx;
    in.mi.dy = dy;
    in.mi.dwFlags = MOUSEEVENTF_MOVE;
    if (SendInput(1, &in, sizeof(in)) == 1)
        ++g_sendCount;
    Note("mouse move %d,%d", dx, dy);
}

bool PhysicalDown(int vk)
{
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

void ReleaseAll()
{
    bool any = false;
    for (int vk = 1; vk < kMaxKeys; ++vk)
    {
        if (g_simDown[vk])
        {
            g_simDown[vk] = false;
            SendKey(ScanOf(vk), false);
            any = true;
        }
    }
    if (g_mouseLeftDown)
    {
        g_mouseLeftDown = false;
        SendMouse(MOUSEEVENTF_LEFTUP);
        any = true;
    }
    if (g_mouseRightDown)
    {
        g_mouseRightDown = false;
        SendMouse(MOUSEEVENTF_RIGHTUP);
        any = true;
    }
    if (any)
        Note("release all");
}

void Tick()
{
    // 目前仅用于统计；各功能自己负责按下/抬起
}

// ---------------------------------------------------------------------------
//  低级键盘钩子：只统计"真人"按键（带 LLKHF_INJECTED 的是我们/别的工具注入的，不算）
// ---------------------------------------------------------------------------
namespace {

HHOOK               g_kbHook = nullptr;
DWORD               g_kbThread = 0;
volatile LONG       g_stopHook = 0;
bool                g_physHeld[256] = {};
volatile LONG       g_physReleases[256] = {};

LRESULT CALLBACK LowLevelKeyboardProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HC_ACTION && lParam != 0)
    {
        const KBDLLHOOKSTRUCT* kb = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
        const bool injected = (kb->flags & LLKHF_INJECTED) != 0;
        // 测试开关：把注入事件也当真人（默认关；只有 nl_switch.ini 里写 fake_physical=1 才生效）
        const bool treatAsPhysical = nl::SwitchFlag("fake_physical");
        const int vk = (int)kb->vkCode;
        if ((!injected || treatAsPhysical) && vk > 0 && vk < 256)
        {
            if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)
                g_physHeld[vk] = true;
            else if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP)
            {
                g_physHeld[vk] = false;
                InterlockedIncrement(&g_physReleases[vk]);
            }
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

DWORD WINAPI HookThread(LPVOID)
{
    g_kbHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, nullptr, 0);
    if (g_kbHook == nullptr)
    {
        FileLog("[input] 低级键盘钩子安装失败 err=%lu", GetLastError());
        return 0;
    }
    FileLog("[input] 低级键盘钩子已安装（只认真人按键，注入事件带 LLKHF_INJECTED 会被忽略）");
    MSG msg;
    while (InterlockedCompareExchange(&g_stopHook, 0, 0) == 0 && GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    UnhookWindowsHookEx(g_kbHook);
    g_kbHook = nullptr;
    FileLog("[input] 低级键盘钩子已卸载");
    return 0;
}

} // namespace

void StartPhysicalKeyHook()
{
    if (g_kbThread != 0)
        return;
    g_kbThread = 1;   // 先占位，避免重复启动
    HANDLE h = CreateThread(nullptr, 0, HookThread, nullptr, 0, nullptr);
    if (h != nullptr)
        CloseHandle(h);
}

bool PhysicalKeyHeld(int vk)
{
    return (vk > 0 && vk < 256) ? g_physHeld[vk] : false;
}

unsigned long long PhysicalReleaseCount(int vk)
{
    return (vk > 0 && vk < 256) ? (unsigned long long)InterlockedCompareExchange(&g_physReleases[vk], 0, 0) : 0;
}

void StopPhysicalKeyHook()
{
    InterlockedExchange(&g_stopHook, 1);
    if (g_kbThread != 0)
    {
        PostThreadMessageW(g_kbThread, WM_QUIT, 0, 0);   // 线程 id 与句柄不同，这里只做尽力唤醒
        g_kbThread = 0;
    }
}

int SimulatedKeyCount() { return g_sendCount; }
const char* LastAction() { return g_lastAction; }

} // namespace input
} // namespace nl
