#include "features/convar.h"
#include "core/log.h"
#include "core/memory.h"
#include "features/game.h"
#include "features/offsets.h"

#include <windows.h>
#include <cstring>
#include <cmath>
#include <vector>

namespace nl {
namespace {

ConVarStats g_stats;

struct FoundVar {
    char      name[48];
    uintptr_t addr;
};
FoundVar g_cache[32];
int      g_cacheCount = 0;
// 负缓存：查过但确实没有的名字（否则每次 miss 都要重扫一遍）
char     g_missed[24][48] = {};
int      g_missedCount = 0;

// ---------------------------------------------------------------------------
//  convar 索引：一次性扫描"像 ConVar 的结构"，之后按名字查表。
//  覆盖两块：(1) dwSensitivity 锚点附近 ±48MB（绝大多数 convar 在这片堆上）；
//           (2) client/engine2/tier0 的数据段。
//  索引没覆盖到的（例如第三人称那批在另一片堆上）走 FindByStringScan 兜底。
// ---------------------------------------------------------------------------
struct VarIndexEntry {
    uintptr_t addr;
    char      name[48];
};
std::vector<VarIndexEntry> g_varIndex;
bool g_varIndexBuilt = false;

bool ReadAscii(uintptr_t addr, char* out, size_t outSize)
{
    if (addr < 0x10000 || outSize < 2)
        return false;
    uint8_t buf[64] = {};
    if (!g_mem.ReadRaw(addr, buf, sizeof(buf) - 1))
        return false;
    size_t n = 0;
    for (size_t i = 0; i + 1 < outSize && i < 63; ++i)
    {
        const uint8_t c = buf[i];
        if (c == 0)
            break;
        if (c < 32 || c > 126)
            return false;
        out[n++] = (char)c;
    }
    out[n] = 0;
    return n > 0;
}

bool LooksLikeConVar(uintptr_t addr, const char* wantName)
{
    char name[64] = { 0 };
    if (!ReadAscii(g_mem.Read<uintptr_t>(addr), name, sizeof(name)))
        return false;
    if (_stricmp(name, wantName) != 0)
        return false;
    char desc[64] = { 0 };
    const uintptr_t descPtr = g_mem.Read<uintptr_t>(addr + 0x20);
    if (descPtr != 0 && !ReadAscii(descPtr, desc, sizeof(desc)))
        return false;
    const float v = g_mem.Read<float>(addr + 0x58);
    if (!std::isfinite(v))
        return false;
    return true;
}

bool NameLooksLikeConVar(const char* name)
{
    const size_t len = strlen(name);
    if (len < 3 || len > 47)
        return false;
    for (size_t i = 0; i < len; ++i)
    {
        const char ch = name[i];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_'))
            return false;
    }
    return true;
}

void IndexWindow(uintptr_t start, uintptr_t end)
{
    for (uintptr_t addr = start; addr + 8 <= end; addr += 8)
    {
        if (!g_mem.IsValidRange(addr, 8))
            continue;
        const uintptr_t p = g_mem.Read<uintptr_t>(addr);
        if (p < 0x10000 || p > 0x00007FFFFFFFFFFFULL)
            continue;
        char name[64] = { 0 };
        if (!ReadAscii(p, name, sizeof(name)))
            continue;
        if (!NameLooksLikeConVar(name))
            continue;
        if (!LooksLikeConVar(addr, name))
            continue;
        VarIndexEntry e;
        e.addr = addr;
        snprintf(e.name, sizeof(e.name), "%s", name);
        g_varIndex.push_back(e);
    }
}

// 兜底：先用"名字字符串"在所有可读私有区域里定位，再在它 ±32MB 邻域反查指向它的指针。
// 比盲目全扫快得多，而且能覆盖不在锚点窗口里的 convar（第三人称那批就在另一片堆上）。
uintptr_t FindByStringScan(const char* name)
{
    const size_t nameLen = strlen(name);
    if (nameLen < 3 || nameLen > 47)
        return 0;

    std::vector<uint8_t> chunk(0x100000 + 64);
    uintptr_t strAddr = 0;
    uintptr_t regionStart = 0;
    uintptr_t regionEnd = 0;

    MEMORY_BASIC_INFORMATION mbi;
    uintptr_t addr = 0x10000;
    while (VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi)) == sizeof(mbi))
    {
        const uintptr_t base = (uintptr_t)mbi.BaseAddress;
        const uintptr_t size = (uintptr_t)mbi.RegionSize;
        const uintptr_t next = base + size;
        if (next <= addr)
            break;
        addr = next;
        if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0)
            continue;
        const DWORD prot = mbi.Protect & 0xFF;
        if (prot != PAGE_READONLY && prot != PAGE_READWRITE &&
            prot != PAGE_EXECUTE_READ && prot != PAGE_EXECUTE_READWRITE)
            continue;

