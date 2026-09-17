#pragma once
#include <cstdint>

namespace nl {
// ---------------------------------------------------------------------------
//  CreateMove 钩子
//
//  目标：拿到每个 tick 的 CUserCmd，才能做"改按键位/写角度"这类必须落在命令上的功能
//  （精确 bhop、anti-aim、silent aim）。落点来自调试器侦察：
//    * CCSGOInput 对象 = client.dll + dwCSGOInput（内联全局对象，首字段是虚表指针）
//    * 虚表索引 kCreateMoveIndex（默认 5）那一支既读 dwViewAngles 又含补命令逻辑
//  探针阶段（当前）只记录参数与调用频率，不改任何游戏状态；
//  确认无误后再把功能接进 CreateMoveOnTick()。
// ---------------------------------------------------------------------------
struct CreateMoveStats {
    bool  installed = false;
    int   calls = 0;
    int   lastSlot = -1;
    void* lastSelf = nullptr;
    void* lastArg3 = nullptr;    // 按 (this, slot, cmd) 约定就是 CUserCmd*
    void* lastArg4 = nullptr;
    uint32_t lastCmdDwords[8] = {};   // cmd 开头 32 字节（用来判断像不像命令对象）
    int   callsPerSec = 0;
};

// 安装 / 卸载（clientBase 由调用方传入，内部换算 dwCSGOInput 偏移）
bool CreateMoveInstall(uintptr_t clientBase);
void CreateMoveRemove();
bool CreateMoveHooked();
const CreateMoveStats& CreateMoveStatsGet();
struct CmdBhopStats { bool applied = false; int jumpWrites = 0; int lastButtons = 0; };
CmdBhopStats CreateMoveBhopStats();
}

