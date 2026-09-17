#pragma once
#include <imgui.h>
#include "features/config.h"
#include "features/bonemap.h"
namespace nl {
// 关节枚举（Bone_Head .. Bone_FootR）与骨骼索引分类器都在 features/bonemap.h 里，
// 这里只保留"屏幕上的一名玩家"这个数据契约。
// 一名玩家在屏幕上的几何信息。
// 现在由预览面板填充；接入真实实体后由世界坐标 -> 视矩阵投影填充，绘制逻辑完全复用。
struct PlayerView {
    uintptr_t ent = 0;               // 这个玩家 Pawn 的实体地址（视觉功能要写字段）
    ImVec2 head = ImVec2(0, 0);      // 头顶屏幕坐标
    ImVec2 feet = ImVec2(0, 0);      // 脚底屏幕坐标
    float  headWorld[3] = { 0, 0, 0 };   // 头顶世界坐标（排查"框脱离人物"要用它换矩阵重投影）
    float  feetWorld[3] = { 0, 0, 0 };   // 脚底世界坐标
    bool   hasWorld = false;
    ImVec2 bones[Bone_Count] = {};
    float  bonesWorld[Bone_Count][3] = {};   // 关节世界坐标（aimbot 解算角度要用）
    bool   bonesValid[Bone_Count] = {};   // 未推导出来的骨骼不要画线（否则会连到 (0,0)）
    bool   hasBones = false;
    float  health = 100.0f;
    float  armor = 0.0f;             // 0..100
    bool   helmet = false;
    float  distance = 430.0f;
    int    team = 2;
    bool   visible = true;
    bool   defuser = false;
    bool   scoped = false;
    bool   reloading = false;
    float  flashed = 0.0f;           // 0..1
    int    money = -1;               // -1 = 未知
    int    ping = -1;
    float  ammo = 30.0f;             // 当前弹匣
    int    ammoClip = -1;            // -1 = 未知
    const char* name = "player";
    const char* weapon = "AK-47";
};

// 已安放的 C4（给顶部的炸弹计时面板用）
struct BombInfo {
    bool  valid = false;
    bool  ticking = false;
    bool  defused = false;
    bool  beingDefused = false;
    int   site = -1;
    float blowTime = 0.0f;
    float timerLength = 40.0f;
    float defuseEndTime = 0.0f;
    char  defuserName[32] = { 0 };
};
const BombInfo& BombGet();
BombInfo& BombMutable();
// 本地 pawn 的 m_flSimulationTime（当作"当前服务器时间"用）
float GameLocalSimulationTime();
void DrawPlayerOverlay(ImDrawList* dl, const PlayerView& p, const VisualConfig& v, float alpha = 1.0f);
void DrawPlayerPreview(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, const VisualConfig& v, float time, float alpha);
void DrawWatermark(const Settings& s, float time);
// 未来的真实玩家遍历入口（世界坐标 -> 屏幕坐标 -> DrawPlayerOverlay）
void OnOverlayTick(const Settings& s, float time);
}


