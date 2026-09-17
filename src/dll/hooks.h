#pragma once
#include <windows.h>
namespace nl {
bool DllInstallHooks();
void DllRemoveHooks();
bool DllHooksInstalled();
void DllRequestUnload();
bool DllUnloadRequested();
void DllPerformUnload();
void DllSetMenuVisible(bool visible);
bool DllMenuVisible();
void DllSetModule(HMODULE mod);
// 把窗口过程记录下来的鼠标/键盘事件喂给 ImGui（在 ImGui::NewFrame 之前调用）
void DllDrainInput();
}
