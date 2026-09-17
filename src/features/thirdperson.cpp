#include "features/thirdperson.h"
#include "features/bind.h"
#include "features/game.h"
#include "core/log.h"
#include "core/memory.h"
#include "core/sigscan.h"

#include <windows.h>
#include <cstring>

namespace nl {
namespace {

ThirdPersonStats g_stats;

// ThirdPersonOffHandler 的完整模式（来自 cs2-sdk 的签名表）：
//   48 83 EC 28            sub  rsp,28
//   48 8B 0D ?? ?? ?? ??   mov  rcx,[rip+?]
//   48 8D 54 24 ??         lea  rdx,[rsp+?]
//   48 8B 01               mov  rax,[rcx]
//   FF 90 ?? ?? ?? ??      call [rax+?]
//   83 7C 24 ?? 00         cmp  dword [rsp+?],0
//   75 ??                  jnz  short
//   48 8B 05 ?? ?? ?? ??   mov  rax,[rip+?]      <- 我们要解的
//   C6 80 ?? ?? ?? ?? ??   mov  byte [rax+?],?   <- 开关偏移与值
const char* kThirdPersonPattern =
    "48 83 EC 28 48 8B 0D ?? ?? ?? ?? 48 8D 54 24 ?? 48 8B 01 FF 90 ?? ?? ?? ?? "
    "83 7C 24 ?? 00 75 ?? 48 8B 05 ?? ?? ?? ?? C6 80 ?? ?? ?? ?? ??";

// 在匹配到的这段代码里找 "48 8B 05 <disp32>"（mov rax,[rip+disp]），返回全局地址
uintptr_t DecodeRipGlobal(uintptr_t matchAddr, size_t scanLen)
{
    uint8_t buf[256] = {};
    if (scanLen > sizeof(buf))
        scanLen = sizeof(buf);
    if (!g_mem.ReadRaw(matchAddr, buf, scanLen))
        return 0;
    for (size_t i = 0; i + 7 <= scanLen; ++i)
    {
        if (buf[i] == 0x48 && buf[i + 1] == 0x8B && buf[i + 2] == 0x05)
        {
            int32_t disp = 0;
            memcpy(&disp, buf + i + 3, 4);
            return matchAddr + i + 7 + (int64_t)disp;
        }
    }
    return 0;
}

// 找 "C6 80 <disp32> <imm8>"（mov byte [rax+disp],imm），返回偏移
bool DecodeByteWrite(uintptr_t matchAddr, size_t scanLen, uint32_t* outOffset)
{
    uint8_t buf[256] = {};
    if (scanLen > sizeof(buf))
        scanLen = sizeof(buf);
    if (!g_mem.ReadRaw(matchAddr, buf, scanLen))
        return false;
    for (size_t i = 0; i + 7 <= scanLen; ++i)
    {
        if (buf[i] == 0xC6 && buf[i + 1] == 0x80)
        {
            int32_t disp = 0;
            memcpy(&disp, buf + i + 2, 4);
            *outOffset = (uint32_t)disp;
            return true;
        }
    }
    return false;
}

// 诊断：同时扫 On/Off 两个模式，把命中地址与解出的全局/偏移都打出来对照。
// 上一版只扫 Off 模式，结果匹配到了一个"形状相似"的函数（偏移解出来是 0x22A 但不是开关），
// 写完没有任何效果 —— 这次用 On 模式（含 41 8B 80 50 0B 00 00 这个独特常量）交叉验证。
const char* kThirdPersonOnPattern =
    "48 83 EC 38 48 8B 0D ?? ?? ?? ?? 48 8D 54 24 ?? 48 8B 01 FF 90 10 03 00 00 "
    "83 7C 24 ?? 00 0F 85 ?? ?? ?? ?? 4C 8B 05 ?? ?? ?? ?? 41 8B 80 50 0B 00 00";

// 在匹配点之后找 "C6 80 <disp32> <imm>" 形式的字节写入，返回偏移与写入值
bool FindByteWrite(uintptr_t matchAddr, size_t scanLen, uint32_t* outOffset, uint8_t* outImm)
{
    uint8_t buf[512] = {};
    if (scanLen > sizeof(buf))
        scanLen = sizeof(buf);
    if (!g_mem.ReadRaw(matchAddr, buf, scanLen))
        return false;
    for (size_t i = 0; i + 7 <= scanLen; ++i)
    {
        if (buf[i] == 0xC6 && buf[i + 1] == 0x80)
        {
            int32_t disp = 0;
            memcpy(&disp, buf + i + 2, 4);
            if (outOffset != nullptr)
                *outOffset = (uint32_t)disp;
            if (outImm != nullptr)
                *outImm = buf[i + 6];
            return true;
        }
    }
    return false;
}

// 在匹配点之后找 "C6 80 <disp32> <imm>"，并往前找最近一次 "48 8B 05 <disp32>"（mov rax,[rip]），
// 那个全局 + disp 才是状态字节。上一版只解了 rip 全局却配了别处的偏移，写了个无关的字节。
// 解 "C7 80 <disp32> <imm32>"（mov dword [rax+disp], imm32）—— Off-handler 复位时写的就是它，
// 对应第三人称的相机距离/偏移。只开开关不设距离，画面依然贴在眼睛上（看着像没效果）。
bool DecodeDwordWrite(uintptr_t matchAddr, size_t scanLen, uint32_t* outOffset, uint32_t* outImm)
{
    uint8_t buf[512] = {};
    if (scanLen > sizeof(buf))
        scanLen = sizeof(buf);
    if (!g_mem.ReadRaw(matchAddr, buf, scanLen))
        return false;
    for (size_t i = 0; i + 10 <= scanLen; ++i)
    {
        if (buf[i] == 0xC7 && buf[i + 1] == 0x80)
        {
            int32_t disp = 0;
            uint32_t imm = 0;
            memcpy(&disp, buf + i + 2, 4);
            memcpy(&imm, buf + i + 6, 4);
            if (disp > 0 && disp < 0x1000)
            {
                if (outOffset != nullptr)
                    *outOffset = (uint32_t)disp;
                if (outImm != nullptr)
                    *outImm = imm;
                return true;
            }
        }
    }
    return false;
}
uintptr_t ResolveFlagFromOnHandler(uintptr_t matchAddr, size_t scanLen, uint32_t* outOffset, uint8_t* outImm)
{
    // 窗口要往匹配点**之前**扩一段：rax 的来源指令（mov rax,[rip+disp]）通常在模式起点之前
    const size_t back = 0x80;
    uint8_t buf[512] = {};
    size_t total = back + scanLen;
    if (total > sizeof(buf))
        total = sizeof(buf);
    const uintptr_t winStart = matchAddr - back;
    if (!g_mem.ReadRaw(winStart, buf, total))
        return 0;

    size_t writePos = (size_t)-1;
    uint32_t off = 0;
    uint8_t imm = 0;
    for (size_t i = 0; i + 7 <= total; ++i)
    {
        if (buf[i] == 0xC6 && buf[i + 1] == 0x80)
        {
            int32_t d = 0;
            memcpy(&d, buf + i + 2, 4);
            if (d > 0 && d < 0x1000)     // 只看小偏移（结构体字段），挡掉无关的写入
            {
                writePos = i;
                off = (uint32_t)d;
                imm = buf[i + 6];
                break;
            }
        }
    }
    if (writePos == (size_t)-1)
        return 0;

    // 从写入点往前找最近一次 "48 8B 05"（mov rax,[rip+disp]）
    for (size_t i = writePos; i-- > 0;)
    {
        if (buf[i] == 0x48 && buf[i + 1] == 0x8B && buf[i + 2] == 0x05)
        {
            int32_t disp = 0;
            memcpy(&disp, buf + i + 3, 4);
            const uintptr_t global = winStart + i + 7 + (int64_t)disp;
            if (outOffset != nullptr)
                *outOffset = off;
            if (outImm != nullptr)
                *outImm = imm;
            return global;
        }
    }
    return 0;
}
void ThirdPersonDiagnose(uintptr_t clientBase, size_t size)
{
    const uintptr_t onHit = ScanForSignature(g_mem, clientBase, size, kThirdPersonOnPattern);
    FileLog("[tp] On 模式命中 = 0x%llX (client+0x%llX)",
            (unsigned long long)onHit, (unsigned long long)(onHit ? onHit - clientBase : 0));
    if (onHit != 0)
    {
        const uintptr_t g = DecodeRipGlobal(onHit, 0x80);
        uint32_t off = 0;
        uint8_t imm = 0;
        const bool haveW = FindByteWrite(onHit, 0x120, &off, &imm);
        FileLog("[tp] On: 全局=0x%llX 字节写偏移=%s0x%X 写入值=%d",
                (unsigned long long)g, haveW ? "" : "未找到 ", off, (int)imm);
        uint32_t dOff = 0;
        uint32_t dImm = 0;
        if (DecodeDwordWrite(onHit, 0x200, &dOff, &dImm))
        {
            float asFloat = 0.0f;
            memcpy(&asFloat, &dImm, 4);
            FileLog("[tp] On: dword 写偏移=0x%X 值=0x%X (int=%d float=%.3f)", dOff, dImm,
                    (int)dImm, (double)asFloat);
        }
    }
    const uintptr_t offHit = ScanForSignature(g_mem, clientBase, size, kThirdPersonPattern);
    FileLog("[tp] Off 模式命中 = 0x%llX (client+0x%llX)",
            (unsigned long long)offHit, (unsigned long long)(offHit ? offHit - clientBase : 0));
    if (offHit != 0)
    {
        const uintptr_t g = DecodeRipGlobal(offHit, 0x80);
        uint32_t off = 0;
        uint8_t imm = 0;
        const bool haveW = FindByteWrite(offHit, 0x120, &off, &imm);
        FileLog("[tp] Off: 全局=0x%llX 字节写偏移=%s0x%X 写入值=%d",
                (unsigned long long)g, haveW ? "" : "未找到 ", off, (int)imm);
    }
}
} // namespace

void ThirdPersonInit(uintptr_t clientBase, size_t clientSize)
{
    if (clientBase == 0 || clientSize == 0)
    {
        g_stats.note = "no client.dll";
        return;
    }
    ThirdPersonDiagnose(clientBase, clientSize);
    uintptr_t hit = ScanForSignature(g_mem, clientBase, clientSize, kThirdPersonOnPattern);
    if (hit == 0)
        hit = ScanForSignature(g_mem, clientBase, clientSize, kThirdPersonPattern);
    if (hit == 0)
    {
        g_stats.note = "特征码没命中（版本可能变了）";
        FileLog("[tp] 第三人称：特征码未命中，功能保持关闭（不会用猜的偏移去写）");
        return;
    }
    uint32_t flagOff = 0;
    uint8_t imm = 0;
    uintptr_t global = ResolveFlagFromOnHandler(hit, 0x180, &flagOff, &imm);
    const bool haveOff = (global != 0);
    if (global == 0 || !haveOff)
    {
        g_stats.note = "解不出全局/偏移";
        FileLog("[tp] 第三人称：特征码命中 0x%llX，但解引用失败，保持关闭",
                (unsigned long long)hit);
        return;
    }
    g_stats.resolved = true;
    g_stats.flagAddr = global + flagOff;
    g_stats.note = "ok";
    FileLog("[tp] 第三人称开关已定位：handler=0x%llX 全局=0x%llX 偏移=0x%X -> 开关字节=0x%llX",
            (unsigned long long)hit, (unsigned long long)global, flagOff,
            (unsigned long long)g_stats.flagAddr);
}

void ThirdPersonTick(const Settings& s)
{
    if (!g_stats.resolved || g_stats.flagAddr == 0)
        return;
    // 只有活着的本地玩家才动它：观战/菜单里改这个开关没有意义，还可能把相机搞乱
    const LocalState& st = GameLocalState();
    if (!st.valid || st.health <= 0)
        return;

    const bool want = BindActive(s.vis.thirdPersonKey) || s.vis.thirdPerson;
    static bool last = false;
    if (want != last)
    {
        last = want;
        const uint8_t v = want ? 1 : 0;
        if (g_mem.Write(g_stats.flagAddr, v))
        {
            ++g_stats.writes;
            g_stats.enabled = want;
            FileLog("[tp] 第三人称 = %s（写入 0x%llX）", want ? "开" : "关",
                    (unsigned long long)g_stats.flagAddr);
        }
    }
}

const ThirdPersonStats& ThirdPersonStatsGet() { return g_stats; }

} // namespace nl




