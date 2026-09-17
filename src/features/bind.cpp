#include "features/bind.h"
#include "features/input.h"

#include <windows.h>
#include <cstdio>
#include <cstring>

namespace nl {
namespace {

// Toggle 模式需要"上一次状态"来检测边沿：按 VK 记录一份即可（同一个键绑到多处时共享状态，
// 这在实际使用里没问题，反而更符合直觉）。
struct ToggleState {
    int  vk = 0;
    bool state = false;
    bool lastDown = false;
};
ToggleState g_toggles[32];

ToggleState& ToggleFor(int vk)
{
    for (auto& t : g_toggles)
        if (t.vk == vk)
            return t;
    for (auto& t : g_toggles)
        if (t.vk == 0)
        {
            t.vk = vk;
            return t;
        }
    return g_toggles[0];   // 极端情况：退化成共用第一个
}

bool RawDown(int vk)
{
    // 真人按键优先（低级钩子），没有钩子信息时退回系统状态
    if (input::PhysicalKeyHeld(vk))
        return true;
    return input::PhysicalDown(vk);
}

} // namespace

int BindVk(int binding) { return binding & 0xFFFF; }
int BindModeOf(int binding) { return (binding >> 16) & 0xFF; }
int BindMake(int vk, int mode) { return (vk & 0xFFFF) | ((mode & 0xFF) << 16); }

bool BindHasKey(int binding)
{
    const int mode = BindModeOf(binding);
    const int vk = BindVk(binding);
    return mode == BindMode_Always || vk != 0;
}

bool BindActive(int binding)
{
    const int mode = BindModeOf(binding);
    const int vk = BindVk(binding);
    if (mode == BindMode_Always)
        return true;
    if (vk == 0)
        return false;              // 没绑键又不是 Always → 不生效
    const bool down = RawDown(vk);
    if (mode == BindMode_Toggle)
    {
        ToggleState& t = ToggleFor(vk);
        if (down && !t.lastDown)
            t.state = !t.state;    // 只在"刚按下"那一次翻转
        t.lastDown = down;
        return t.state;
    }
    return down;                   // Hold
}

const char* BindVkName(int vk)
{
    static char buf[32];
    switch (vk)
    {
    case 0: return "none";
    case VK_LBUTTON: return "Mouse1";
    case VK_RBUTTON: return "Mouse2";
    case VK_MBUTTON: return "Mouse3";
    case VK_XBUTTON1: return "Mouse4";
    case VK_XBUTTON2: return "Mouse5";
    case VK_SPACE: return "Space";
    case VK_SHIFT: return "Shift";
    case VK_CONTROL: return "Ctrl";
    case VK_MENU: return "Alt";
    case VK_TAB: return "Tab";
    case VK_CAPITAL: return "CapsLock";
    case VK_INSERT: return "Insert";
    case VK_DELETE: return "Delete";
    case VK_HOME: return "Home";
    case VK_END: return "End";
    case VK_PRIOR: return "PageUp";
    case VK_NEXT: return "PageDown";
    case VK_OEM_3: return "`";
    case VK_OEM_MINUS: return "-";
    case VK_OEM_PLUS: return "=";
    case VK_OEM_4: return "[";
    case VK_OEM_5: return "\\";
    case VK_OEM_6: return "]";
    case VK_OEM_1: return ";";
    case VK_OEM_7: return "'";
    case VK_OEM_COMMA: return ",";
    case VK_OEM_PERIOD: return ".";
    case VK_OEM_2: return "/";
    default: break;
    }
    if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z'))
    {
        snprintf(buf, sizeof(buf), "%c", (char)vk);
        return buf;
    }
    if (vk >= VK_F1 && vk <= VK_F24)
    {
        snprintf(buf, sizeof(buf), "F%d", vk - VK_F1 + 1);
        return buf;
    }
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9)
    {
        snprintf(buf, sizeof(buf), "Num%d", vk - VK_NUMPAD0);
        return buf;
    }
    snprintf(buf, sizeof(buf), "0x%02X", vk);
    return buf;
}

} // namespace nl
