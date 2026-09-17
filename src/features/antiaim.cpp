#include "features/antiaim.h"
#include "features/config.h"
#include "features/game.h"
#include "core/log.h"
#include "core/memory.h"
#include "core/paths.h"

#include <windows.h>
#include <cmath>

namespace nl {
namespace {

AntiAimStats g_stats;
bool  g_log = false;
ULONGLONG g_lastLog = 0;
int   g_jitterFlip = 0;

float NormalizeYaw(float yaw)
{
    while (yaw > 180.0f)
        yaw -= 360.0f;
    while (yaw < -180.0f)
        yaw += 360.0f;
    return yaw;
}

float ClampPitch(float pitch)
{
    if (pitch > 89.0f)
        pitch = 89.0f;
    if (pitch < -89.0f)
        pitch = -89.0f;
    return pitch;
}

} // namespace

void AntiAimApplyToCmd(void* cmd, const float* viewAngles)
{
    g_stats.applied = false;
    if (cmd == nullptr || viewAngles == nullptr)
        return;

    const Settings& s = g_settings;
    if (!s.aa.enable)
        return;
    const LocalState& st = GameLocalState();
    if (!st.valid || st.health <= 0)
        return;

    // ---- 俯仰 ----
    float pitch = viewAngles[0];
    switch (s.aa.pitch)
    {
    case 1: pitch = 89.0f; break;     // 朝天
    case 2: pitch = -89.0f; break;    // 朝地
    case 3: pitch = 0.0f; break;      // 水平
    default: break;                   // 0 = 关
    }
    pitch = ClampPitch(pitch);

    // ---- 偏航 ----
    float yaw = viewAngles[1];
    switch (s.aa.yawBase)
    {
    case 0: yaw = viewAngles[1]; break;          // 视角
    case 1: yaw = viewAngles[1] + 180.0f; break; // 背面
    case 2: yaw = 0.0f; break;                   // 世界坐标
    case 3: break;                               // 保留
    default: break;
    }
    if (s.aa.yawAdd != 0.0f)
        yaw += s.aa.yawAdd;

    if (s.aa.yawJitter > 0.1f)
    {
        g_jitterFlip ^= 1;
        const float j = s.aa.yawJitter * 0.5f;
        yaw += (g_jitterFlip != 0) ? j : -j;
    }
    yaw = NormalizeYaw(yaw);

    // ---- 写进命令（+0x18 = pitch, +0x1C = yaw）----
    const uintptr_t base = reinterpret_cast<uintptr_t>(cmd);
    const float pair[2] = { pitch, yaw };
    if (!g_mem.WriteRaw(base + 0x18, pair, sizeof(pair)))
        return;

    // ---- 关键：还要写角色身上的 m_angEyeAngles ----
    //  命令里的角度只决定"打枪方向"；别人看到的模型朝向是 m_angEyeAngles（客户端网络同步的字段）。
    //  只写命令的话，AA 在别人眼里完全没效果 —— 用户反馈"AA 没效果"就是这个原因。
    //  写入它不会带动本地相机（相机走 dwViewAngles / 输入角度），所以仍然是"自己看不出来"。
    if (!SwitchFlag("aa_no_eyefield"))
    {
        const uintptr_t eyeOff = Off("m_angEyeAngles");
        if (eyeOff != 0 && st.pawn != 0)
        {
            const float eye[2] = { pitch, yaw };
            if (g_mem.WriteRaw(st.pawn + eyeOff, eye, sizeof(eye)))
                ++g_stats.eyeWrites;
        }
    }

    g_stats.applied = true;
    ++g_stats.writes;
    g_stats.lastPitch = pitch;
    g_stats.lastYaw = yaw;

    const ULONGLONG now = GetTickCount64();
    if (g_log && now - g_lastLog > 1000)
    {
        g_lastLog = now;
        FileLog("[aa] 命令角度=(%.1f, %.1f) 原视角=(%.1f, %.1f) 命令写入=%d 次 模型朝向写入=%d 次",
                (double)pitch, (double)yaw, (double)viewAngles[0], (double)viewAngles[1],
                g_stats.writes, g_stats.eyeWrites);
    }
}

const AntiAimStats& AntiAimStatsGet() { return g_stats; }
void AntiAimSetLog(bool on) { g_log = on; }

} // namespace nl


