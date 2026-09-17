#pragma once
#include <cstdint>
namespace nl {
// ---------------------------------------------------------------------------
//  ConVar（引擎控制台变量）访问
//
//  很多功能（静音敌人、视野、各种调试开关）本质就是"改一个 convar"，比改内存安全得多。
//  ConVar_t 的布局是调试器实测出来的（用 dwSensitivity 那个 convar 验证）：
//
//      +0x00  const char* name        -> "sensitivity"
//      +0x20  const char* description -> "Mouse sensitivity."
//      +0x28  uint32 flags
//      +0x58  float value             -> 0.95
//
//  查找方式：在有写权限的模块数据里找"某个 qword 指向的字符串 == 目标名"，再校验
//  +0x20 也是一个字符串指针、+0x58 是个合理浮点 —— 三个条件都满足才认。
//  找到后结果会缓存，避免每帧扫描。
// ---------------------------------------------------------------------------
uintptr_t ConVarFind(const char* name);                 // 0 = 没找到
bool  ConVarGetFloat(const char* name, float* out);
bool  ConVarSetFloat(const char* name, float value);
bool  ConVarGetInt(const char* name, int* out);
bool  ConVarSetInt(const char* name, int value);
// 自检：用已知的 sensitivity 验证整套机制是否可用（启动时打一行日志）
void  ConVarSelfTest();
// 批量探测：对一批候选 convar 打印「是否存在 / 当前值 / flags」，用于判断功能可行性
void  ConVarProbe(const char* const* names, int count);
// 调试统计
struct ConVarStats {
    int found = 0;
    int writes = 0;
    bool selfTestOk = false;
    const char* lastError = "";
};
const ConVarStats& ConVarStatsGet();
}
