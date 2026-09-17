#pragma once
#include <string>
namespace nl {
// 当前代码所在模块（EXE 或 LoadLibrary 注入的 DLL）的目录。
// 手动映射（manual map，比如内核驱动注入）时模块不在 PEB 模块链表里，
// 按地址反查会失败，这时返回 "."。
std::string ModuleDirectory();

// 主 EXE 目录（手动映射时拿它当兜底，通常就是游戏目录）
std::string HostDirectory();

// 读写数据（日志 / 配置 / offsets.ini）的目录：
//   1) 模块目录（正常 EXE、LoadLibrary 注入——行为与以前一致）
//   2) 手动映射时退回 %LOCALAPPDATA%\NeverloseUI（自动创建）
std::string DataDirectory();

// 只读查找：按 模块目录 -> 主 EXE 目录 -> 当前工作目录 -> %LOCALAPPDATA%\NeverloseUI
// 的顺序返回第一个存在的文件；都没有时返回模块目录下的路径（便于报错时看路径）。
std::string ResolveDataFile(const char* fileName);

// 本镜像是否在 PEB 模块链表里（正常 EXE / LoadLibrary 注入 = true）。
// 手动映射的镜像为 false —— 这时不能调 FreeLibraryAndExitThread，只能摘完钩子留驻。
bool IsLoaderModule();

// 调试开关：读数据目录下的 nl_switch.ini（键=1 生效），用于二分定位问题：
//   no_hooks / no_wndproc / no_draw / no_tick / dual_box
bool SwitchFlag(const char* name);

// 同上，但取整数：nl_switch.ini 里写 key=3 就返回 3；没写返回 def
int  SwitchInt(const char* name, int def);
}
