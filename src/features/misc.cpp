#include "features/misc.h"
#include "features/convar.h"
#include "features/input.h"
#include "features/bind.h"
#include "features/game.h"
#include "features/esp.h"
#include "core/log.h"
#include "core/paths.h"
#include "core/memory.h"

#include <windows.h>
#include <cmath>
#include <cstdio>

namespace nl {
namespace {

MiscStatus g_status;

// ---- bhop / jump 相关 ----
bool  g_userHoldsJump = false;   // 用户真的按住跳键（干净采样窗口里读到的）
bool  g_jumpInjecting = false;   // 我们正在注入跳键按下
bool  g_lastHolds = false;       // 上一次的按住判定（用于只记录变化）
ULONGLONG g_repressAt = 0;
ULONGLONG g_lastRepressAt = 0;
bool  g_repressInFlight = false;
ULONGLONG g_lastJumpAt = 0;
int   g_maxJumpGapMs = 0;
int   g_jumpGapOver300 = 0;
bool  g_wasOnGround = false;
int   g_airTransitions = 0;   // 地面->空中 次数 = 真的起跳成功次数
int   g_repressCount = 0;
int   g_landings = 0;         // 空中->地面 次数
bool  g_repressedThisLanding = false;
ULONGLONG g_jumpStart = 0;
bool  g_jumpPendingConfirm = false;
int   g_jumpConfirm = 0;

// ---- autoStrafe ----
ULONGLONG g_lastStrafeSwitch = 0;
int   g_strafeSide = 0;          // +1 = D，-1 = A
float g_lastYaw = 0.0f;

// ---- triggerbot ----
ULONGLONG g_lastTriggerFire = 0;
int   g_triggerFires = 0;

// ---- autoPistol ----
ULONGLONG g_lastPistolShot = 0;

// ---- autoPeek ----
bool  g_peekKeyWasDown = false;
float g_peekOrigin[3] = { 0, 0, 0 };
bool  g_peekReturning = false;
ULONGLONG g_peekReturnStart = 0;

// ---- noFlash ----
bool  g_flashZeroed = false;

bool KeyHeld(int vk)
{
    if (vk == 0)
        return true;
    return input::PhysicalDown(vk);
}

bool IsPistol(int id)
{
    switch (id)
    {
    case 1: case 2: case 3: case 4: case 30: case 32: case 36: case 61: case 63: case 64:
        return true;
    default:
        return false;
    }
}

// 把世界坐标差换算成"要不要按 W/S/A/D"：用视角度把 delta 投影到前/右轴
void PressMoveKeysToward(const LocalState& st, const float target[3])
{
    const float dx = target[0] - st.origin[0];
    const float dy = target[1] - st.origin[1];
    const float dist = sqrtf(dx * dx + dy * dy);
    const float yaw = st.viewAngles[1] * 3.14159265358979323846f / 180.0f;
    const float fwdX = cosf(yaw), fwdY = sinf(yaw);
    const float rightX = sinf(yaw), rightY = -cosf(yaw);
    const float f = dx * fwdX + dy * fwdY;
    const float r = dx * rightX + dy * rightY;
    const float dead = 12.0f;
    input::KeyToggle('W', f > dead);
    input::KeyToggle('S', f < -dead);
    input::KeyToggle('D', r > dead);
    input::KeyToggle('A', r < -dead);
    if (dist < 24.0f)
    {
        input::KeyToggle('W', false);
        input::KeyToggle('S', false);
        input::KeyToggle('A', false);
        input::KeyToggle('D', false);
    }
}

void ReleaseMoveKeys()
{
    input::KeyToggle('W', false);
    input::KeyToggle('S', false);
    input::KeyToggle('A', false);
    input::KeyToggle('D', false);
}

// ---- bhop：**按住跳键时**才接管；松开跳键立即停止 ----
//
//  语义（按用户要求）：
//    * 用户按住跳键（默认空格）→ 我们接管这个键：按住期间固定周期做"抬起→按回"的重按，
//      让游戏每次落地都能收到一次全新的跳跃按键，从而连跳；
//    * 用户松开跳键 → 立刻停止注入，一个多余的跳都没有。
//
//  **失效安全（fail-safe）设计**：进入连跳只看**低级键盘钩子**（只统计真人按键，
//  忽略 LLKHF_INJECTED）。GetAsyncKeyState 分不清真人按键和我们自己的注入，用它来
//  "进入"状态会把自己注入的按下当成用户按住 → 用户松手后一直跳（实测踩过两次）。
//  所以现在：
//    * 进入 = 钩子报告"真人按着跳键"；
//    * 退出 = 钩子报告"真人抬起"（或看门狗发现注入状态与真人状态不符）；
//    * 钩子要是不工作（比如被系统摘掉），结果只是"连跳不生效"，绝不会乱跳。
void UpdateJumpKeyUserState(int key)   // 模式取自 g_settings.misc.bhopKey
{
    // 1) 真人抬起：最高优先级，立刻退出
    static unsigned long long lastRelease = 0;
    const unsigned long long releases = input::PhysicalReleaseCount(key);
    if (releases != lastRelease)
    {
        lastRelease = releases;
        if (g_userHoldsJump)
            FileLog("[misc] bhop 跳键：真人抬起 → 立刻停止连跳");
        g_userHoldsJump = false;
        g_lastHolds = false;
        return;
    }

    // 2) 真人按住（钩子视角）才是进入条件；模式（Hold/Toggle/Always）由绑定决定
    const bool phys = BindActive(BindMake(key, BindModeOf(g_settings.misc.bhopKey)));
    g_status.jumpKeyPhys = phys;
    g_status.jumpKeyAsync = input::PhysicalDown(key);

    // 3) 看门狗：我们在注入、但真人并没有按着 → 说明状态不一致，马上收手
    if ((g_jumpInjecting || g_repressInFlight) && !phys)
    {
        if (g_userHoldsJump)
            FileLog("[misc] bhop 看门狗：真人状态=未按住，但我们在注入 → 立刻停手并松开");
        g_userHoldsJump = false;
        g_lastHolds = false;
        return;
    }

    if (g_userHoldsJump != phys)
    {
        g_userHoldsJump = phys;
        g_lastHolds = phys;
        FileLog("[misc] bhop 跳键状态变化：真人按住=%d（async 原始值=%d）",
                phys ? 1 : 0, g_status.jumpKeyAsync ? 1 : 0);
    }
}

// 连跳的核心：**重按**（先抬起、约 15ms 后按回，最终状态仍是"按着"）。
// 为什么不能像上一版那样"按下→松开"：注入的抬起会把用户自己的按住状态一起抹掉，
// 于是系统认为跳键已经松开，用户即使一直按着也只会跳一次。
// 重按不会改变最终状态（始终按下），既给游戏制造了"新的按键"，又保留用户的按住语义。
void TickBhop(const Settings& s, const LocalState& st)
{
    // 如果已经用"写命令按键位"的方式做连跳（nl_switch.ini: bhop_cmd=1），
    // SendInput 这一套就整体让位，避免两套机制互相打架。
    if (SwitchFlag("bhop_cmd"))
    {
        if (g_jumpInjecting || g_repressInFlight)
        {
            input::KeyUp((s.misc.bhopKey != 0) ? s.misc.bhopKey : VK_SPACE);
            g_jumpInjecting = false;
            g_repressInFlight = false;
        }
        g_status.bhopActive = SwitchFlag("bhop_cmd") && s.misc.bhop && BindActive(s.misc.bhopKey);
        g_status.jumpKeyPhys = BindActive(s.misc.bhopKey);
        g_status.jumpKeyUser = g_status.bhopActive;
        return;
    }
    const int key = (s.misc.bhopKey != 0) ? s.misc.bhopKey : VK_SPACE;
    UpdateJumpKeyUserState(key);

    const ULONGLONG now = GetTickCount64();
    const bool canRun = st.valid && s.misc.bhop && st.health > 0;
    if (!canRun || !g_userHoldsJump)
    {
        if (g_jumpInjecting || g_repressInFlight)
        {
            input::KeyUp(key);
            g_jumpInjecting = false;
            g_repressInFlight = false;
        }
        g_status.bhopActive = false;
        g_status.bhopJumping = false;
        g_status.jumpKeyUser = g_userHoldsJump;
        return;
    }

    g_status.bhopActive = true;
    if (st.onGround && !g_wasOnGround) { ++g_landings; }
    if (!st.onGround && g_wasOnGround) { ++g_airTransitions; }
    g_wasOnGround = st.onGround;
    g_status.jumpKeyUser = true;

    if (g_repressInFlight)
    {
        // 抬起已经持续了 ~15ms（足够被一个 tick 采到），现在按回
        if (now >= g_repressAt)
        {
            input::KeyDown(key);
            g_repressInFlight = false;
            g_jumpInjecting = true;
            g_repressedThisLanding = true;
            ++g_repressCount;      // 注入周期数（不是真的起跳）
            if (g_lastJumpAt != 0) { const int gap = (int)(now - g_lastJumpAt); if (gap > g_maxJumpGapMs) g_maxJumpGapMs = gap; if (gap > 300) ++g_jumpGapOver300; }
            g_lastJumpAt = now;
            g_status.jumpConfirmCount = g_jumpConfirm;
    g_status.maxJumpGapMs = g_maxJumpGapMs;
    g_status.airTransitions = g_airTransitions;
    g_status.landings = g_landings;
    g_status.repressCount = g_repressCount;
    g_status.jumpGapOver300 = g_jumpGapOver300;
        }
        g_status.bhopJumping = false;
        return;
    }

    if (!st.onGround)
        g_repressedThisLanding = false;   // 离地后允许下次落地再重按

    // 关键改动：**按住期间持续做重按**，而不是"等落地那一帧再按"。
    // 之前只在地面时重按，只要我们的帧没采到落地那一帧（或落地下一次落地之间隔了
    // 一整帧），这一次落地就没有新按键 → 连跳断链（用户反馈"有些地方会断"）。
    // 现在改成固定 ~25ms 一个周期：抬起 8ms → 按回。落地无论发生在周期里的哪一刻，
    // 最多 25ms 内一定会吃到一次全新的按键，链就接得上了。
    const ULONGLONG kRepressPeriodMs = 25;
    const ULONGLONG kRepressUpMs = 8;
    if (now - g_lastRepressAt >= kRepressPeriodMs)
    {
        input::KeyUp(key);
        g_jumpInjecting = false;
        g_repressInFlight = true;
        g_repressAt = now + kRepressUpMs;
        g_lastRepressAt = now;
    }
    g_status.bhopJumping = g_jumpInjecting;
    g_status.jumpConfirmCount = g_jumpConfirm;
}

// ---- autoStrafe：同样只在"用户按住跳键"的期间生效（离地时按视角转动方向交替 A/D）----
void TickAutoStrafe(const Settings& s, const LocalState& st)
{
    g_status.strafeActive = false;
    if (!st.valid || !s.misc.autoStrafe || !g_userHoldsJump || st.onGround || st.speed2D < 5.0f)
    {
        input::KeyToggle('A', false);
        input::KeyToggle('D', false);
        g_strafeSide = 0;
        g_status.strafeSide = 0;
        return;
    }
    const float yaw = st.viewAngles[1];
    float dYaw = yaw - g_lastYaw;
    while (dYaw > 180.0f)
        dYaw -= 360.0f;
    while (dYaw < -180.0f)
        dYaw += 360.0f;
    g_lastYaw = yaw;
    // 视角向右转 → 按 D；向左转 → 按 A（经典 strafe 方向）
    if (fabsf(dYaw) > 0.05f)
    {
        const int want = (dYaw > 0.0f) ? 1 : -1;
        const ULONGLONG now = GetTickCount64();
        if (want != g_strafeSide && now - g_lastStrafeSwitch > 30)
        {
            g_strafeSide = want;
            g_lastStrafeSwitch = now;
            input::KeyToggle('D', want > 0);
            input::KeyToggle('A', want < 0);
        }
    }
    g_status.strafeActive = g_strafeSide != 0;
    g_status.strafeSide = g_strafeSide;
}

// ---- triggerbot：准星附近有可见敌人就点一下 ----
// 顺带把"准星到最近敌人的像素距离"记进状态里：这是 triggerbot / 后续 aimbot
// 瞄点选择的核心判据，日志里能直接看到它随视角变化。
float CrosshairDistanceToEnemies(const LocalState& st)
{
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 center(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
    float best = 9999.0f;
    for (const PlayerView& p : GamePlayerList())
    {
        if (!p.visible || p.team == st.team || p.health <= 0.0f)
            continue;
        // 用胸口/头部骨骼点判断"准星是否压在敌人身上"
        for (int joint : { Bone_Head, Bone_Chest, Bone_Pelvis })
        {
            if (!p.bonesValid[joint])
                continue;
            const ImVec2 b = p.bones[joint];
            const float d = sqrtf((b.x - center.x) * (b.x - center.x) + (b.y - center.y) * (b.y - center.y));
            if (d < best)
                best = d;
        }
    }
    return best;
}

void TickTriggerbot(const Settings& s, const LocalState& st)
{
    g_status.triggerFiring = false;
    g_status.crosshairDist = CrosshairDistanceToEnemies(st);
    g_status.crosshairCandidate = (g_status.crosshairDist < 9999.0f) ? 1 : -1;
    if (!st.valid || !s.rage.triggerbot || !BindActive(s.rage.key) || st.health <= 0)
        return;
    const float radius = 12.0f;
    if (g_status.crosshairDist > radius)
        return;
    const ULONGLONG now = GetTickCount64();
    const ULONGLONG delay = (ULONGLONG)((s.rage.triggerDelay > 0.0f) ? s.rage.triggerDelay : 0.0f);
    if (now - g_lastTriggerFire < delay)
        return;
    g_lastTriggerFire = now;
    input::MouseLeft(true);
    ++g_triggerFires;
    g_status.triggerFiring = true;
    g_status.triggerFireCount = g_triggerFires;
}

// ---- edgeJump（近似实现）：站在地面、正在移动且按住键时补一个跳 ----
// 真正的 edge jump 需要在起跳瞬间判断"前方是悬空"，那要射线追踪（CreateMove 阶段再做）。
// 这里先做"移动中自动补跳"，效果接近、可验证，代码里如实标注。
void TickEdgeJump(const Settings& s, const LocalState& st)
{
    if (!st.valid || !s.misc.edgeJump || !BindActive(s.misc.edgeJumpKey) || st.health <= 0)
        return;
    if (st.onGround && st.speed2D > 30.0f)
        input::KeyDown(VK_SPACE);
    else if (!st.onGround && !s.misc.bhop)
        input::KeyUp(VK_SPACE);
}

// ---- autoPistol：手枪按住左键时按射速点射 ----
void TickAutoPistol(const Settings& s, const LocalState& st)
{
    g_status.autoPistolFiring = false;
    if (!st.valid || !s.misc.autoPistol || !IsPistol(st.weaponId))
        return;
    if (!input::PhysicalDown(VK_LBUTTON))
        return;
    const ULONGLONG now = GetTickCount64();
    if (now - g_lastPistolShot < 120)
        return;
    g_lastPistolShot = now;
    input::MouseLeft(false);
    input::MouseLeft(true);
    g_status.autoPistolFiring = true;
}

// ---- knifeBot：近距离背后/正面都直接砍 ----
void TickKnifeBot(const Settings& s, const LocalState& st)
{
    if (!st.valid || !s.misc.knifeBot || !BindActive(s.rage.key) || st.health <= 0)
        return;
    for (const PlayerView& p : GamePlayerList())
    {
        if (!p.visible || p.team == st.team || p.health <= 0.0f)
            continue;
        if (p.distance > 2.2f)     // 2.2m 内
            continue;
        input::MouseLeft(true);
        return;
    }
    input::MouseLeft(false);
}

// ---- noFlash：把闪光上限写 0（注入版才有写权限）----
void TickNoFlash(const Settings& s, const LocalState& st)
{
    if (!st.valid || st.flashAlphaAddr == 0)
        return;
    if (s.misc.noFlash)
    {
        if (!g_flashZeroed)
        {
            const float zero = 0.0f;
            if (g_mem.Write(st.flashAlphaAddr, zero))
            {
                g_flashZeroed = true;
                FileLog("[misc] noFlash: m_flFlashMaxAlpha -> 0");
            }
        }
    }
    else if (g_flashZeroed)
    {
        const float normal = 255.0f;
        if (g_mem.Write(st.flashAlphaAddr, normal))
        {
            g_flashZeroed = false;
            FileLog("[misc] noFlash: m_flFlashMaxAlpha -> 255");
        }
    }
    g_status.noFlashApplied = g_flashZeroed;
}

// ---- autoPeek：按住记录位置，松开自动走回去 ----
void TickAutoPeek(const Settings& s, const LocalState& st)
{
    if (!st.valid || !s.misc.autoPeek)
    {
        if (g_peekReturning)
        {
            ReleaseMoveKeys();
            g_peekReturning = false;
        }
        g_peekKeyWasDown = false;
        g_status.autoPeekWalking = false;
        return;
    }
    const bool down = BindActive(s.misc.autoPeekKey);
    if (down && !g_peekKeyWasDown)
    {
        g_peekOrigin[0] = st.origin[0];
        g_peekOrigin[1] = st.origin[1];
        g_peekOrigin[2] = st.origin[2];
        g_peekReturning = false;
        ReleaseMoveKeys();
    }
    else if (!down && g_peekKeyWasDown)
    {
        g_peekReturning = true;
        g_peekReturnStart = GetTickCount64();
    }
    g_peekKeyWasDown = down;

    if (g_peekReturning)
    {
        const float dx = g_peekOrigin[0] - st.origin[0];
        const float dy = g_peekOrigin[1] - st.origin[1];
        const float dist = sqrtf(dx * dx + dy * dy);
        if (dist > 24.0f && GetTickCount64() - g_peekReturnStart < 1500)
        {
            PressMoveKeysToward(st, g_peekOrigin);
            g_status.autoPeekWalking = true;
        }
        else
        {
            ReleaseMoveKeys();
            g_peekReturning = false;
            g_status.autoPeekWalking = false;
        }
    }
}


// ---- 静音敌人 / 防挂机（定义放在匿名 namespace 内，用到上面的状态）----
void TickMuteEnemy(const Settings& s)
{
    static int applied = -1;      // -1=没动过，0/1=当前写入的值
    const int want = s.misc.muteEnemy ? 1 : 0;
    if (want == applied)
        return;
    if (ConVarSetInt("cl_mute_enemy_team", want))
    {
        applied = want;
        FileLog("[misc] cl_mute_enemy_team = %d（%s）", want, want ? "敌人已静音" : "恢复");
    }
}

// ---- 防挂机：每隔一段时间给一点输入，避免被判 AFK ----
void TickAntiAfk(const Settings& s, const LocalState& st)
{
    if (!s.misc.antiAfk || !st.valid)
        return;
    static ULONGLONG last = 0;
    const ULONGLONG now = GetTickCount64();
    if (now - last < 10000)
        return;
    last = now;
    // 切武器（送一次数字键）比乱动安全：不会把你从掩体后面挪出去
    static int slot = 0;
    const int vks[3] = { '1', '2', '3' };
    const int vk = vks[slot % 3];
    ++slot;
    input::KeyDown(vk);
    Sleep(30);
    input::KeyUp(vk);
    FileLog("[misc] anti-afk：切武器键 %c", (char)vk);
}

} // namespace

void MiscTick(const Settings& s)
{
    const LocalState& st = GameLocalState();
    g_status.note = input::LastAction();

    TickBhop(s, st);
    TickEdgeJump(s, st);
    TickAutoStrafe(s, st);
    TickTriggerbot(s, st);
    TickAutoPistol(s, st);
    TickKnifeBot(s, st);
    TickNoFlash(s, st);
    TickAutoPeek(s, st);
    TickMuteEnemy(s);
    TickAntiAfk(s, st);

    // triggerbot 只点一下：命中过就松手，避免一直按住
    if (!g_status.triggerFiring && g_status.triggerFireCount > 0)
        input::MouseLeft(false);
}

// ---- 静音敌人（cl_mute_enemy_team，纯 convar，比改内存安全）----
void MiscShutdown()
{
    ReleaseMoveKeys();
    input::ReleaseAll();
    if (g_flashZeroed)
    {
        const float normal = 255.0f;
        g_mem.Write(GameLocalState().flashAlphaAddr, normal);
        g_flashZeroed = false;
    }
}

const MiscStatus& MiscStatusGet() { return g_status; }
} // namespace nl







