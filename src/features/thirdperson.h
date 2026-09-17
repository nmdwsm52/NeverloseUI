#pragma once
#include "features/config.h"
#include <cstdint>
namespace nl {
// ---------------------------------------------------------------------------
//  第三人称（写状态字节，不调用游戏函数）
//
//  来源：cs2-sdk 的签名表里 `ThirdPersonOffHandler` 的字节模式自己暴露了状态存放位置：
//      mov  rax, [rip+disp32]        ; 取全局对象
//      mov  byte [rax+disp32], 0     ; 关掉第三人称
//  也就是说：**全局对象 + 某个偏移的一个字节 = 第三人称开关**。
//  所以做法是：扫出这段代码 → 解出它引用的全局 + 那个字节偏移 → 我们自己往那儿写 1/0。
//  好处是完全不依赖"能不能执行控制台命令"，也不会调用签名不明的游戏函数。
//
//  安全性：模式扫不到（版本变了）就整块跳过并打日志，绝不用猜的偏移去写。
// ---------------------------------------------------------------------------
void ThirdPersonInit(uintptr_t clientBase, size_t clientSize);
void ThirdPersonTick(const Settings& s);
struct ThirdPersonStats {
    bool  resolved = false;
    uintptr_t flagAddr = 0;      // 开关字节的绝对地址
    int   writes = 0;
    bool  enabled = false;
    const char* note = "未初始化";
};
const ThirdPersonStats& ThirdPersonStatsGet();
}
