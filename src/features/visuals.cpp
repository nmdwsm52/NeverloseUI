#include "features/visuals.h"
#include "features/esp.h"
#include "features/game.h"
#include "core/log.h"
#include "core/memory.h"
#include "core/paths.h"
#include "features/convar.h"

#include <windows.h>
#include <cmath>

namespace nl {
namespace {

VisualsStats g_stats;
// 我们改过的实体，用于关闭时恢复
uintptr_t g_glowTouched[16] = {};
int       g_glowTouchedCount = 0;

void RememberGlow(uintptr_t ent)
{
    for (int i = 0; i < g_glowTouchedCount; ++i)
        if (g_glowTouched[i] == ent)
            return;
    if (g_glowTouchedCount < (int)(sizeof(g_glowTouched) / sizeof(g_glowTouched[0])))
        g_glowTouched[g_glowTouchedCount++] = ent;
}

// 写"被点亮"：小地图/雷达可见（这两个字段是 schema 里的，写进去游戏自己就认）
void ApplyRadar(uintptr_t ent)
{
    const uintptr_t stateOff = Off("m_entitySpottedState");
    const uintptr_t bSpotted = Off("m_bSpotted");
    bool wrote = false;
    if (stateOff != 0 && bSpotted != 0)
    {
        const uint8_t one = 1;
        wrote = g_mem.Write(stateOff + ent + bSpotted, one) || wrote;
    }
    const uintptr_t maskOff = Off("m_bSpottedByMask");
    if (maskOff != 0)
    {
        const uint32_t all = 0xFFFFFFFFu;
        wrote = g_mem.Write(ent + maskOff, all) || wrote;
    }
    if (wrote)
        ++g_stats.spottedWrites;
}

// 校验 glow 偏移是否可信：m_bGlowing 必须读到 0/1，颜色必须是 0 或合理值。
// 校验不过就绝不写 —— 防止偏移过期把游戏写崩。
bool GlowOffsetLooksSane(uintptr_t ent)
{
    const uintptr_t glowOff = Off("m_Glow");
    const uintptr_t glowingOff = Off("m_bGlowing");
    const uintptr_t colorOff = Off("m_glowColorOverride");
    if (glowOff == 0 || glowingOff == 0 || colorOff == 0)
        return false;
    const uintptr_t glow = ent + glowOff;
    const uint8_t glowing = g_mem.Read<uint8_t>(glow + glowingOff);
    if (glowing > 1)
        return false;
    const uint32_t color = g_mem.Read<uint32_t>(glow + colorOff);
    // 颜色要么是 0（没设过），要么是个正常 ARGB（这里只要求不是明显垃圾值）
    if (color != 0 && (color & 0xFF000000u) == 0)
        return false;
    return true;
}

// chams：写 C_BaseModelEntity::m_clrRender（schema 实测 0xC98，Color = 4 字节 R,G,B,A）。
// 写之前校验原值像不像颜色（避免偏移过期写坏内存），并记住原值以便关掉时恢复。
uintptr_t g_chamsTouched[16] = {};
uint32_t  g_chamsOrig[16] = {};
int       g_chamsTouchedCount = 0;

bool ChamsOffsetLooksSane(uintptr_t ent)
{
    const uintptr_t off = Off("m_clrRender");
    if (off == 0)
        return false;
    const uint32_t cur = g_mem.Read<uint32_t>(ent + off);
    // 原始渲染色通常是 0xFFFFFFFF（白）或全 0，任何一种都算合理
    return cur == 0xFFFFFFFFu || cur == 0x00000000u || (cur & 0xFF000000u) != 0;
}

void ApplyChams(const Settings& s, uintptr_t ent)
{
    const uintptr_t off = Off("m_clrRender");
    if (off == 0)
        return;
    if (!ChamsOffsetLooksSane(ent))
    {
        static bool warned = false;
        if (!warned)
        {
            warned = true;
            FileLog("[vis] m_clrRender 偏移校验失败（0x%llX），跳过 chams", (unsigned long long)off);
        }
        return;
    }
    if (g_chamsTouchedCount < (int)(sizeof(g_chamsTouched) / sizeof(g_chamsTouched[0])))
    {
        bool known = false;
        for (int i = 0; i < g_chamsTouchedCount; ++i)
            if (g_chamsTouched[i] == ent)
                known = true;
        if (!known)
        {
            g_chamsTouched[g_chamsTouchedCount] = ent;
            g_chamsOrig[g_chamsTouchedCount] = g_mem.Read<uint32_t>(ent + off);
            ++g_chamsTouchedCount;
        }
    }
    const float* c = s.vis.chamsColor;      // float[4] RGBA 0..1
    const uint32_t r = (uint32_t)(255.0f * c[0]);
    const uint32_t g = (uint32_t)(255.0f * c[1]);
    const uint32_t b = (uint32_t)(255.0f * c[2]);
    const uint32_t a = (uint32_t)(255.0f * (c[3] > 0.0f ? c[3] : 1.0f));
    const uint32_t packed = r | (g << 8) | (b << 16) | (a << 24);
    if (g_mem.Write(ent + off, packed))
        ++g_stats.chamsWrites;
}
void ApplyGlow(const Settings& s, uintptr_t ent)
{
    const uintptr_t glowOff = Off("m_Glow");
    const uintptr_t glowingOff = Off("m_bGlowing");
    const uintptr_t colorOff = Off("m_glowColorOverride");
    const uintptr_t glow = ent + glowOff;

    if (!g_stats.glowChecked)
    {
        g_stats.glowChecked = true;
        g_stats.glowOffsetOk = GlowOffsetLooksSane(ent);
        FileLog("[vis] glow 偏移校验：%s（m_Glow=0x%llX m_bGlowing=0x%llX color=0x%llX）",
                g_stats.glowOffsetOk ? "通过" : "不通过，跳过 glow（偏移可能已过期）",
                (unsigned long long)glowOff, (unsigned long long)glowingOff, (unsigned long long)colorOff);
    }
    if (!g_stats.glowOffsetOk)
        return;

    // 颜色：UI 里是 ImVec4（0..1 的 RGBA），写成 ARGB
    const float* c = s.vis.glowColor;              // float[4]：R,G,B,A（0..1）
    const uint32_t a = (uint32_t)(255.0f * (c[3] > 0.0f ? c[3] : 1.0f));
    const uint32_t r = (uint32_t)(255.0f * c[0]);
    const uint32_t g = (uint32_t)(255.0f * c[1]);
    const uint32_t b = (uint32_t)(255.0f * c[2]);
    const uint32_t argb = (a << 24) | (r << 16) | (g << 8) | b;

    const uint8_t one = 1;
    if (g_mem.Write(glow + glowingOff, one) && g_mem.Write(glow + colorOff, argb))
    {
        ++g_stats.glowingWrites;
        RememberGlow(ent);
    }
}

// FOV：写本地玩家相机服务里的 m_iFOV
void ApplyFov(const Settings& s)
{
    const LocalState& st = GameLocalState();
    if (!st.valid || st.pawn == 0)
        return;
    const uintptr_t camOff = Off("m_pCameraServices");
    const uintptr_t fovOff = Off("m_iFOV");
    if (camOff == 0 || fovOff == 0)
        return;
    const uintptr_t cam = g_mem.Read<uintptr_t>(st.pawn + camOff);
    if (cam == 0 || !g_mem.IsValidRange(cam + fovOff, 4))
        return;

    // 开镜时绝不覆盖：狙击枪开镜是"变焦 FOV"，我们每帧写回去会和游戏打架 → 开镜瞬间闪一下
    const bool scoped = GameLocalState().scoped;
    if (s.vis.fovOverride > 1.0f && !scoped)
    {
        const int want = (int)(s.vis.fovOverride + 0.5f);
        const int cur = g_mem.Read<int>(cam + fovOff);
        if (cur != want)
        {
            if (g_mem.Write(cam + fovOff, want))
            {
                g_stats.fovApplied = true;
                g_stats.lastFov = s.vis.fovOverride;
                FileLog("[vis] FOV = %d（原来 %d）", want, cur);
            }
        }
    }
}

// 用 convar 实现的三项（探测确认这些 convar 在本构建里存在）：
//   viewmodel_fov     —— 手模视野（跟着 fovOverride 一起设）
//   cl_radar_scale    —— 雷达缩放
//   cl_clanid         —— 战队标签（只能指定"战队 id"，CS2 没有自定义文本的入口）
void ApplyConVarFeatures(const Settings& s)
{
    // 手模 FOV：只在用户设了主 FOV 时同步，避免擅自改他的手模设置
    if (s.vis.fovOverride > 1.0f)
    {
        static float lastVmf = 0.0f;
        if (fabsf(lastVmf - s.vis.fovOverride) > 0.5f)
        {
            if (ConVarSetFloat("viewmodel_fov", s.vis.fovOverride))
            {
                lastVmf = s.vis.fovOverride;
                FileLog("[vis] viewmodel_fov = %.0f", (double)s.vis.fovOverride);
            }
        }
    }

    // 雷达缩放
    if (s.vis.radarHack)
    {
        static float lastScale = 0.0f;
        if (fabsf(lastScale - s.vis.radarScale) > 0.01f)
        {
            if (ConVarSetFloat("cl_radar_scale", s.vis.radarScale))
            {
                lastScale = s.vis.radarScale;
                FileLog("[vis] cl_radar_scale = %.2f", (double)s.vis.radarScale);
            }
        }
    }

    // 战队标签：cl_clanid 只能填"战队 id"。文本框里是纯数字才写，否则如实说明限制。
    static int lastClan = -2;
    const int want = s.misc.clantag ? atoi(s.misc.clantagText) : 0;
    if (want != lastClan)
    {
        const bool numeric = s.misc.clantagText[0] != 0 && want != 0;
        if (!s.misc.clantag)
        {
            if (ConVarSetInt("cl_clanid", 0))
            {
                lastClan = 0;
                FileLog("[vis] cl_clanid 已清零（关闭战队标签）");
            }
        }
        else if (numeric)
        {
            if (ConVarSetInt("cl_clanid", want))
            {
                lastClan = want;
                FileLog("[vis] cl_clanid = %d", want);
            }
        }
        else
        {
            lastClan = want;
            FileLog("[vis] 战队标签：CS2 只认 cl_clanid（数字战队 id），不支持自定义文本“%s”", s.misc.clantagText);
        }
    }
}
} // namespace

void VisualsTick(const Settings& s)
{
    const LocalState& st = GameLocalState();
    if (!st.valid)
        return;

    for (const PlayerView& p : GamePlayerList())
    {
        if (p.team == st.team || p.health <= 0.0f)
            continue;
        const uintptr_t ent = p.ent;
        if (ent == 0)
            continue;
        if (s.vis.radarHack)
            ApplyRadar(ent);
        if (s.vis.glow)
        {
            ApplyGlow(s, ent);
            ++g_stats.glowTargets;
        }
        if (s.vis.chams)
        {
            ApplyChams(s, ent);
            ++g_stats.chamsTargets;
        }
    }

    ApplyFov(s);
    ApplyConVarFeatures(s);
}

void VisualsRestore()
{
    // chams 恢复原渲染色
    const uintptr_t chamsOff = Off("m_clrRender");
    if (chamsOff != 0)
    {
        for (int i = 0; i < g_chamsTouchedCount; ++i)
            g_mem.Write(g_chamsTouched[i] + chamsOff, g_chamsOrig[i]);
        g_chamsTouchedCount = 0;
    }
    if (!g_stats.glowOffsetOk)
        return;
    const uintptr_t glowOff = Off("m_Glow");
    const uintptr_t glowingOff = Off("m_bGlowing");
    const uint8_t zero = 0;
    for (int i = 0; i < g_glowTouchedCount; ++i)
        g_mem.Write(g_glowTouched[i] + glowOff + glowingOff, zero);
    g_glowTouchedCount = 0;
}

const VisualsStats& VisualsStatsGet() { return g_stats; }

} // namespace nl