        const uintptr_t end = base + size;
        for (uintptr_t chunkStart = base; chunkStart < end; chunkStart += 0x100000)
        {
            const size_t want = (size_t)((end - chunkStart > 0x100000) ? 0x100000 : (end - chunkStart));
            if (want < nameLen + 1)
                break;
            if (!g_mem.ReadRaw(chunkStart, chunk.data(), want))
                continue;
            for (size_t i = 0; i + nameLen + 1 <= want; ++i)
            {
                if (chunk[i] != (uint8_t)name[0])
                    continue;
                if (memcmp(chunk.data() + i, name, nameLen) != 0)
                    continue;
                if (chunk[i + nameLen] != 0)     // 必须是完整字符串
                    continue;
                strAddr = chunkStart + i;
                break;
            }
            if (strAddr != 0)
                break;
        }
        if (strAddr != 0)
            break;
    }
    if (strAddr == 0)
        return 0;

    const uintptr_t window = 0x2000000;   // ±32MB
    regionStart = (strAddr > window) ? (strAddr - window) : 0x10000;
    regionEnd = strAddr + window;
    for (uintptr_t a = regionStart; a + 8 <= regionEnd; a += 8)
    {
        if (!g_mem.IsValidRange(a, 8))
            continue;
        if (g_mem.Read<uintptr_t>(a) != strAddr)
            continue;
        if (LooksLikeConVar(a, name))
            return a;
    }
    return 0;
}

} // namespace

void BuildVarIndex()
{
    if (g_varIndexBuilt)
        return;
    g_varIndexBuilt = true;

    const uintptr_t anchorGlobal = GameModuleBase() + Off("dwSensitivity");
    const uintptr_t anchor = g_mem.Read<uintptr_t>(anchorGlobal);
    if (anchor > 0x10000)
    {
        const uintptr_t span = 0x3000000;
        IndexWindow((anchor > span) ? (anchor - span) : 0x10000, anchor + span);
    }
    for (const char* modName : { "client.dll", "engine2.dll", "tier0.dll" })
    {
        HMODULE mod = GetModuleHandleA(modName);
        if (mod == nullptr)
            continue;
        auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(mod);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE)
            continue;
        auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(reinterpret_cast<uint8_t*>(mod) + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE)
            continue;
        const uint8_t* base = reinterpret_cast<const uint8_t*>(mod);
        auto* sec = IMAGE_FIRST_SECTION(nt);
        for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec)
        {
            if ((sec->Characteristics & IMAGE_SCN_CNT_INITIALIZED_DATA) == 0)
                continue;
            IndexWindow(reinterpret_cast<uintptr_t>(base + sec->VirtualAddress),
                        reinterpret_cast<uintptr_t>(base + sec->VirtualAddress) + sec->Misc.VirtualSize);
        }
    }
    FileLog("[cvar] convar 索引建立完成：%d 个", (int)g_varIndex.size());
    g_mem.ResetFailedReads();
}

uintptr_t ConVarFind(const char* name)
{
    if (name == nullptr || *name == 0)
        return 0;
    for (int i = 0; i < g_cacheCount; ++i)
        if (_stricmp(g_cache[i].name, name) == 0)
            return g_cache[i].addr;
    for (int i = 0; i < g_missedCount; ++i)
        if (_stricmp(g_missed[i], name) == 0)
            return 0;

    BuildVarIndex();
    uintptr_t found = 0;
    for (const VarIndexEntry& e : g_varIndex)
        if (_stricmp(e.name, name) == 0)
        {
            found = e.addr;
            break;
        }
    if (found == 0)
        found = FindByStringScan(name);

    if (found != 0)
    {
        if (g_cacheCount < (int)(sizeof(g_cache) / sizeof(g_cache[0])))
        {
            snprintf(g_cache[g_cacheCount].name, sizeof(g_cache[g_cacheCount].name), "%s", name);
            g_cache[g_cacheCount].addr = found;
            ++g_cacheCount;
        }
        ++g_stats.found;
        return found;
    }
    if (g_missedCount < (int)(sizeof(g_missed) / sizeof(g_missed[0])))
        snprintf(g_missed[g_missedCount++], sizeof(g_missed[0]), "%s", name);
    return 0;
}

