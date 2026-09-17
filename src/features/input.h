#pragma once
namespace nl {
// ---------------------------------------------------------------------------
//  输入模拟层
//
//  用途：把"按 W/A/S/D、空格、鼠标左右键、鼠标移动"这些操作，以最接近真实硬件的
//  方式送给游戏（SendInput + 扫描码）。这样 bhop / autoStrafe / triggerbot /
//  autoPistol / autoPeek 这些功能不需要 CreateMove 钩子就能生效。
//
//  设计要点：
//   * 只在"状态变化"时真正 SendInput，避免每帧刷屏（很多游戏会把连发当作异常输入）；
//   * 自己维护一份"逻辑按键状态"，功能之间不会互相踩；
//   * 注入版与外部版都能用（外部版窗口不是焦点时游戏收不到，属于预期行为）。
// ---------------------------------------------------------------------------
namespace input {

// 逻辑键：用 Windows 虚拟键码
void KeyDown(int vk);
void KeyUp(int vk);
void KeyToggle(int vk, bool down);
bool KeyDownNow(int vk);          // 我们这边最后一次"按下"没释放
void MouseLeft(bool down);
void MouseRight(bool down);
void MouseMove(int dx, int dy);

// 物理按键状态（游戏里真实的按键，与我们模拟的无关）
bool PhysicalDown(int vk);

// 一次性清空我们按下的所有键/鼠标（退出、关功能时调用，避免卡键）
void ReleaseAll();

// 每帧调用：处理"自动释放"（例如长按超过 N 帧的键）
void Tick();

// ---------------------------------------------------------------------------
//  真人按键钩子（低级键盘钩子 WH_KEYBOARD_LL）
//
//  为什么需要它：GetAsyncKeyState 分不清"用户按的"和"我们注入的"。
//  连续重按时我们一直在注入，采样窗口被自己占满 → 用户松手也检测不到 → 一直跳（实测踩到）。
//  低级钩子会给出 LLKHF_INJECTED 标志，可以只统计**真人**的按下/抬起事件。
// ---------------------------------------------------------------------------
void StartPhysicalKeyHook();
bool PhysicalKeyHeld(int vk);            // 真人是否按着（不含注入）
unsigned long long PhysicalReleaseCount(int vk);   // 真人抬起次数（用于"松手立刻停"）
void StopPhysicalKeyHook();

// 调试统计
int  SimulatedKeyCount();
const char* LastAction();
}
}
