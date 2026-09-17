#pragma once
#include "features/config.h"
namespace nl {
// ---------------------------------------------------------------------------
//  Aimbot（静默 / 写命令角度版）
//
//  链路已经全部打通：
//    * GamePlayerList() 提供每个敌人的**关节世界坐标**（bonesWorld）+ 可见性/队伍/血量；
//    * CreateMove 钩子里我们能改命令的 +0x18/+0x1C（角度）与 +0x60（按键位）。
//  所以这里做的是"真静默"：只在**发给服务器的命令**里把角度改成瞄向目标的解，
//  客户端相机完全不动 —— 自己屏幕上看不出任何异常。
//
//  已实现：目标选择（FOV / 最近准星 / 最近距离 / 最低血量）、部位（头/脖/胸/骨盆）、
//          自动开火（写 IN_ATTACK）、开关与按键（rage.enable / rage.key）。
//  未实现：命中率(hitchance)、伤害与穿透(mindamage)、自动开墙(autowall)、
//          后坐力补偿、回溯(backtrack) —— 需要弹道/伤害模型，属于下一批。
// ---------------------------------------------------------------------------
struct AimTarget {
    bool  valid = false;
    int   index = -1;          // 目标在玩家列表里的下标
    int   joint = -1;          // 瞄的关节（Bone_*）
    float pitch = 0.0f;
    float yaw = 0.0f;
    float fovDist = 9999.0f;   // 与当前准星的夹角（度）
    float dist = 0.0f;         // 与本地玩家的距离（米）
    bool  visible = false;
    const char* name = "";
};

// 每帧调用（GameTick 末尾）：刷新当前目标
void AimbotTick(const Settings& s, float dt);
// CreateMove 里调用（原函数返回后）：把角度写进命令；返回 true 表示改写了角度
bool AimbotApplyToCmd(void* cmd, const float* viewAngles, bool* outFire);
const AimTarget& AimbotTargetGet();
bool AimbotKeyHeld();
// 调试开关：每秒打一行目标信息
void AimbotSetLog(bool on);
}
