#include "features/createmove.h"
#include "features/offsets.h"
#include "features/game.h"
#include "features/antiaim.h"
#include "features/aimbot.h"
#include "features/input.h"
#include "core/log.h"
#include "core/memory.h"
#include "core/vmt.h"
#include "core/paths.h"

#include <windows.h>
#include <cstring>

namespace nl {
namespace {

// 原函数：按 (this, slot, a3, a4) 四个寄存器参数转发 —— x64 前四个参数都在寄存器里，
// 这样无论目标函数实际是 3 参还是 4 参都不会破坏调用约定。
using CreateMoveFn = __int64(__fastcall*)(void*, int, void*, void*);

VmtHook       g_hook;
CreateMoveFn  g_original = nullptr;
CreateMoveStats g_stats;
ULONGLONG     g_windowStart = 0;
bool          g_dumpAfterOriginal = false;
struct CmdBhopLocalStats { bool applied = false; int jumpWrites = 0; int lastButtons = 0; };
CmdBhopLocalStats g_cmdBhop;
int           g_windowCalls = 0;

void SnapshotCmd(void* cmd)
{
    if (cmd == nullptr)
        return;
    uint32_t dwords[8] = {};
    if (g_mem.ReadRaw(reinterpret_cast<uintptr_t>(cmd), dwords, sizeof(dwords)))
    {
        for (int i = 0; i < 8; ++i)
            g_stats.lastCmdDwords[i] = dwords[i];
    }
}

// 把当前物理按键换算成 Source 引擎的按键位掩码（IN_ATTACK=1, IN_JUMP=2, IN_DUCK=4,
// IN_FORWARD=8, IN_BACK=16, IN_MOVELEFT=128, IN_MOVERIGHT=256, IN_ATTACK2=2048）。
// 然后在命令对象 / 它的子对象 / CCSGOInput 本体里扫这个值 —— 命中的偏移就是 m_nButtons。
int BuildButtonMask(int* keyCount)
{
    int mask = 0;
    int n = 0;
    const struct { int vk; int bit; } binds[] = {
        { VK_LBUTTON, 1 }, { VK_SPACE, 2 }, { VK_CONTROL, 4 }, { 'W', 8 }, { 'S', 16 },
        { 'A', 128 }, { 'D', 256 }, { VK_RBUTTON, 2048 }, { VK_SHIFT, 512 }, { 'E', 32 },
    };
    for (const auto& b : binds)
    {
        if ((GetAsyncKeyState(b.vk) & 0x8000) != 0)
        {
            mask |= b.bit;
            ++n;
        }
    }
    if (keyCount != nullptr)
        *keyCount = n;
    return mask;
}

// 候选表：同一个 (区域, 偏移) 如果能在**多种不同按键掩码**下都精确等于当时的掩码，
// 那它几乎只可能是真正的按键位字段（巧合要连续蒙对 3 个不同的值，概率极低）。
struct ButtonCandidate {
    char     tag[24];
    uintptr_t off;
    int      hits;
    int      distinct;
    uint64_t seen;        // 见过的掩码集合（用 1<<(mask%64) 压缩）
    uint32_t lastMask;
};
ButtonCandidate g_btnCands[64];
int             g_btnCandCount = 0;

void NoteButtonHit(const char* tag, uintptr_t off, uint32_t mask)
{
    for (int i = 0; i < g_btnCandCount; ++i)
    {
        if (g_btnCands[i].off == off && strncmp(g_btnCands[i].tag, tag, sizeof(g_btnCands[i].tag)) == 0)
        {
            ++g_btnCands[i].hits;
            g_btnCands[i].lastMask = mask;
            const uint64_t bit = 1ULL << (mask % 64);
            if ((g_btnCands[i].seen & bit) == 0)
            {
                g_btnCands[i].seen |= bit;
                ++g_btnCands[i].distinct;
                // 第一时间把"又蒙对了一个新掩码"的偏移打出来 —— 这就是真按键位的强信号
                FileLog("[btnnew] %s+0x%zX distinct=%d hits=%d lastMask=0x%X",
                        g_btnCands[i].tag, g_btnCands[i].off, g_btnCands[i].distinct,
                        g_btnCands[i].hits, g_btnCands[i].lastMask);
            }
            return;
        }
    }
    if (g_btnCandCount >= (int)(sizeof(g_btnCands) / sizeof(g_btnCands[0])))
        return;
    ButtonCandidate& c = g_btnCands[g_btnCandCount++];
    snprintf(c.tag, sizeof(c.tag), "%s", tag);
    c.off = off;
    c.hits = 1;
    c.distinct = 1;
    c.seen = 1ULL << (mask % 64);
    c.lastMask = mask;
}

// 定期把"匹配过 >=2 种不同掩码"的候选打出来，按 distinct 排序
void DumpButtonCandidates()
{
    int order[64];
    int n = 0;
    for (int i = 0; i < g_btnCandCount; ++i)
        if (g_btnCands[i].distinct >= 2)
            order[n++] = i;
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j)
            if (g_btnCands[order[j]].distinct > g_btnCands[order[i]].distinct)
            {
                const int t = order[i];
                order[i] = order[j];
                order[j] = t;
            }
    if (n == 0)
        return;
    FileLog("[btncand] 候选（偏移, 不同掩码数, 命中次数, 最后掩码）:");
    for (int i = 0; i < n && i < 8; ++i)
    {
        const ButtonCandidate& c = g_btnCands[order[i]];
        FileLog("[btncand]   %s+0x%zX  distinct=%d hits=%d lastMask=0x%X",
                c.tag, c.off, c.distinct, c.hits, c.lastMask);
    }
}

void ScanForButtonMask(void* self, void* cmd)
{
    int keyCount = 0;
    const int mask = BuildButtonMask(&keyCount);
    static int beat = 0;
    if (mask == 0 || keyCount == 0)
    {
        if (++beat % 5 == 0)
            FileLog("[btnscan] mask=0 keyCount=%d（当前没按键，扫不了）", keyCount);
        return;
    }
    if (++beat % 2 == 0)
        FileLog("[btnscan] mask=0x%X keyCount=%d", mask, keyCount);   // 没按键时全 0 到处都是命中，没意义

    auto scan = [&](const char* tag, uintptr_t addr, size_t len, int cap)
    {
        if (addr == 0 || len == 0)
            return;
        int hits = 0;
        for (size_t off = 0; off + 4 <= len && hits < cap; off += 4)
        {
            int32_t v = 0;
            if (!g_mem.ReadRaw(addr + off, &v, 4))
                continue;
            if (v == mask)
            {
                NoteButtonHit(tag, off, (uint32_t)mask);
                ++hits;
            }
        }
    };

    const uintptr_t cmdAddr = reinterpret_cast<uintptr_t>(cmd);
    const int cap = 16;
    scan("cmd", cmdAddr, 0x200, cap);
    // cmd 里的堆指针子对象
    for (uintptr_t off = 0x00; off <= 0x1F8; off += 8)
    {
        const uintptr_t p = g_mem.Read<uintptr_t>(cmdAddr + off);
        if (p < 0x0000010000000000ULL || p > 0x00007FFFFFFFFFFFULL)
            continue;
        if (!g_mem.IsValidRange(p, 0x40))
            continue;
        char tag[40] = { 0 };
        snprintf(tag, sizeof(tag), "p%llX", (unsigned long long)off);
        scan(tag, p, 0x200, cap);
    }
    // CCSGOInput 本体（按键状态数组通常就在输入对象里）
    if (self != nullptr)
        scan("input", reinterpret_cast<uintptr_t>(self), 0x6000, 24);
}

// 每秒把 CUserCmd 前 0x60 字节原样写进日志，同时记录当时物理按下的键 ——
// 两者对照就能量出"按键位/前后左右移动"在命令里的偏移。
// 另外把 cmd 里几个指针字段指向的子对象也 dump 出来（按键位多半在子对象里）。
void DumpCmdAndKeys(void* self, void* cmd)
{
    if (cmd == nullptr)
        return;
    const uintptr_t base = reinterpret_cast<uintptr_t>(cmd);

    const struct { const char* name; int vk; } keys[] = {
        { "W", 'W' }, { "A", 'A' }, { "S", 'S' }, { "D", 'D' },
        { "SPACE", VK_SPACE }, { "CTRL", VK_CONTROL }, { "LMB", VK_LBUTTON }, { "RMB", VK_RBUTTON },
    };
    char keyStr[96] = { 0 };
    for (const auto& k : keys)
    {
        if ((GetAsyncKeyState(k.vk) & 0x8000) != 0)
        {
            strncat_s(keyStr, sizeof(keyStr), k.name, _TRUNCATE);
            strncat_s(keyStr, sizeof(keyStr), " ", _TRUNCATE);
        }
    }

    auto dumpRange = [&](const char* tag, uintptr_t addr, size_t len)
    {
        if (addr == 0 || len == 0 || len > 0x100)
            return;
        uint8_t buf[0x100] = {};
        if (!g_mem.ReadRaw(addr, buf, len))
            return;
        char hex[0x100 * 3 + 1] = { 0 };
        for (size_t i = 0; i < len; ++i)
            snprintf(hex + i * 3, 4, "%02X ", buf[i]);
        FileLog("[cmd.%s] keys=[%s] addr=0x%llX bytes=%s", tag, keyStr, (unsigned long long)addr, hex);
    };

    // cmd 本体：先扫按键掩码，再 dump 一小段做参考
    ScanForButtonMask(self, cmd);
    dumpRange("self0", base, 0x40);
    dumpRange("self1", base + 0x80, 0x80);

    // 扫描"像堆指针"的字段，逐个把子对象也 dump 出来（最多 4 个）
    int dumped = 0;
    for (uintptr_t off = 0x00; off <= 0x1F8 && dumped < 4; off += 8)
    {
        const uintptr_t p = g_mem.Read<uintptr_t>(base + off);
        if (p < 0x0000010000000000ULL || p > 0x00007FFFFFFFFFFFULL)
            continue;
        if (!g_mem.IsValidRange(p, 0x40))
            continue;
        char tag[32] = { 0 };
        snprintf(tag, sizeof(tag), "p%llX", (unsigned long long)off);
        dumpRange(tag, p, 0x60);
        ++dumped;
    }
}
__int64 __fastcall HookedCreateMove(void* self, int slot, void* a3, void* a4)
{
    ++g_stats.calls;
    ++g_windowCalls;
    g_stats.lastSelf = self;
    g_stats.lastSlot = slot;
    g_stats.lastArg3 = a3;
    g_stats.lastArg4 = a4;
    SnapshotCmd(a3);

    const ULONGLONG now = GetTickCount64();
    if (g_windowStart == 0)
        g_windowStart = now;
    if (now - g_windowStart >= 1000)
    {
        g_stats.callsPerSec = g_windowCalls;
        g_windowCalls = 0;
        g_windowStart = now;
        // 每秒一行：调用频率 + 参数样子
        FileLog("[cm] CreateMove 调用 %d 次/秒  self=0x%llX slot=%d arg3=0x%llX arg4=0x%llX",
                g_stats.callsPerSec, (unsigned long long)(uintptr_t)self, slot,
                (unsigned long long)(uintptr_t)a3, (unsigned long long)(uintptr_t)a4);
        g_dumpAfterOriginal = true;   // 这一轮的 dump 放到原函数返回后做
        if (SwitchFlag("bhop_cmd") && g_windowCalls > 0)
        {
            static int lastWrites = 0;
            const int delta = g_cmdBhop.jumpWrites - lastWrites;
            lastWrites = g_cmdBhop.jumpWrites;
            if (delta > 0 || g_cmdBhop.applied)
                FileLog("[cmb] 写命令按键位：本轮置 IN_JUMP %d 次，累计 %d，lastButtons=0x%X",
                        delta, g_cmdBhop.jumpWrites, g_cmdBhop.lastButtons);
        }
    }

    // 先让原函数把命令填好（角度也是它写的），返回前再覆盖 —— 这样改的是"发给服务器的朝向"，
    // 客户端相机不受影响（静默）。
    const __int64 ret = (g_original != nullptr) ? g_original(self, slot, a3, a4) : 0;
    if (a3 != nullptr)
    {
        // 原函数已经跑完：此时命令里的按键位/角度才被填好，正是扫描按键位的最好时机
        if (g_dumpAfterOriginal)
        {
            g_dumpAfterOriginal = false;
            ScanForButtonMask(self, a3);
            static int btnDumpTick = 0;
            if (++btnDumpTick % 5 == 0)
                DumpButtonCandidates();
        }
        // ---- 连跳（写命令按键位版）：只在"站在地面"那一刻给一次全新的 IN_JUMP ----
        // 原函数已经跑完，此时命令的按键位在 +0x60（调试器扫描定位：4 种不同掩码下都精确吻合）。
        // 空中一律清掉 IN_JUMP，落地那一帧再置上 —— 每一次落地都必然是"新按下"，不会漏。
        if (g_settings.misc.bhop && SwitchFlag("bhop_cmd"))
        {
            const int key = (g_settings.misc.bhopKey != 0) ? g_settings.misc.bhopKey : VK_SPACE;
            const LocalState& st2 = GameLocalState();
            if (st2.valid && st2.health > 0 && input::PhysicalKeyHeld(key))
            {
                const uintptr_t btnAddr = reinterpret_cast<uintptr_t>(a3) + 0x60;
                int buttons = g_mem.Read<int>(btnAddr);
                const bool wantJump = st2.onGround;
                if (wantJump)
                    buttons |= 2;      // IN_JUMP
                else
                    buttons &= ~2;
                g_mem.Write(btnAddr, buttons);
                g_cmdBhop.applied = true;
                g_cmdBhop.lastButtons = buttons;
                if (wantJump)
                    ++g_cmdBhop.jumpWrites;
            }
        }
        // 瞄具优先：按着瞄键且有目标时，用 aimbot 的静默角度；否则才用 anti-aim
        const LocalState& st = GameLocalState();
        bool fired = false;
        if (!AimbotApplyToCmd(a3, st.viewAngles, &fired))
            AntiAimApplyToCmd(a3, st.viewAngles);
    }
    return ret;
}

} // namespace

