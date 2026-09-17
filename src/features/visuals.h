#pragma once
#include "features/config.h"
namespace nl {
// ---------------------------------------------------------------------------
//  世界/玩家视觉（能直接改字段的那一批）
//
//  已实现：
//    * radarHack    —— 给敌人写 m_bSpotted / m_bSpottedByMask，小地图直接显示
//    * glow         —— 写 CGlowProperty：m_bGlowing=1 + m_glowColorOverride（描边发光）
//    * fovOverride  —— 写本地玩家的相机 FOV
//  这些都不需要材质系统，直接改 schema 字段即可（偏移可在 offsets.ini 里改）。
//
//  还没做（需要引擎/材质接口）：chams（材质覆盖）、nightMode（光照/雾）、
//  removeSmoke（粒子）、removeScope（覆盖层）、bulletImpacts。
// ---------------------------------------------------------------------------
struct VisualsStats {
    int  spottedWrites = 0;
    int  glowingWrites = 0;
    int  glowTargets = 0;
    int  chamsWrites = 0;
    int  chamsTargets = 0;
    bool glowChecked = false;      // 是否校验过 glow 偏移是否可信
    bool glowOffsetOk = false;
    bool fovApplied = false;
    float lastFov = 0.0f;
};
void VisualsTick(const Settings& s);
const VisualsStats& VisualsStatsGet();
// 关闭/卸载时把写过的字段恢复原状
void VisualsRestore();
}

