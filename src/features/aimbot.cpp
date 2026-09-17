#include "features/aimbot.h"
#include "features/esp.h"
#include "features/game.h"
#include "features/input.h"
#include "features/bind.h"
#include "core/log.h"
#include "core/memory.h"
#include "core/paths.h"

#include <windows.h>
#include <cmath>

namespace nl {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kRad2Deg = 180.0f / kPi;

AimTarget g_target;
bool      g_log = false;
ULONGLONG g_lastLog = 0;
bool      g_fired = false;
int       g_fireWrites = 0;

float NormalizeDelta(float a)
{
    while (a > 180.0f)
        a -= 360.0f;
    while (a < -180.0f)
        a += 360.0f;
    return a;
}

// 世界坐标 -> 角度（Source 约定：pitch 负=抬头，yaw atan2(dy,dx)）
void CalcAngle(const float* src, const float* dst, float* outPitch, float* outYaw)
{
    const float dx = dst[0] - src[0];
    const float dy = dst[1] - src[1];
    const float dz = dst[2] - src[2];
    const float hyp = sqrtf(dx * dx + dy * dy);
    *outPitch = atan2f(-dz, hyp) * kRad2Deg;
    *outYaw = atan2f(dy, dx) * kRad2Deg;
}

// rage.hitbox: 0=头 1=脖 2=胸 3=骨盆（越靠上越优先）
int JointFromHitbox(int hitbox)
{
    switch (hitbox)
    {
    case 0: return Bone_Head;
    case 1: return Bone_Neck;
    case 2: return Bone_Chest;
    case 3: return Bone_Pelvis;
    default: return Bone_Head;
    }
}

} // namespace

bool AimbotKeyHeld()
{
    const Settings& s = g_settings;
    if (!s.rage.enable)
        return false;
    // 按键绑定支持 Hold / Toggle / Always 三种模式（在菜单里右键绑定框选）
    return BindActive(s.rage.key);
}

void AimbotTick(const Settings& s, float dt)
{
    (void)dt;
    g_target = AimTarget();

    const LocalState& st = GameLocalState();
    if (!st.valid || s.rage.enable == false || st.health <= 0)
    {
        g_fired = false;
        return;
    }
    if (!AimbotKeyHeld())
    {
        g_fired = false;
        return;
    }

    const int joint = JointFromHitbox(s.rage.hitbox);
    const float* eye = nullptr;
    float eyePos[3] = { st.origin[0], st.origin[1], st.eyeZ };
    eye = eyePos;

    float bestScore = 1.0e9f;
    for (size_t i = 0; i < GamePlayerList().size(); ++i)
    {
        const PlayerView& p = GamePlayerList()[i];
        if (p.team == st.team || p.health <= 0.0f)
            continue;
        // 部位优先级：先试设定部位；那根骨骼没推导出来就往下退
        int useJoint = joint;
        if (!p.bonesValid[useJoint])
        {
            if (p.bonesValid[Bone_Chest])
                useJoint = Bone_Chest;
            else if (p.bonesValid[Bone_Head])
                useJoint = Bone_Head;
            else
                continue;
        }
        const float* wp = p.bonesWorld[useJoint];
        if (wp[0] == 0.0f && wp[1] == 0.0f && wp[2] == 0.0f)
            continue;

        float pitch = 0.0f, yaw = 0.0f;
        CalcAngle(eye, wp, &pitch, &yaw);
        const float dPitch = NormalizeDelta(pitch - st.viewAngles[0]);
        const float dYaw = NormalizeDelta(yaw - st.viewAngles[1]);
        const float angDist = sqrtf(dPitch * dPitch + dYaw * dYaw);
        if (angDist > s.rage.fov)
            continue;   // FOV 之外不瞄

        // priority: 0=离准星最近（默认），1=离得最近，2=血最少
        float score = angDist;
        if (s.rage.priority == 1)
            score = p.distance;
        else if (s.rage.priority == 2)
            score = p.health;

        if (score < bestScore)
        {
            bestScore = score;
            g_target.valid = true;
            g_target.index = (int)i;
            g_target.joint = useJoint;
            g_target.pitch = pitch;
            g_target.yaw = yaw;
            g_target.fovDist = angDist;
            g_target.dist = p.distance;
            g_target.visible = p.visible;
            g_target.name = p.name;
        }
    }

    const ULONGLONG now = GetTickCount64();
    if (g_log && now - g_lastLog > 1000)
    {
        g_lastLog = now;
        if (g_target.valid)
            FileLog("[aim] 目标=%s 部位=%d 距离=%.1fm 与准星夹角=%.2f° 瞄角=(%.1f, %.1f) 当前=(%.1f, %.1f) 可见=%d",
                    g_target.name, g_target.joint, (double)g_target.dist, (double)g_target.fovDist,
                    (double)g_target.pitch, (double)g_target.yaw,
                    (double)st.viewAngles[0], (double)st.viewAngles[1], g_target.visible ? 1 : 0);
        else
            FileLog("[aim] 没有目标（FOV=%.0f° 内为空）", (double)s.rage.fov);
    }
}

bool AimbotApplyToCmd(void* cmd, const float* viewAngles, bool* outFire)
{
    if (outFire != nullptr)
        *outFire = false;
    if (cmd == nullptr || viewAngles == nullptr)
        return false;

    const Settings& s = g_settings;
    if (!s.rage.enable || !g_target.valid || !AimbotKeyHeld())
        return false;
    const LocalState& st = GameLocalState();
    if (!st.valid || st.health <= 0)
        return false;

    // ---- 写角度（静默：只改命令，不动相机）----
    // smooth 只影响"相机跟随"的 legit 模式；这里先做静默瞬时对齐，
    // 平滑留给 legit aim（鼠标移动版），所以此处忽略 smooth。
    const float pair[2] = { g_target.pitch, g_target.yaw };
    const uintptr_t base = reinterpret_cast<uintptr_t>(cmd);
    if (!g_mem.WriteRaw(base + 0x18, pair, sizeof(pair)))
        return false;

    // ---- 自动开火：准星已经在目标身上就给 IN_ATTACK ----
    const bool onTarget = g_target.fovDist <= 1.6f;
    if (s.rage.autoFire && onTarget)
    {
        const uintptr_t btnAddr = base + 0x60;
        int buttons = g_mem.Read<int>(btnAddr);
        buttons |= 1;             // IN_ATTACK
        g_mem.Write(btnAddr, buttons);
        g_fired = true;
        ++g_fireWrites;
        if (outFire != nullptr)
            *outFire = true;
    }

    if (g_log)
    {
        static ULONGLONG last = 0;
        const ULONGLONG now = GetTickCount64();
        if (now - last > 1000)
        {
            last = now;
            FileLog("[aim] 已写入命令角度：(%.1f, %.1f)  开火累计 %d 次  目标=%s",
                    (double)pair[0], (double)pair[1], g_fireWrites, g_target.name);
        }
    }
    return true;
}

const AimTarget& AimbotTargetGet() { return g_target; }
void AimbotSetLog(bool on) { g_log = on; }

} // namespace nl