bool CreateMoveInstall(uintptr_t clientBase)
{
    if (g_stats.installed)
        return true;
    const uintptr_t inputOffset = Off("dwCSGOInput");
    if (clientBase == 0 || inputOffset == 0)
    {
        FileLog("[cm] 无法安装：clientBase=0x%llX dwCSGOInput=0x%llX",
                (unsigned long long)clientBase, (unsigned long long)inputOffset);
        return false;
    }
    const int index = (int)Off("kCreateMoveIndex");
    void* object = reinterpret_cast<void*>(clientBase + inputOffset);
    // 先确认对象首字段是个虚表指针（指向 client.dll 的可执行区）
    const uintptr_t vtable = g_mem.Read<uintptr_t>(reinterpret_cast<uintptr_t>(object));
    if (vtable == 0 || !g_mem.IsValidRange(vtable, sizeof(void*) * 8))
    {
        FileLog("[cm] 对象 0x%llX 首字段不是有效虚表（0x%llX），先跳过",
                (unsigned long long)(uintptr_t)object, (unsigned long long)vtable);
        return false;
    }
    void* entry = reinterpret_cast<void*>(g_mem.Read<uintptr_t>(vtable + (uintptr_t)index * sizeof(void*)));
    if (entry == nullptr)
    {
        FileLog("[cm] 虚表 0x%llX 索引 %d 为空", (unsigned long long)vtable, index);
        return false;
    }
    if (!g_hook.Install(object, index, reinterpret_cast<void*>(&HookedCreateMove),
                        reinterpret_cast<void**>(&g_original)))
    {
        FileLog("[cm] 挂载失败（object=0x%llX index=%d）", (unsigned long long)(uintptr_t)object, index);
        return false;
    }
    g_stats = CreateMoveStats();
    g_stats.installed = true;
    AntiAimSetLog(SwitchFlag("aa_log"));
    AimbotSetLog(SwitchFlag("aim_log"));
    FileLog("[cm] CreateMove 钩子已安装：object=0x%llX vtable=0x%llX index=%d entry=0x%llX original=0x%llX（探针模式，只记录）",
            (unsigned long long)(uintptr_t)object, (unsigned long long)vtable, index,
            (unsigned long long)(uintptr_t)entry, (unsigned long long)(uintptr_t)g_original);
    return true;
}

void CreateMoveRemove()
{
    if (!g_stats.installed)
        return;
    g_hook.RemoveAll();
    g_stats.installed = false;
    g_original = nullptr;
    FileLog("[cm] CreateMove 钩子已摘除（共被调用 %d 次）", g_stats.calls);
}

bool CreateMoveHooked() { return g_stats.installed; }
const CreateMoveStats& CreateMoveStatsGet() { return g_stats; }
CmdBhopStats CreateMoveBhopStats()
{
    CmdBhopStats out;
    out.applied = g_cmdBhop.applied;
    out.jumpWrites = g_cmdBhop.jumpWrites;
    out.lastButtons = g_cmdBhop.lastButtons;
    return out;
}

} // namespace nl

