bool ConVarGetFloat(const char* name, float* out)
{
    const uintptr_t v = ConVarFind(name);
    if (v == 0)
        return false;
    const float f = g_mem.Read<float>(v + 0x58);
    if (!std::isfinite(f))
        return false;
    if (out != nullptr)
        *out = f;
    return true;
}

bool ConVarSetFloat(const char* name, float value)
{
    const uintptr_t v = ConVarFind(name);
    if (v == 0)
    {
        g_stats.lastError = "convar 未找到";
        return false;
    }
    if (!LooksLikeConVar(v, name))
    {
        g_stats.lastError = "结构校验失败，拒写";
        return false;
    }
    if (!g_mem.Write(v + 0x58, value))
    {
        g_stats.lastError = "写入失败";
        return false;
    }
    ++g_stats.writes;
    const float back = g_mem.Read<float>(v + 0x58);
    if (fabsf(back - value) > 1e-6f)
    {
        g_stats.lastError = "写入后读回不一致";
        return false;
    }
    return true;
}

bool ConVarGetInt(const char* name, int* out)
{
    const uintptr_t v = ConVarFind(name);
    if (v == 0)
        return false;
    if (out != nullptr)
        *out = g_mem.Read<int>(v + 0x5C);
    return true;
}

bool ConVarSetInt(const char* name, int value)
{
    const uintptr_t v = ConVarFind(name);
    if (v == 0)
    {
        g_stats.lastError = "convar 未找到";
        return false;
    }
    if (!LooksLikeConVar(v, name))
    {
        g_stats.lastError = "结构校验失败，拒写";
        return false;
    }
    // ConVar_t 里 float m_fValue(+0x58) 与 int m_nValue(+0x5C) 是一对：整型两个都要写，
    // 只写 float 的话，按 int 读它的代码会拿到旧值。
    const float f = (float)value;
    if (!g_mem.Write(v + 0x58, f))
    {
        g_stats.lastError = "float 写入失败";
        return false;
    }
    g_mem.Write(v + 0x5C, value);
    ++g_stats.writes;
    const int back = g_mem.Read<int>(v + 0x5C);
    if (back != value)
    {
        g_stats.lastError = "int 写回不一致";
        return false;
    }
    return true;
}

void ConVarProbe(const char* const* names, int count)
{
    for (int i = 0; i < count; ++i)
    {
        const char* n = names[i];
        const uintptr_t v = ConVarFind(n);
        if (v == 0)
        {
            FileLog("[cvarprobe] %-28s 不存在", n);
            continue;
        }
        const float cur = g_mem.Read<float>(v + 0x58);
        const uint32_t flags = g_mem.Read<uint32_t>(v + 0x28);
        FileLog("[cvarprobe] %-28s 存在 值=%.4f flags=0x%X 地址=0x%llX",
                n, (double)cur, flags, (unsigned long long)v);
    }
}

void ConVarSelfTest()
{
    float sens = 0.0f;
    const uintptr_t v = ConVarFind("sensitivity");
    if (v != 0 && ConVarGetFloat("sensitivity", &sens))
    {
        g_stats.selfTestOk = true;
        FileLog("[cvar] 自检通过：sensitivity 对象=0x%llX 当前值=%.3f（ConVar 结构 = name@0x00 / desc@0x20 / value@0x58）",
                (unsigned long long)v, (double)sens);
    }
    else
    {
        g_stats.selfTestOk = false;
        FileLog("[cvar] 自检失败：没找到 sensitivity（convar 扫描机制要检查）");
    }
    g_mem.ResetFailedReads();
}

const ConVarStats& ConVarStatsGet() { return g_stats; }

} // namespace nl
