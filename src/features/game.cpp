#include "features/game.h"
#include "features/offsets.h"
#include "features/misc.h"
#include "features/aimbot.h"
#include "features/visuals.h"
#include "features/thirdperson.h"
#include "core/memory.h"
#include "core/log.h"
#include "core/paths.h"
#include "ui/style.h"
#include <imgui.h>
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>
#include <unordered_map>

namespace nl {

GameStatus g_status;

// 上一帧的视矩阵：用于"双框对比"排查转身时方框与人物错位
static float g_prevMatrix[16] = { 0 };
const float* GamePreviousViewMatrix() { return g_prevMatrix; }

// 本地玩家状态（Misc / 后续 aimbot 共用）
static LocalState g_localState;
const LocalState& GameLocalState() { return g_localState; }

// GameTick 各段耗时（毫秒）：默认累加一个"窗口"（120 帧）后由性能遥测取走并清零。
// 注意要累加 + 记最大值，只打印最后一帧的值会看起来全是 0（扫描是每 60 帧才做一次）。
static double g_phaseScanSum = 0.0, g_phaseScanMax = 0.0;
static double g_phaseEntitySum = 0.0, g_phaseEntityMax = 0.0;
static double g_phaseBoneSum = 0.0, g_phaseBoneMax = 0.0;
static double g_phaseOtherSum = 0.0, g_phaseOtherMax = 0.0;
static double g_phasePreSum = 0.0, g_phasePreMax = 0.0;
static double g_phasePostSum = 0.0, g_phasePostMax = 0.0;
static double g_phaseBombSum = 0.0, g_phaseBombMax = 0.0;
static double g_matrixErrSum = 0.0, g_matrixErrMax = 0.0;
static int    g_matrixErrCount = 0;
static int    g_phaseFrames = 0;

void GameTickPhasesGet(GameTickPhases* out)
{
    if (out == nullptr)
        return;
    const double n = (g_phaseFrames > 0) ? (double)g_phaseFrames : 1.0;
    out->scanMs = g_phaseScanSum / n;
    out->entityMs = g_phaseEntitySum / n;
    out->boneMs = g_phaseBoneSum / n;
    out->otherMs = g_phaseOtherSum / n;
    out->scanMaxMs = g_phaseScanMax;
    out->entityMaxMs = g_phaseEntityMax;
    out->boneMaxMs = g_phaseBoneMax;
    out->otherMaxMs = g_phaseOtherMax;
    out->preMs = g_phasePreSum / n;
    out->preMaxMs = g_phasePreMax;
    out->postMs = g_phasePostSum / n;
    out->postMaxMs = g_phasePostMax;
    out->bombMs = g_phaseBombSum / n;
    out->bombMaxMs = g_phaseBombMax;
    out->matrixErrAvg = (g_matrixErrCount > 0) ? (g_matrixErrSum / g_matrixErrCount) : 0.0;
    out->matrixErrMax = g_matrixErrMax;
    out->frames = g_phaseFrames;
    g_phaseScanSum = g_phaseScanMax = 0.0;
    g_phaseEntitySum = g_phaseEntityMax = 0.0;
    g_phaseBoneSum = g_phaseBoneMax = 0.0;
    g_phaseOtherSum = g_phaseOtherMax = 0.0;
    g_phasePreSum = g_phasePreMax = 0.0;
    g_phasePostSum = g_phasePostMax = 0.0;
    g_phaseBombSum = g_phaseBombMax = 0.0;
    g_matrixErrSum = 0.0;
    g_matrixErrMax = 0.0;
    g_matrixErrCount = 0;
    g_phaseFrames = 0;
}

void GameTickPhasesAdd(int which, double ms)
{
    double* sum = nullptr;
    double* mx = nullptr;
    switch (which)
    {
    case 0: sum = &g_phaseScanSum;   mx = &g_phaseScanMax;   break;
    case 1: sum = &g_phaseEntitySum; mx = &g_phaseEntityMax; break;
    case 2: sum = &g_phaseBoneSum;   mx = &g_phaseBoneMax;   break;
    case 3: sum = &g_phaseOtherSum;  mx = &g_phaseOtherMax;  break;
    case 4: sum = &g_phasePreSum;    mx = &g_phasePreMax;    break;
    case 5: sum = &g_phasePostSum;   mx = &g_phasePostMax;   break;
    case 6: sum = &g_phaseBombSum;   mx = &g_phaseBombMax;   break;
    default: return;
    }
    *sum += ms;
    if (ms > *mx)
        *mx = ms;
}

void GameTickPhasesFrame() { ++g_phaseFrames; }

struct PlayerRecord {
    PlayerView  view;
    std::string name;
    std::string weapon;
    int         clip = 0;
};

// 每个模型的关节索引表：由 ClassifyBones() 从骨架几何 + 左右对称推导出来。
// 键是骨骼数组指针（等于"某个 pawn 的这套骨架"，索引表本身不随姿势变化），
// 推导一次就缓存；推导失败时不缓存，下一帧还会再试。
struct BoneMap {
    int   index[Bone_Count] = {};
    bool  valid = false;
    float headDz = 0.0f;
    float axisDeg = 0.0f;
    float axisCost = 0.0f;
};
std::unordered_map<uintptr_t, BoneMap> g_boneMapCache;

bool BuildBoneMap(uintptr_t boneArr, const float origin[3], float yawDeg, uintptr_t stride,
                  uintptr_t posOff, int boneCount, BoneMap* out)
{
    if (out == nullptr || boneCount <= 0)
        return false;
    // 先把整张表读出来（尾部未初始化的项会被分类器按"离身体太远"丢掉）
    std::vector<BoneSample> samples;
    samples.reserve((size_t)boneCount);
    for (int i = 0; i < boneCount; ++i)
    {
        float p[3] = { 0.0f, 0.0f, 0.0f };
        if (!g_mem.ReadRaw(boneArr + stride * (uintptr_t)i + posOff, p, sizeof(p)))
            break;
        if (!isfinite(p[0]) || !isfinite(p[1]) || !isfinite(p[2]))
            continue;
        BoneSample s;
        s.index = i;
        s.pos = BonePoint{ p[0], p[1], p[2] };
        samples.push_back(s);
    }
    if (samples.size() < 8)
        return false;
    int idx[Bone_Count] = { 0 };
    BoneClassifyReport rep;
    const BonePoint org{ origin[0], origin[1], origin[2] };
    if (!ClassifyBones(samples.data(), (int)samples.size(), org, yawDeg, idx, &rep))
    {
        FileLog("[bones] 分类失败 model=0x%llX bones=%d: %s",
                (unsigned long long)boneArr, (int)samples.size(), rep.reason);
        return false;
    }
    for (int j = 0; j < Bone_Count; ++j)
        out->index[j] = idx[j];
    out->valid = true;
    out->headDz = rep.headDz;
    out->axisDeg = rep.axisDeg;
    out->axisCost = rep.axisCost;
    FileLog("[bones] model=0x%llX bones=%d head=%d(%.1fcm) neck=%d chest=%d pelvis=%d "
            "shoulder=%d/%d elbow=%d/%d hand=%d/%d knee=%d/%d foot=%d/%d 左右轴=%.1f° 对称面误差=%.2fcm",
            (unsigned long long)boneArr, (int)samples.size(), idx[Bone_Head], (double)rep.headDz,
            idx[Bone_Neck], idx[Bone_Chest], idx[Bone_Pelvis],
            idx[Bone_ShoulderR], idx[Bone_ShoulderL], idx[Bone_ElbowR], idx[Bone_ElbowL],
            idx[Bone_HandR], idx[Bone_HandL], idx[Bone_KneeR], idx[Bone_KneeL],
            idx[Bone_FootR], idx[Bone_FootL], (double)rep.axisDeg, (double)rep.axisCost);
    return true;
}

std::vector<PlayerRecord> g_records;
std::vector<PlayerView>   g_views;
std::vector<int>          g_slotState;

void EnsureSlotState()
{
    if ((int)g_slotState.size() != OffsetSlotCount())
        g_slotState.assign((size_t)OffsetSlotCount(), 0);
}

void MarkSlot(const char* name, int state, std::string* report, const std::string& detail)
{
    OffsetSlot* slot = FindOffsetSlot(name);
    if (slot != nullptr)
    {
        const int idx = (int)(slot - &OffsetSlotAt(0));
        EnsureSlotState();
        if (idx >= 0 && idx < (int)g_slotState.size())
            g_slotState[(size_t)idx] = state;
    }
    if (report != nullptr)
    {
        const char* tag = (state == 1) ? "OK  " : (state == 2 ? "BAD " : "MISS");
        *report += "[" + std::string(tag) + "] " + std::string(name) + "  " + detail + "\n";
    }
}

uintptr_t Off(const char* name)
{
    OffsetSlot* s = FindOffsetSlot(name);
    return s ? s->value : 0;
}

// 下面两个函数需要外部链接（注入版渲染层也要直接调用），按 nl 命名空间导出

uintptr_t EntityFromIndex(const Memory& mem, uintptr_t base, int index)
{
    const uintptr_t list = mem.Read<uintptr_t>(base + Off("dwEntityList"));
    if (list == 0)
        return 0;
    // 内部模式解引用前先确认指针可读，避免垃圾指针把游戏带崩
    if (!mem.IsValidRange(list, sizeof(uintptr_t)))
        return 0;
    const int scheme = (int)Off("kEntityScheme");
    const uintptr_t stride = Off("kEntityStride");
    const uintptr_t chunkOffset = Off("kEntityChunkOffset");
    const bool entryIsPointer = (Off("kEntityEntryPointer") != 0);
    auto pick = [&](uintptr_t slot) -> uintptr_t
    {
        const uintptr_t value = entryIsPointer ? mem.Read<uintptr_t>(slot) : slot;
        return mem.IsValidRange(value, 0x10) ? value : 0;
    };
    // scheme 1：线性表（CS:GO 与部分版本）
    if (scheme == 1)
        return pick(list + stride * (uintptr_t)index + chunkOffset);
    // scheme 0：块表 -> 块 -> 条目（CS2）
    const uintptr_t chunk = mem.Read<uintptr_t>(list + 0x8 * ((index & 0x7FFF) >> 9) + chunkOffset);
    if (chunk == 0 || !mem.IsValidRange(chunk, sizeof(uintptr_t)))
        return 0;
    return pick(chunk + stride * (uintptr_t)(index & 0x1FF));
}

// 休眠标记：CS2 在 CGameSceneNode 上，CS:GO 在实体上；读到不合理值就换另一边，避免误杀整个 ESP
int ReadDormant(const Memory& mem, uintptr_t ent, uintptr_t scene)
{
    auto sane = [](int v) { return v == 0 || v == 1; };
    const bool preferScene = (Off("kDormantFromSceneNode") != 0);
    int value = 0;
    if (preferScene)
    {
        if (scene != 0)
            value = (int)mem.Read<uint8_t>(scene + Off("m_bDormant"));
        if (!sane(value))
            value = (int)mem.Read<uint8_t>(ent + Off("m_bDormant"));
    }
    else
    {
        value = (int)mem.Read<uint8_t>(ent + Off("m_bDormant"));
        if (!sane(value) && scene != 0)
            value = (int)mem.Read<uint8_t>(scene + Off("m_bDormant"));
    }
    return sane(value) ? value : 0;
}

// 玩家名：CS2 名字在控制器上（pawn -> m_hController -> controller + m_iszPlayerName），CS:GO 直接在实体上
std::string ReadPlayerName(const Memory& mem, uintptr_t base, uintptr_t ent)
{
    const uintptr_t controllerOffset = Off("m_hController");
    if (controllerOffset != 0)
    {
        const uint32_t handle = mem.Read<uint32_t>(ent + controllerOffset);
        const uintptr_t controller = EntityFromIndex(mem, base, (int)(handle & 0x7FFF));
        if (controller != 0)
        {
            const std::string name = mem.ReadString(controller + Off("m_iszPlayerName"), 32);
            if (!name.empty())
                return name;
        }
    }
    return mem.ReadString(ent + Off("m_iszPlayerName"), 32);
}

// 可见性：优先用 EntitySpottedState_t（CS2），取不到就退回实体上的 m_bSpotted
int ReadSpotted(const Memory& mem, uintptr_t ent)
{
    const uintptr_t stateOffset = Off("m_entitySpottedState");
    if (stateOffset != 0)
    {
        const int spotted = (int)mem.Read<uint8_t>(ent + stateOffset + Off("m_bSpotted"));
        if (spotted == 0 || spotted == 1)
            return spotted;
    }
    const int direct = (int)mem.Read<uint8_t>(ent + Off("m_bSpotted"));
    return (direct == 0 || direct == 1) ? direct : 0;
}

// 判定"这是不是玩家 Pawn"：必须有有效场景节点 + 能通过 m_hController 找到控制器
// （武器/投掷物没有 m_hController，因此这个判据比"血量在 0..100"稳得多）
bool LooksLikePlayerPawn(const Memory& mem, uintptr_t base, uintptr_t ent, uintptr_t* controllerOut = nullptr)
{
    if (ent == 0)
        return false;
    const uintptr_t scene = mem.Read<uintptr_t>(ent + Off("m_pGameSceneNode"));
    if (!mem.IsValidRange(scene, 0x40))
        return false;
    const uintptr_t ctrlOffset = Off("m_hController");
    if (ctrlOffset == 0)
        return true;
    const uint32_t handle = mem.Read<uint32_t>(ent + ctrlOffset);
    if (handle == 0)
        return false;
    const uintptr_t controller = EntityFromIndex(mem, base, (int)(handle & 0x7FFF));
    if (controller == 0 || !mem.IsValidRange(controller, 0x40))
        return false;
    if (controllerOut != nullptr)
        *controllerOut = controller;
    return true;
}

// 实体索引上限：CS2 的玩家索引经常远超 64，用实体系统里的 highestEntityIndex
int EntityIndexLimit(const Memory& mem, uintptr_t base)
{
    const uintptr_t list = mem.Read<uintptr_t>(base + Off("dwEntityList"));
    // 说明：CS2 里 dwGameEntitySystem_highestEntityIndex 读出来的值（这里 223）比真实玩家索引小得多
    // （实测本地 pawn 在索引 418），所以以 kEntityIndexLimit 为准，仅把它当参考
    int limit = (int)Off("kEntityIndexLimit");
    if (limit <= 0)
        limit = 4096;
    const uintptr_t highestOffset = Off("dwGameEntitySystem_highestEntityIndex");
    if (list != 0 && highestOffset != 0 && mem.IsValidRange(list + highestOffset, 4))
    {
        const int highest = mem.Read<int32_t>(list + highestOffset);
        if (highest > limit && highest < 0x4000)
            limit = highest;   // 万一某天这个值比上限还大，就用它
    }
    if (limit < 512)
        limit = 512;
    if (limit > 0x4000)
        limit = 0x4000;
    return limit;
}

const char* WeaponNameFromId(int id)
{
    switch (id)
    {
    case 1: return "deagle";
    case 3: return "fiveseven";
    case 4: return "glock";
    case 7: return "ak47";
    case 8: return "aug";
    case 9: return "awp";
    case 10: return "famas";
    case 11: return "g3sg1";
    case 13: return "galil";
    case 14: return "m249";
    case 16: return "m4a4";
    case 17: return "mac10";
    case 19: return "p90";
    case 20: return "mp5sd";
    case 24: return "ump45";
    case 25: return "xm1014";
    case 26: return "bizon";
    case 27: return "mag7";
    case 28: return "negev";
    case 29: return "sawedoff";
    case 30: return "tec9";
    case 31: return "taser";
    case 32: return "p2000";
    case 33: return "mp7";
    case 34: return "mp9";
    case 35: return "nova";
    case 36: return "p250";
    case 38: return "scar20";
    case 39: return "sg553";
    case 40: return "ssg08";
    case 43: return "flash";
    case 44: return "he";
    case 45: return "smoke";
    case 46: return "molotov";
    case 47: return "decoy";
    case 48: return "incendiary";
    case 49: return "c4";
    case 60: return "m4a1-s";
    case 61: return "usp-s";
    case 63: return "cz75";
    case 64: return "revolver";
    default: return nullptr;
    }
}

bool WorldToScreen(const float m[16], const float world[3], ImVec2* out)
{
    if (out == nullptr)
        return false;
    const float w = m[12] * world[0] + m[13] * world[1] + m[14] * world[2] + m[15];
    if (w < 0.01f)
        return false;
    const float x = m[0] * world[0] + m[1] * world[1] + m[2] * world[2] + m[3];
    const float y = m[4] * world[0] + m[5] * world[1] + m[6] * world[2] + m[7];
    const ImGuiIO& io = ImGui::GetIO();
    out->x = (io.DisplaySize.x * 0.5f) * (1.0f + x / w);
    out->y = (io.DisplaySize.y * 0.5f) * (1.0f - y / w);
    return true;
}

bool GameAttach(const char* processName, const char* moduleName)
{
    // 注入模式：目标就是本进程，模块用 GetModuleHandle 解析；外部模式才去 OpenProcess
    const bool internalMode = (g_mem.Attached() && g_mem.Mode() == MemMode::Internal);
    if (internalMode)
    {
        g_records.clear();
        g_views.clear();
        g_status = GameStatus();
        if (!g_mem.Attached())
            g_mem.Open("self");
    }
    else
    {
        GameDetach();
        if (!g_mem.Open(processName))
        {
            g_status.attached = false;
            g_status.message = "找不到进程（未启动或需要管理员权限）";
            FileLog("[game] attach failed: %s", processName ? processName : "(null)");
            return false;
        }
    }
    g_status.attached = true;
    g_status.process = g_mem.ProcessName();
    g_status.pid = g_mem.Pid();
    g_status.moduleName = moduleName ? moduleName : "client.dll";
    g_status.moduleBase = g_mem.ModuleBase(g_status.moduleName.c_str());
    g_status.moduleSize = g_mem.ModuleSize(g_status.moduleName.c_str());
    if (g_status.moduleBase == 0)
    {
        g_status.ready = false;
        g_status.message = "进程已连接，但找不到模块 " + g_status.moduleName;
        return false;
    }
    const int missing = OffsetsRequiredMissing();
    g_status.ready = (missing == 0);
    char buf[192];
    snprintf(buf, sizeof(buf), "模块 %s @ 0x%llX  ·  缺少必需偏移 %d 项",
             g_status.moduleName.c_str(), (unsigned long long)g_status.moduleBase, missing);
    g_status.message = buf;
    FileLog("[game] attached %s pid=%lu module=%s base=0x%llX missingOffsets=%d",
            g_status.process.c_str(), g_status.pid, g_status.moduleName.c_str(),
            (unsigned long long)g_status.moduleBase, missing);
    return true;
}

void GameDetach()
{
    g_mem.Close();
    g_records.clear();
    g_views.clear();
    g_status = GameStatus();
}

void GameTick(const Settings& s)
{
    const ULONGLONG t0 = GetTickCount64();
    LARGE_INTEGER qf{}, qa{}, qb{};
    QueryPerformanceFrequency(&qf);
    QueryPerformanceCounter(&qa);
    g_records.clear();
    g_views.clear();
    g_status.playersFound = 0;
    g_status.entityScanned = 0;
    g_status.boneHits = 0;
    if (!g_mem.Attached())
    {
        g_status.attached = false;
        g_status.ready = false;
        g_status.message = "未连接目标进程";
        return;
    }
    const uintptr_t base = g_status.moduleBase;
    if (base == 0)
    {
        g_status.ready = false;
        g_status.message = "模块基址无效";
        return;
    }
    const int missing = OffsetsRequiredMissing();
    if (missing > 0)
    {
        g_status.ready = false;
        char buf[128];
        snprintf(buf, sizeof(buf), "还缺 %d 项必需偏移（Memory 页粘贴后即可用）", missing);
        g_status.message = buf;
        return;
    }
    g_status.ready = true;

    float matrix[16] = { 0 };
    if (!g_mem.ReadArray(base + Off("dwViewMatrix"), matrix, 16))
    {
        g_status.message = "视矩阵读取失败";
        return;
    }
    // 排查用：NL_MATRIX_DELAY=N 表示"用 N 帧之前的视矩阵"。
    // 转身时框和模型错位，只可能来自"我们读到的矩阵"和"游戏渲染这一帧用的矩阵"不是同一个；
    // 有了这个开关就能一帧一帧试，看哪一帧的矩阵刚好对上。
    static int matrixDelay = -1;
    if (matrixDelay < 0)
    {
        char dbuf[16] = { 0 };
        GetEnvironmentVariableA("NL_MATRIX_DELAY", dbuf, sizeof(dbuf));
        matrixDelay = atoi(dbuf);
        if (matrixDelay == 0)
            matrixDelay = SwitchInt("matrix_delay", 0);   // nl_switch.ini 里也能配
        if (matrixDelay < 0)
            matrixDelay = 0;
        if (matrixDelay > 4)
            matrixDelay = 4;
        FileLog("[game] 视矩阵延迟 = %d 帧（NL_MATRIX_DELAY 或 nl_switch.ini 的 matrix_delay）", matrixDelay);
    }
    if (matrixDelay > 0)
    {
        static float history[5][16] = {};
        static int   historyCount = 0;
        const int slots = matrixDelay + 1;
        const int writeAt = historyCount % slots;
        float use[16];
        if (historyCount >= matrixDelay)
            memcpy(use, history[(historyCount - matrixDelay) % slots], sizeof(use));
        else
            memcpy(use, matrix, sizeof(use));
        memcpy(history[writeAt], matrix, sizeof(matrix));
        ++historyCount;
        memcpy(matrix, use, sizeof(matrix));
    }
    g_status.localController = g_mem.Read<uintptr_t>(base + Off("dwLocalPlayerController"));
    g_status.localPawn = g_mem.Read<uintptr_t>(base + Off("dwLocalPlayerPawn"));
    if (!g_mem.IsValidRange(g_status.localController, 0x10))
        g_status.localController = 0;
    if (!g_mem.IsValidRange(g_status.localPawn, 0x10))
        g_status.localPawn = 0;
    if (g_status.localPawn == 0 && g_status.localController != 0)
    {
        const uint32_t handle = g_mem.Read<uint32_t>(g_status.localController + Off("m_hPlayerPawn"));
        g_status.localPawn = EntityFromIndex(g_mem, base, (int)(handle & 0x7FFF));
    }

    int localTeam = 0;
    float localOrigin[3] = { 0.0f, 0.0f, 0.0f };
    g_localState = LocalState();
    if (g_status.localPawn != 0)
    {
        localTeam = (int)g_mem.Read<uint8_t>(g_status.localPawn + Off("m_iTeamNum"));
        const uintptr_t scene = g_mem.Read<uintptr_t>(g_status.localPawn + Off("m_pGameSceneNode"));
        const uintptr_t originAddr = (Off("kOriginFromSceneNode") != 0 && scene != 0)
                                         ? scene + Off("m_vecAbsOrigin")
                                         : g_status.localPawn + Off("m_vecAbsOrigin");
        g_mem.ReadRaw(originAddr, localOrigin, sizeof(localOrigin));
        if (!isfinite(localOrigin[0]) || !isfinite(localOrigin[1]) || !isfinite(localOrigin[2]))
            localOrigin[0] = localOrigin[1] = localOrigin[2] = 0.0f;

        // ---- 本地状态：Misc / aimbot 需要的那几个字段 ----
        LocalState& st = g_localState;
        st.valid = true;
        st.pawn = g_status.localPawn;
        for (int i = 0; i < 3; ++i)
        {
            st.origin[i] = localOrigin[i];
            st.velocity[i] = g_mem.Read<float>(g_status.localPawn + Off("m_vecVelocity") + i * 4);
            st.viewAngles[i] = g_mem.Read<float>(base + Off("dwViewAngles") + i * 4);
        }
        st.flags = (int)g_mem.Read<uint32_t>(g_status.localPawn + Off("m_fFlags"));
        st.onGround = (st.flags & 1) != 0;          // FL_ONGROUND
        st.health = (int)g_mem.Read<int32_t>(g_status.localPawn + Off("m_iHealth"));
        st.team = localTeam;
        st.scoped = (g_mem.Read<uint8_t>(g_status.localPawn + Off("m_bIsScoped")) & 1) != 0;
        st.speed2D = sqrtf(st.velocity[0] * st.velocity[0] + st.velocity[1] * st.velocity[1]);
        const float viewOffset = g_mem.Read<float>(g_status.localPawn + Off("m_vecViewOffset") + 8);
        st.eyeZ = localOrigin[2] + (isfinite(viewOffset) ? viewOffset : 64.0f);
        st.flashAlphaAddr = g_status.localPawn + Off("m_flFlashMaxAlpha");
        {
            const uintptr_t ws = g_mem.Read<uintptr_t>(g_status.localPawn + Off("m_pWeaponServices"));
            if (ws != 0 && g_mem.IsValidRange(ws, 0x10))
            {
                const uint32_t handle = g_mem.Read<uint32_t>(ws + Off("m_hActiveWeapon"));
                const uintptr_t weapon = EntityFromIndex(g_mem, base, (int)(handle & 0x7FFF));
                if (weapon != 0)
                {
                    uintptr_t itemBase = weapon;
                    if (Off("m_AttributeManager") != 0)
                        itemBase = weapon + Off("m_AttributeManager") + Off("m_AttributeManager_Item");
                    int defIndex = g_mem.Read<int32_t>(itemBase + Off("m_iItemDefinitionIndex"));
                    if (defIndex <= 0 || defIndex > 70)
                        defIndex = g_mem.Read<int32_t>(weapon + Off("m_iItemDefinitionIndex"));
                    st.weaponId = defIndex;
                }
            }
        }
    }

    // 索引扫描很贵（4096 个索引 × 指针链），改成"每秒全扫一次 + 每帧按缓存索引读"
    QueryPerformanceCounter(&qb);
    GameTickPhasesAdd(4, (double)(qb.QuadPart - qa.QuadPart) * 1000.0 / (double)qf.QuadPart);  // 入口 -> 扫描之前
    QueryPerformanceCounter(&qa);
    static std::vector<int> g_indexCache;
    static int g_rescanCooldown = 0;
    // 注意：扫描很贵（4096 个索引 × 指针链），必须按冷却走；
    // 之前写成"缓存为空就每帧重扫"，在主菜单这种"一个玩家都没有"的状态下
    // 会把每一帧都塞满 4096 次链表遍历（注入版实测直接掉帧）。
    if (g_rescanCooldown <= 0)
    {
        g_rescanCooldown = 60;
        g_indexCache.clear();
        const int limit = EntityIndexLimit(g_mem, base);
        for (int i = 0; i < limit; ++i)
        {
            if (EntityFromIndex(g_mem, base, i) != 0)
                g_indexCache.push_back(i);
        }
    }
    else
    {
        --g_rescanCooldown;
    }
    QueryPerformanceCounter(&qb);
    GameTickPhasesAdd(0, (double)(qb.QuadPart - qa.QuadPart) * 1000.0 / (double)qf.QuadPart);
    QueryPerformanceCounter(&qa);
    g_records.reserve(g_indexCache.size());
    for (int idx : g_indexCache)
    {
        const uintptr_t ent = EntityFromIndex(g_mem, base, idx);
        if (ent == 0)
            continue;
        ++g_status.entityScanned;
        if (ent == g_status.localPawn)
            continue;
        const int health = g_mem.Read<int32_t>(ent + Off("m_iHealth"));
        const int lifeState = (int)g_mem.Read<uint8_t>(ent + Off("m_lifeState"));
        const int team = (int)g_mem.Read<uint8_t>(ent + Off("m_iTeamNum"));
        const uintptr_t dampScene = g_mem.Read<uintptr_t>(ent + Off("m_pGameSceneNode"));
        const int dormant = ReadDormant(g_mem, ent, dampScene);
        if (health <= 0 || health > 200 || lifeState != 0 || dormant != 0)
            continue;
        if (team != 2 && team != 3)
            continue;
        if (s.vis.teamCheck && localTeam != 0 && team == localTeam)
            continue;
        const uintptr_t scene = dampScene;
        float origin[3] = { 0.0f, 0.0f, 0.0f };
        const uintptr_t originAddr = (Off("kOriginFromSceneNode") != 0 && scene != 0)
                                         ? scene + Off("m_vecAbsOrigin")
                                         : ent + Off("m_vecAbsOrigin");
        if (!g_mem.ReadRaw(originAddr, origin, sizeof(origin)))
            continue;
        const float dx = origin[0] - localOrigin[0];
        const float dy = origin[1] - localOrigin[1];
        const float dz = origin[2] - localOrigin[2];
        const float meters = sqrtf(dx * dx + dy * dy + dz * dz) / 52.49f;
        if (s.vis.maxDistance > 0.0f && meters > s.vis.maxDistance)
            continue;

        PlayerRecord rec;
        rec.name = ReadPlayerName(g_mem, base, ent);
        // 只有真正有名字的玩家实体才画（道具/投掷物常被误判成 pawn）
        bool namePrintable = !rec.name.empty();
        for (char c : rec.name)
        {
            const unsigned char u = (unsigned char)c;
            if (u < 32 || u == 127)
            {
                namePrintable = false;
                break;
            }
        }
        if (!namePrintable)
            continue;
        const uintptr_t weaponServices = g_mem.Read<uintptr_t>(ent + Off("m_pWeaponServices"));
        if (weaponServices != 0 && g_mem.IsValidRange(weaponServices, 0x10))
        {
            const uint32_t handle = g_mem.Read<uint32_t>(weaponServices + Off("m_hActiveWeapon"));
            const uintptr_t weapon = EntityFromIndex(g_mem, base, (int)(handle & 0x7FFF));
            if (weapon != 0)
            {
                uintptr_t itemBase = weapon;
                if (Off("m_AttributeManager") != 0)
                    itemBase = weapon + Off("m_AttributeManager") + Off("m_AttributeManager_Item");
                int defIndex = g_mem.Read<int32_t>(itemBase + Off("m_iItemDefinitionIndex"));
                if (defIndex <= 0 || defIndex > 70)
                    defIndex = g_mem.Read<int32_t>(weapon + Off("m_iItemDefinitionIndex"));
                const char* wn = WeaponNameFromId(defIndex);
                rec.weapon = wn ? wn : "";
            }
            const int clip = g_mem.Read<int32_t>(weapon + Off("m_iClip1"));
            rec.clip = clip;
            if (clip >= 0 && clip <= 200)
                rec.view.ammoClip = clip;
            rec.view.reloading = (g_mem.Read<uint8_t>(weapon + Off("m_bInReload")) & 1) != 0;
        }
        QueryPerformanceCounter(&qb);
        GameTickPhasesAdd(1, (double)(qb.QuadPart - qa.QuadPart) * 1000.0 / (double)qf.QuadPart);
        QueryPerformanceCounter(&qa);
        ImVec2 bones[Bone_Count];
    float bonesWorld[Bone_Count][3] = {};   // 关节世界坐标（aimbot 用）
        float worldHead[3] = { 0.0f, 0.0f, 0.0f };
        float worldFootL[3] = { 0.0f, 0.0f, 0.0f };
        float worldFootR[3] = { 0.0f, 0.0f, 0.0f };
        bool  haveBoneWorld = false;
        for (int b = 0; b < Bone_Count; ++b)
            bones[b] = ImVec2(0.0f, 0.0f);
        bool valid[Bone_Count] = {};
        bool hasBones = false;
        if (scene != 0)
        {
            const int layout = (int)Off("kBoneLayout");
            uintptr_t boneArr = (layout == 1)
                                          ? g_mem.Read<uintptr_t>(ent + Off("m_dwBoneMatrix"))
                                          : g_mem.Read<uintptr_t>(scene + Off("m_modelState") + Off("m_modelState_boneOffset"));
            if (!g_mem.IsValidRange(boneArr, 0x20))
                boneArr = 0;
            if (boneArr != 0)
            {
                const int boneCount = (int)Off("kBoneCount");
                const uintptr_t stride = (layout == 1) ? 0x30 : Off("kBoneStep");
                const uintptr_t posOffset = (layout == 1) ? 0x0C : 0;
                // 每个模型的关节索引由 ClassifyBones() 推导一次并缓存
                if (g_boneMapCache.size() > 512)
                    g_boneMapCache.clear();
                BoneMap& map = g_boneMapCache[boneArr];
                if (!map.valid)
                {
                    float eye[3] = { 0.0f, 0.0f, 0.0f };
                    g_mem.ReadRaw(ent + Off("m_angEyeAngles"), eye, sizeof(eye));
                    const float yaw = isfinite(eye[1]) ? eye[1] : 0.0f;
                    BuildBoneMap(boneArr, origin, yaw, stride, posOffset, boneCount, &map);
                }
                auto readBoneIdx = [&](int boneIdx, ImVec2* out) -> bool
                {
                    if (boneIdx < 0 || boneIdx >= boneCount)
                        return false;
                    float pos[3] = { 0.0f, 0.0f, 0.0f };
                    if (!g_mem.ReadRaw(boneArr + stride * (uintptr_t)boneIdx + posOffset, pos, sizeof(pos)))
                        return false;
                    return WorldToScreen(matrix, pos, out);
                };
                // 世界坐标版本：留给"换矩阵重投影"用（排查框和人物错位）
                auto readBoneWorld = [&](int boneIdx, float* out) -> bool
                {
                    if (boneIdx < 0 || boneIdx >= boneCount)
                        return false;
                    return g_mem.ReadRaw(boneArr + stride * (uintptr_t)boneIdx + posOffset, out, sizeof(float) * 3);
                };
                if (map.valid)
                {
                    for (int j = 0; j < Bone_Count; ++j)
                        valid[j] = readBoneIdx(map.index[j], &bones[j]);
                    // 盒子需要"头顶 + 两只脚"：这三根齐了才用真实骨骼画
                    hasBones = valid[Bone_Head] && valid[Bone_FootL] && valid[Bone_FootR];
                }
                else
                {
                    // 分类失败时退回固定索引（offsets.ini 里的 kBoneIdx*，可能不准但不至于空着）
                    valid[Bone_Head] = readBoneIdx((int)Off("kBoneIdxHead"), &bones[Bone_Head]);
                    valid[Bone_Neck] = readBoneIdx((int)Off("kBoneIdxNeck"), &bones[Bone_Neck]);
                    valid[Bone_Chest] = readBoneIdx((int)Off("kBoneIdxChest"), &bones[Bone_Chest]);
                    valid[Bone_Pelvis] = readBoneIdx((int)Off("kBoneIdxPelvis"), &bones[Bone_Pelvis]);
                    valid[Bone_FootL] = readBoneIdx((int)Off("kBoneIdxLeftFoot"), &bones[Bone_FootL]);
                    valid[Bone_FootR] = readBoneIdx((int)Off("kBoneIdxRightFoot"), &bones[Bone_FootR]);
                    hasBones = valid[Bone_Head] && valid[Bone_Neck];
                }
                if (hasBones)
                    ++g_status.boneHits;
                // 顺手把"头 + 两只脚"的世界坐标也取出来（双框对比用，代价 3 次读）
                if (hasBones && map.valid)
                {
                    haveBoneWorld = readBoneWorld(map.index[Bone_Head], worldHead) &&
                                    readBoneWorld(map.index[Bone_FootL], worldFootL) &&
                                    readBoneWorld(map.index[Bone_FootR], worldFootR);
                }
            }
        }
        ImVec2 feet;
        if (!WorldToScreen(matrix, origin, &feet))
            continue;
        ImVec2 head;
        float headWorldOut[3] = { 0.0f, 0.0f, 0.0f };
        float feetWorldOut[3] = { 0.0f, 0.0f, 0.0f };
        bool haveWorld = false;
        if (hasBones)
        {
            head = bones[Bone_Head];
            const ImVec2 footMid((bones[Bone_FootL].x + bones[Bone_FootR].x) * 0.5f,
                                 (bones[Bone_FootL].y + bones[Bone_FootR].y) * 0.5f);
            // 骨骼索引不对时脚骨可能落到离谱位置，这里只接受"在头下方且不超过身高 1.6 倍"的结果
            const float boneHeight = footMid.y - head.y;
            const float originHeight = feet.y - head.y;
            if (footMid.y > head.y && boneHeight < originHeight * 1.6f)
                feet = footMid;
            // 世界坐标（头骨 + 两脚中点）——双框对比要用
            if (haveBoneWorld)
            {
                headWorldOut[0] = worldHead[0];
                headWorldOut[1] = worldHead[1];
                headWorldOut[2] = worldHead[2];
                feetWorldOut[0] = (worldFootL[0] + worldFootR[0]) * 0.5f;
                feetWorldOut[1] = (worldFootL[1] + worldFootR[1]) * 0.5f;
                feetWorldOut[2] = (worldFootL[2] + worldFootR[2]) * 0.5f;
                haveWorld = true;
            }
        }
        else
        {
            const float headPos[3] = { origin[0], origin[1], origin[2] + 72.0f };
            if (!WorldToScreen(matrix, headPos, &head))
                continue;
            headWorldOut[0] = headPos[0];
            headWorldOut[1] = headPos[1];
            headWorldOut[2] = headPos[2];
            feetWorldOut[0] = origin[0];
            feetWorldOut[1] = origin[1];
            feetWorldOut[2] = origin[2];
            haveWorld = true;
        }
        rec.view.head = head;
        rec.view.feet = feet;
        // 统计"换上一帧矩阵"会挪动多少像素：视角一转这个数就是 1 帧误差的量级
        if (haveWorld && g_prevMatrix[15] != 0.0f)
        {
            ImVec2 h2;
            if (WorldToScreen(g_prevMatrix, headWorldOut, &h2))
            {
                const float dx = h2.x - head.x;
                const float dy = h2.y - head.y;
                const float d = sqrtf(dx * dx + dy * dy);
                if (d < 600.0f)   // 排除传送/切镜头这种瞬移
                {
                    g_matrixErrSum += d;
                    if (d > g_matrixErrMax)
                        g_matrixErrMax = d;
                    ++g_matrixErrCount;
                }
            }
        }
        rec.view.hasWorld = haveWorld;
        if (haveWorld)
        {
            for (int k = 0; k < 3; ++k)
            {
                rec.view.headWorld[k] = headWorldOut[k];
                rec.view.feetWorld[k] = feetWorldOut[k];
            }
        }
        rec.view.ent = ent;
rec.view.hasBones = hasBones;
        for (int b = 0; b < Bone_Count; ++b)
            rec.view.bones[b] = bones[b];
        for (int b = 0; b < Bone_Count; ++b)
            rec.view.bonesValid[b] = valid[b];
        for (int b = 0; b < Bone_Count; ++b)
            for (int k = 0; k < 3; ++k)
                rec.view.bonesWorld[b][k] = bonesWorld[b][k];
        QueryPerformanceCounter(&qb);
        GameTickPhasesAdd(2, (double)(qb.QuadPart - qa.QuadPart) * 1000.0 / (double)qf.QuadPart);
        rec.view.health = (float)health;
        rec.view.distance = meters;
        rec.view.team = team;
        rec.view.visible = (team == localTeam) || (ReadSpotted(g_mem, ent) != 0);
        rec.view.ammo = (float)rec.clip;
        rec.view.defuser = g_mem.Read<uint8_t>(ent + Off("m_bIsDefusing")) != 0;
        rec.view.scoped = g_mem.Read<uint8_t>(ent + Off("m_bIsScoped")) != 0;
        rec.view.armor = (float)g_mem.Read<int32_t>(ent + Off("m_ArmorValue"));
        if (rec.view.armor < 0.0f || rec.view.armor > 100.0f)
            rec.view.armor = 0.0f;
        {
            // 头盔：pawn -> ItemServices -> m_bHasHelmet
            const uintptr_t itemServices = g_mem.Read<uintptr_t>(ent + Off("m_pItemServices"));
            rec.view.helmet = (itemServices != 0 && g_mem.IsValidRange(itemServices, 0x60)) &&
                              (g_mem.Read<uint8_t>(itemServices + Off("m_bHasHelmet")) & 1) != 0;
            // 闪光强度 0..1
            float flash = g_mem.Read<float>(ent + Off("m_flFlashDuration"));
            if (!isfinite(flash) || flash < 0.0f)
                flash = 0.0f;
            rec.view.flashed = ImMin(flash, 1.0f);
        }
        // 控制器上的金钱 / 延迟（CS2 玩家自己的信息在 controller 上）
        {
            const uint32_t ctrlHandle = (Off("m_hController") != 0) ? g_mem.Read<uint32_t>(ent + Off("m_hController")) : 0;
            const uintptr_t ctrl = (ctrlHandle != 0) ? EntityFromIndex(g_mem, base, (int)(ctrlHandle & 0x7FFF)) : 0;
            if (ctrl != 0 && g_mem.IsValidRange(ctrl, 0x40))
            {
                const int ping = g_mem.Read<int32_t>(ctrl + Off("m_iPing"));
                if (ping >= 0 && ping < 1000)
                    rec.view.ping = ping;
                const uintptr_t moneyServices = g_mem.Read<uintptr_t>(ctrl + Off("m_pInGameMoneyServices"));
                if (moneyServices != 0 && g_mem.IsValidRange(moneyServices, 0x60))
                {
                    const int money = g_mem.Read<int32_t>(moneyServices + Off("m_iAccount"));
                    if (money >= 0 && money <= 20000)
                        rec.view.money = money;
                }
            }
        }
        g_records.push_back(std::move(rec));
    }

    g_views.reserve(g_records.size());
    for (auto& rec : g_records)
    {
        rec.view.name = rec.name.c_str();
        rec.view.weapon = rec.weapon.empty() ? "" : rec.weapon.c_str();
        g_views.push_back(rec.view);
    }
    g_status.playersFound = (int)g_views.size();
    g_status.tickMs = (float)(GetTickCount64() - t0);
    GameTickPhasesFrame();
    QueryPerformanceCounter(&qb);
    GameTickPhasesAdd(5, (double)(qb.QuadPart - qa.QuadPart) * 1000.0 / (double)qf.QuadPart);  // 每个玩家剩下的零碎读取
    QueryPerformanceCounter(&qa);
    // ---- C4 / 炸弹计时 ----
    {
        BombInfo& bomb = BombMutable();
        bomb = BombInfo();
        const uintptr_t c4 = g_mem.Read<uintptr_t>(base + Off("dwPlantedC4"));
        if (c4 != 0 && g_mem.IsValidRange(c4, 0x40))
        {
            bomb.valid = true;
            bomb.ticking = (g_mem.Read<uint8_t>(c4 + Off("m_bBombTicking")) & 1) != 0;
            bomb.blowTime = g_mem.Read<float>(c4 + Off("m_flC4Blow"));
            bomb.timerLength = g_mem.Read<float>(c4 + Off("m_flTimerLength"));
            bomb.site = g_mem.Read<int32_t>(c4 + Off("m_nBombSite"));
            bomb.beingDefused = (g_mem.Read<uint8_t>(c4 + Off("m_bBeingDefused")) & 1) != 0;
            bomb.defuseEndTime = g_mem.Read<float>(c4 + Off("m_flDefuseCountDown"));
            const uint32_t defuserHandle = g_mem.Read<uint32_t>(c4 + Off("m_hBombDefuser"));
            if (defuserHandle != 0)
            {
                const uintptr_t ctrl = EntityFromIndex(g_mem, base, (int)(defuserHandle & 0x7FFF));
                if (ctrl != 0 && g_mem.IsValidRange(ctrl, 0x40))
                {
                    const std::string n = g_mem.ReadString(ctrl + Off("m_iszPlayerName"), 31);
                    snprintf(bomb.defuserName, sizeof(bomb.defuserName), "%s", n.c_str());
                }
            }
            if (!bomb.ticking)
            {
                bomb.valid = false;
            }
        }
    }
    QueryPerformanceCounter(&qb);
    GameTickPhasesAdd(6, (double)(qb.QuadPart - qa.QuadPart) * 1000.0 / (double)qf.QuadPart);  // C4 块
    // 记下这一帧的视矩阵，下一帧就能用"上一帧的矩阵"再投影一次做对比
    memcpy(g_prevMatrix, matrix, sizeof(g_prevMatrix));
    // Misc 行为（bhop/strafe/trigger/noFlash/autoPeek）：数据已经是最新的，放最后跑
    MiscTick(s);
    // 瞄具目标解算（用刚更新好的玩家/骨骼数据）
    AimbotTick(s, 0.0f);
    // 视觉类字段写入（雷达/发光/FOV）
    VisualsTick(s);
    // 第三人称（写状态字节）
    ThirdPersonTick(s);
}

void GameDrawEsp(const Settings& s, ImDrawList* dl)
{
    if (!s.vis.enable || !g_status.ready)
        return;
    for (const auto& p : g_views)
        DrawPlayerOverlay(dl, p, s.vis, 1.0f);
}

const GameStatus& GameStatusGet() { return g_status; }
uintptr_t GameModuleBase() { return g_status.moduleBase; }
float GameLocalSimulationTime()
{
    if (!g_mem.Attached() || g_status.moduleBase == 0)
        return 0.0f;
    const uintptr_t pawn = g_mem.Read<uintptr_t>(g_status.moduleBase + Off("dwLocalPlayerPawn"));
    if (pawn == 0 || !g_mem.IsValidRange(pawn, 0x40))
        return 0.0f;
    const float sim = g_mem.Read<float>(pawn + Off("m_flSimulationTime"));
    return isfinite(sim) ? sim : 0.0f;
}

void GameLogEntityFields(uintptr_t ent, int index)
{
    if (ent == 0 || !g_mem.IsValidRange(ent, 0x40))
        return;
    const int hp = g_mem.Read<int32_t>(ent + Off("m_iHealth"));
    const int team = (int)g_mem.Read<uint8_t>(ent + Off("m_iTeamNum"));
    const int life = (int)g_mem.Read<uint8_t>(ent + Off("m_lifeState"));
    const uintptr_t scene = g_mem.Read<uintptr_t>(ent + Off("m_pGameSceneNode"));
    float origin[3] = { 0.0f, 0.0f, 0.0f };
    bool sceneOk = (scene != 0 && g_mem.IsValidRange(scene, 0x120));
    if (sceneOk)
        g_mem.ReadRaw(scene + Off("m_vecAbsOrigin"), origin, sizeof(origin));
    const uint32_t ctrlHandle = g_mem.Read<uint32_t>(ent + Off("m_hController"));
    const uintptr_t ctrl = (ctrlHandle != 0) ? EntityFromIndex(g_mem, g_status.moduleBase, (int)(ctrlHandle & 0x7FFF)) : 0;
    std::string name;
    if (ctrl != 0 && g_mem.IsValidRange(ctrl, 0x40))
        name = g_mem.ReadString(ctrl + Off("m_iszPlayerName"), 31);
    FileLog("[probe] idx=%d ptr=0x%llX hp=%d team=%d life=%d scene=%s origin=(%.0f,%.0f,%.0f) ctrl=0x%llX name=\"%s\"",
            index, (unsigned long long)ent, hp, team, life, sceneOk ? "ok" : "bad",
            (double)origin[0], (double)origin[1], (double)origin[2], (unsigned long long)ctrl, name.c_str());
}
const std::vector<PlayerView>& GamePlayerList() { return g_views; }
int GameSlotState(int slotIndex)
{
    EnsureSlotState();
    if (slotIndex < 0 || slotIndex >= (int)g_slotState.size())
        return 0;
    return g_slotState[(size_t)slotIndex];
}

// 名字偏移标定：要求"多个控制器的同一偏移都能读出像人名的字符串"，排掉偶然命中
int GameCalibrateNameOffset(std::string* report)
{
    std::string out;
    auto finish = [&](int code)
    {
        if (report != nullptr)
            *report = out;
        return code;
    };
    if (!g_mem.Attached() || g_status.moduleBase == 0)
    {
        out = "未连接目标进程或模块基址无效。";
        return finish(-1);
    }
    const uintptr_t base = g_status.moduleBase;
    // 收集样本控制器：本地 + 前几个玩家 pawn 的控制器
    uintptr_t controllers[6] = { 0, 0, 0, 0, 0, 0 };
    int controllerCount = 0;
    const uintptr_t localCtrl = g_mem.Read<uintptr_t>(base + Off("dwLocalPlayerController"));
    if (localCtrl != 0 && g_mem.IsValidRange(localCtrl, 0x40))
        controllers[controllerCount++] = localCtrl;
    const int limit = EntityIndexLimit(g_mem, base);
    for (int i = 1; i < limit && controllerCount < 6; ++i)
    {
        const uintptr_t ent = EntityFromIndex(g_mem, base, i);
        if (ent == 0)
            continue;
        const uint32_t handle = g_mem.Read<uint32_t>(ent + Off("m_hController"));
        if (handle == 0)
            continue;
        const uintptr_t ctrl = EntityFromIndex(g_mem, base, (int)(handle & 0x7FFF));
        if (ctrl == 0 || !g_mem.IsValidRange(ctrl, 0x40))
            continue;
        bool duplicate = false;
        for (int c = 0; c < controllerCount; ++c)
            duplicate = duplicate || (controllers[c] == ctrl);
        if (!duplicate)
            controllers[controllerCount++] = ctrl;
    }
    if (controllerCount == 0)
    {
        out = "拿不到控制器样本（先进地图）。";
        return finish(-1);
    }
    auto looksLikeName = [&](const char* buf, int len)
    {
        if (len <= 0 || len > 24)
            return false;
        int visible = 0;
        for (int i = 0; i < len; ++i)
        {
            const unsigned char u = (unsigned char)buf[i];
            if (u < 32 || u == 127)
                return false;
            if (u != ' ')
                ++visible;
        }
        return visible >= 2;
    };
    uintptr_t foundOffset = 0;
    char sample[64] = { 0 };
    for (uintptr_t off = 0x600; off <= 0x820; off += 4)
    {
        bool allOk = true;
        for (int c = 0; c < controllerCount && allOk; ++c)
        {
            char buf[32] = { 0 };
            if (!g_mem.ReadRaw(controllers[c] + off, buf, 24))
            {
                allOk = false;
                break;
            }
            const size_t len = strnlen_s(buf, 24);
            if (len == 0 || len >= 24 || !looksLikeName(buf, (int)len))
                allOk = false;
        }
        if (allOk)
        {
            foundOffset = off;
            g_mem.ReadRaw(controllers[0] + off, sample, sizeof(sample) - 1);
            break;
        }
    }
    char line[256];
    snprintf(line, sizeof(line), "名字偏移标定：控制器样本 %d 个\n", controllerCount);
    out += line;
    if (foundOffset == 0)
    {
        out += "  0x600..0x820 未找到「所有样本都像人名的字符串」\n";
        return finish(1);
    }
    OffsetSlot* slot = FindOffsetSlot("m_iszPlayerName");
    if (slot != nullptr)
    {
        slot->value = foundOffset;
        slot->filled = true;
    }
    OffsetsSave();
    snprintf(line, sizeof(line), "  ✅ m_iszPlayerName = 0x%llX   样本名字=\"%.24s\"\n",
             (unsigned long long)foundOffset, sample);
    out += line;
    FileLog("[name] m_iszPlayerName calibrated to 0x%llX (\"%s\")", (unsigned long long)foundOffset, sample);
    return finish(1);
}

// 用已知指针反推句柄偏移：pawn + m_hController -> controller，controller + m_hPlayerPawn -> pawn
int GameCalibrateHandleOffsets(std::string* report)
{
    std::string out;
    auto finish = [&](int code)
    {
        if (report != nullptr)
            *report = out;
        return code;
    };
    if (!g_mem.Attached() || g_status.moduleBase == 0)
    {
        out = "未连接目标进程或模块基址无效。";
        return finish(-1);
    }
    const uintptr_t base = g_status.moduleBase;
    const uintptr_t pawn = g_mem.Read<uintptr_t>(base + Off("dwLocalPlayerPawn"));
    const uintptr_t controller = g_mem.Read<uintptr_t>(base + Off("dwLocalPlayerController"));
    if (pawn == 0 || controller == 0 || !g_mem.IsValidRange(pawn, 0x40) || !g_mem.IsValidRange(controller, 0x40))
    {
        out = "本地 pawn/controller 不可用（进地图后再标定）。";
        return finish(-1);
    }
    char line[256];

    out += "句柄字段标定（已知本地 pawn/controller 指针作为锚点）：\n";
    bool ok = false;
    // pawn 侧：找 m_hController
    {
        uintptr_t foundOffset = 0;
        for (uintptr_t off = 0x1000; off <= 0x1600; off += 4)
        {
            const uint32_t handle = g_mem.Read<uint32_t>(pawn + off);
            if (handle == 0 || handle == 0xFFFFFFFF)
                continue;
            const int index = (int)(handle & 0x7FFF);
            if (index <= 0 || index > 0x4000)
                continue;
            if (EntityFromIndex(g_mem, base, index) == controller)
            {
                foundOffset = off;
                break;
            }
        }
        if (foundOffset != 0)
        {
            OffsetSlot* slot = FindOffsetSlot("m_hController");
            if (slot)
            {
                slot->value = foundOffset;
                slot->filled = true;
            }
            snprintf(line, sizeof(line), "  ✅ m_hController = 0x%llX\n", (unsigned long long)foundOffset);
            out += line;
            FileLog("[handle] m_hController calibrated to 0x%llX", (unsigned long long)foundOffset);
            ok = true;
        }
        else
            out += "  m_hController：0x1000..0x1600 未命中（句柄编码可能不同）\n";
    }
    // controller 侧：找 m_hPlayerPawn
    {
        uintptr_t foundOffset = 0;
        for (uintptr_t off = 0x800; off <= 0xA00; off += 4)
        {
            const uint32_t handle = g_mem.Read<uint32_t>(controller + off);
            if (handle == 0 || handle == 0xFFFFFFFF)
                continue;
            const int index = (int)(handle & 0x7FFF);
            if (index <= 0 || index > 0x4000)
                continue;
            if (EntityFromIndex(g_mem, base, index) == pawn)
            {
                foundOffset = off;
                break;
            }
        }
        if (foundOffset != 0)
        {
            OffsetSlot* slot = FindOffsetSlot("m_hPlayerPawn");
            if (slot)
            {
                slot->value = foundOffset;
                slot->filled = true;
            }
            snprintf(line, sizeof(line), "  ✅ m_hPlayerPawn = 0x%llX\n", (unsigned long long)foundOffset);
            out += line;
            FileLog("[handle] m_hPlayerPawn calibrated to 0x%llX", (unsigned long long)foundOffset);
            ok = true;
        }
        else
            out += "  m_hPlayerPawn：0x800..0xA00 未命中\n";
    }
    if (ok)
        OffsetsSave();
    return finish(ok ? 1 : 1);
}

// 实体列表自动标定：已知 controller / pawn 指针，反推块表参数（版本更新后一键修）
int GameCalibrateEntityList(std::string* report)
{
    std::string out;
    auto finish = [&](int code)
    {
        if (report != nullptr)
            *report = out;
        return code;
    };
    if (!g_mem.Attached() || g_status.moduleBase == 0)
    {
        out = "未连接目标进程或模块基址无效，先 Attach。";
        return finish(-1);
    }
    const uintptr_t base = g_status.moduleBase;
    const uintptr_t list = g_mem.Read<uintptr_t>(base + Off("dwEntityList"));
    if (list == 0)
    {
        out = "dwEntityList 读出来是 0，先确认这个模块偏移。";
        return finish(-1);
    }
    // 已知目标：本地控制器 / 本地 pawn（以及 pawn 的控制器句柄 index）
    const uintptr_t knownController = g_mem.Read<uintptr_t>(base + Off("dwLocalPlayerController"));
    const uintptr_t knownPawn = g_mem.Read<uintptr_t>(base + Off("dwLocalPlayerPawn"));
    if (knownController == 0 && knownPawn == 0)
    {
        out = "本地玩家指针为空（主菜单/未进地图）：进地图拿到实体后再标定。";
        return finish(-1);
    }
    int pawnIndex = -1;
    if (knownPawn != 0 && Off("m_hController") != 0)
    {
        const uint32_t handle = g_mem.Read<uint32_t>(knownPawn + Off("m_hController"));
        if (handle != 0)
            pawnIndex = (int)(handle & 0x7FFF);
    }

    struct Candidate {
        int scheme;
        uintptr_t stride;
        uintptr_t chunkOffset;
        bool entryIsPointer;
    };
    const uintptr_t strides[4] = { 0x78, 0x70, 0x80, 0x68 };
    const uintptr_t chunkOffsets[4] = { 0x10, 0x8, 0x18, 0x0 };
    const bool pointerModes[2] = { true, false };
    int foundScheme = -1;
    uintptr_t foundStride = 0;
    uintptr_t foundChunkOffset = 0;
    bool foundPointer = true;
    uintptr_t hitValue = 0;
    int hitIndex = -1;
    int attempts = 0;

    auto probe = [&](int scheme, uintptr_t stride, uintptr_t chunkOffset, bool entryIsPointer, int index) -> uintptr_t
    {
        if (index < 0 || index > 0x7FFF)
            return 0;
        ++attempts;
        auto slotToValue = [&](uintptr_t slot) -> uintptr_t
        {
            const uintptr_t v = entryIsPointer ? g_mem.Read<uintptr_t>(slot) : slot;
            return g_mem.IsValidRange(v, 0x40) ? v : 0;
        };
        if (scheme == 1)
            return slotToValue(list + stride * (uintptr_t)index + chunkOffset);
        const uintptr_t chunk = g_mem.Read<uintptr_t>(list + 0x8 * ((index & 0x7FFF) >> 9) + chunkOffset);
        if (chunk == 0 || !g_mem.IsValidRange(chunk, 0x10))
            return 0;
        return slotToValue(chunk + stride * (uintptr_t)(index & 0x1FF));
    };
    auto matches = [&](uintptr_t value) -> bool
    {
        return value != 0 && ((knownController != 0 && value == knownController) ||
                              (knownPawn != 0 && value == knownPawn));
    };

    // 用 pawn 的控制器句柄 index 去找 controller；再用 controller 反查 pawn
    for (int s = 0; s < 2 && foundScheme < 0; ++s)
    {
        for (int st = 0; st < 4 && foundScheme < 0; ++st)
        {
            for (int co = 0; co < 4 && foundScheme < 0; ++co)
            {
                for (int pm = 0; pm < 2 && foundScheme < 0; ++pm)
                {
                    if (pawnIndex >= 0)
                    {
                        const uintptr_t v = probe(s, strides[st], chunkOffsets[co], pointerModes[pm], pawnIndex);
                        if (matches(v))
                        {
                            foundScheme = s;
                            foundStride = strides[st];
                            foundChunkOffset = chunkOffsets[co];
                            foundPointer = pointerModes[pm];
                            hitValue = v;
                            hitIndex = pawnIndex;
                        }
                    }
                    if (foundScheme < 0 && knownController != 0)
                    {
                        // controller -> pawn 句柄同样可以当锚点
                        const uint32_t h = g_mem.Read<uint32_t>(knownController + Off("m_hPlayerPawn"));
                        const int ctrlPawnIndex = (int)(h & 0x7FFF);
                        const uintptr_t v = probe(s, strides[st], chunkOffsets[co], pointerModes[pm], ctrlPawnIndex);
                        if (matches(v))
                        {
                            foundScheme = s;
                            foundStride = strides[st];
                            foundChunkOffset = chunkOffsets[co];
                            foundPointer = pointerModes[pm];
                            hitValue = v;
                            hitIndex = ctrlPawnIndex;
                        }
                    }
                }
            }
        }
    }

    char line[256];
    snprintf(line, sizeof(line), "实体列表标定：尝试 %d 种组合，锚点 controller=0x%llX pawn=0x%llX index=%d\n",
             attempts, (unsigned long long)knownController, (unsigned long long)knownPawn, pawnIndex);
    out += line;
    if (foundScheme < 0)
    {
        out += "  没有组合能还原出已知指针：可能不是列表结构问题（先确认 m_hController / m_hPlayerPawn 句柄偏移）\n";
        FileLog("[elist] calibration failed after %d attempts", attempts);
        return finish(1);
    }
    struct Write { const char* name; uintptr_t value; };
    const Write writes[4] = {
        { "kEntityScheme", (uintptr_t)foundScheme },
        { "kEntityStride", foundStride },
        { "kEntityChunkOffset", foundChunkOffset },
        { "kEntityEntryPointer", foundPointer ? 1u : 0u },
    };
    for (const Write& w : writes)
    {
        OffsetSlot* slot = FindOffsetSlot(w.name);
        if (slot != nullptr)
        {
            slot->value = w.value;
            slot->filled = true;
        }
    }
    OffsetsSave();
    snprintf(line, sizeof(line), "  ✅ 命中：scheme=%s  stride=0x%llX  chunkOffset=0x%llX  条目是%s\n",
             foundScheme == 1 ? "线性表" : "块表", (unsigned long long)foundStride,
             (unsigned long long)foundChunkOffset, foundPointer ? "指针" : "实体本体");
    out += line;
    snprintf(line, sizeof(line), "  用 index=%d 解析出 0x%llX（与已知指针一致），已写回 offsets.ini\n",
             hitIndex, (unsigned long long)hitValue);
    out += line;
    out += "  建议再点一次「校验当前偏移」确认玩家 Pawn 数目正常。";
    FileLog("[elist] calibrated scheme=%d stride=0x%llX chunkOffset=0x%llX pointer=%d",
            foundScheme, (unsigned long long)foundStride, (unsigned long long)foundChunkOffset, foundPointer ? 1 : 0);
    return finish(1);
}

// 骨骼自动标定：不知道骨骼索引时，直接扫骨骼数组，按几何关系反推 头/颈/胸/骨盆/脚
int GameCalibrateBones(std::string* report)
{
    std::string out;
    auto done = [&](int code)
    {
        if (report != nullptr)
            *report = out;
        return code;
    };
    if (!g_mem.Attached() || g_status.moduleBase == 0)
    {
        out = "未连接目标进程或模块基址无效，先 Attach。";
        return done(-1);
    }
    const uintptr_t base = g_status.moduleBase;
    // 样本优先用本地 pawn（一定是真玩家、一定有骨骼），退化时才去列表里找一个
    uintptr_t sample = g_mem.Read<uintptr_t>(base + Off("dwLocalPlayerPawn"));
    if (sample == 0 || !g_mem.IsValidRange(sample, 0x40))
    {
        sample = 0;
        const int maxPlayers = EntityIndexLimit(g_mem, base);
        for (int i = 1; i < maxPlayers; ++i)
        {
            const uintptr_t ent = EntityFromIndex(g_mem, base, i);
            if (ent == 0)
                continue;
            const int hp = g_mem.Read<int32_t>(ent + Off("m_iHealth"));
            const int team = (int)g_mem.Read<uint8_t>(ent + Off("m_iTeamNum"));
            if (hp > 0 && hp <= 100 && (team == 2 || team == 3))
            {
                sample = ent;
                break;
            }
        }
    }
    if (sample == 0)
    {
        out = "找不到可用样本实体：先进地图（本地 pawn 不可用时才需要列表）。";
        return done(-1);
    }
    const uintptr_t scene = g_mem.Read<uintptr_t>(sample + Off("m_pGameSceneNode"));
    if (scene == 0)
    {
        out = "m_pGameSceneNode 无效，先修好场景节点偏移。";
        return done(-1);
    }
    float origin[3] = { 0.0f, 0.0f, 0.0f };
    g_mem.ReadRaw(scene + Off("m_vecAbsOrigin"), origin, sizeof(origin));

    const int layout = (int)Off("kBoneLayout");
    const uintptr_t stride = (layout == 1) ? 0x30 : Off("kBoneStep");
    const uintptr_t posOff = (layout == 1) ? 0x0C : 0;
    const int scanCount = ImMin((int)Off("kBoneCount"), 200);
    auto bonePos = [&](uintptr_t arr, int idx, float* p) -> bool
    {
        const uintptr_t addr = arr + stride * (uintptr_t)idx + posOff;
        if (!g_mem.IsValidRange(addr, sizeof(float) * 3))
            return false;
        return g_mem.ReadRaw(addr, p, sizeof(float) * 3);
    };

    // 1) 找骨骼数组：在场景节点里宽范围扫描（0x40..0x400，每 4 字节），
    //    每个候选既当作"指针"也当作"内联数组起点"试，能在里面找到"头"骨骼的才算数
    uintptr_t boneArr = 0;
    int usedOffset = 0;
    int headIdx = -1;
    float headDz = 0.0f;
    uintptr_t usedStride = stride;
    for (uintptr_t off = 0x40; off <= 0x400 && boneArr == 0; off += 4)
    {
        const uintptr_t asPointer = g_mem.Read<uintptr_t>(scene + off);
        const uintptr_t candidates[2] = { asPointer, scene + off };
        for (int c = 0; c < 2 && boneArr == 0; ++c)
        {
            const uintptr_t arr = candidates[c];
            if (arr == 0 || !g_mem.IsValidRange(arr, stride * 8u))
                continue;
            int bestIdx = -1;
            float bestDz = -1.0f;
            for (int i = 0; i < scanCount; ++i)
            {
                float p[3] = { 0.0f, 0.0f, 0.0f };
                if (!bonePos(arr, i, p))
                    break;
                const float dz = p[2] - origin[2];
                const float dh = sqrtf((p[0] - origin[0]) * (p[0] - origin[0]) + (p[1] - origin[1]) * (p[1] - origin[1]));
                if (dh < 20.0f && dz > 45.0f && dz < 95.0f && dz > bestDz)
                {
                    bestDz = dz;
                    bestIdx = i;
                }
            }
            if (bestIdx >= 0)
            {
                boneArr = arr;
                usedOffset = (int)off;
                usedStride = stride;
                headIdx = bestIdx;
                headDz = bestDz;
            }
        }
    }
    if (boneArr == 0)
    {
        out = "在场景节点 +0x40..0x400 都没找到「头部骨骼」特征（dz 45~95、水平 <20）：试改 kBoneStep / kBoneLayout，或确认 kBoneIdxHead。";
        return done(-1);
    }

    // 2) 认关节：直接调用运行时用的同一个分类器（骨架几何 + 左右对称拟合），
    //    不再用"按身高比例硬凑"的旧办法——旧办法会把小腿骨当成骨盆。
    std::vector<BoneSample> samples;
    samples.reserve((size_t)scanCount);
    for (int i = 0; i < scanCount; ++i)
    {
        float p[3] = { 0.0f, 0.0f, 0.0f };
        if (!bonePos(boneArr, i, p))
            break;
        BoneSample s;
        s.index = i;
        s.pos = BonePoint{ p[0], p[1], p[2] };
        samples.push_back(s);
    }
    float eyeAngles[3] = { 0.0f, 0.0f, 0.0f };
    g_mem.ReadRaw(sample + Off("m_angEyeAngles"), eyeAngles, sizeof(eyeAngles));
    int joint[Bone_Count] = { 0 };
    BoneClassifyReport rep;
    const BonePoint sampleOrigin{ origin[0], origin[1], origin[2] };
    const bool classified = ClassifyBones(samples.data(), (int)samples.size(), sampleOrigin,
                                          isfinite(eyeAngles[1]) ? eyeAngles[1] : 0.0f, joint, &rep);
    const int headPick = classified ? joint[Bone_Head] : headIdx;
    const int neckPick = classified ? joint[Bone_Neck] : -1;
    const int chestPick = classified ? joint[Bone_Chest] : -1;
    const int pelvisPick = classified ? joint[Bone_Pelvis] : -1;
    const int footL = classified ? joint[Bone_FootL] : -1;
    const int footR = classified ? joint[Bone_FootR] : -1;
    const int shoulderL = classified ? joint[Bone_ShoulderL] : -1;
    const int shoulderR = classified ? joint[Bone_ShoulderR] : -1;
    const int handL = classified ? joint[Bone_HandL] : -1;
    const int handR = classified ? joint[Bone_HandR] : -1;

    char line[256];
    struct Write { const char* name; int value; };
    Write writes[7];
    int writeCount = 0;
    // 代码里算的是 scene + m_modelState + boneOffset，这里把绝对偏移换算成相对值
    const uintptr_t modelState = Off("m_modelState");
    if (usedOffset >= (int)modelState)
    {
        writes[writeCount++] = { "m_modelState_boneOffset", (int)((uintptr_t)usedOffset - modelState) };
    }
    else
    {
        OffsetSlot* ms = FindOffsetSlot("m_modelState");
        if (ms) { ms->value = (uintptr_t)usedOffset; ms->filled = true; }
        writes[writeCount++] = { "m_modelState_boneOffset", 0 };
    }
    writes[writeCount++] = { "kBoneIdxHead", headPick };
    if (neckPick >= 0) writes[writeCount++] = { "kBoneIdxNeck", neckPick };
    if (chestPick >= 0) writes[writeCount++] = { "kBoneIdxChest", chestPick };
    if (pelvisPick >= 0) writes[writeCount++] = { "kBoneIdxPelvis", pelvisPick };
    if (footL >= 0) writes[writeCount++] = { "kBoneIdxLeftFoot", footL };
    if (footR >= 0) writes[writeCount++] = { "kBoneIdxRightFoot", footR };
    for (int i = 0; i < writeCount; ++i)
    {
        OffsetSlot* slot = FindOffsetSlot(writes[i].name);
        if (slot != nullptr)
        {
            slot->value = (uintptr_t)writes[i].value;
            slot->filled = true;
        }
    }
    OffsetsSave();

    snprintf(line, sizeof(line), "骨骼自动标定完成（样本实体 hp=%d team=%d）：\n",
             g_mem.Read<int32_t>(sample + Off("m_iHealth")), (int)g_mem.Read<uint8_t>(sample + Off("m_iTeamNum")));
    out += line;
    snprintf(line, sizeof(line), "  骨骼数组 = 场景节点 + 0x%X（已换算成 m_modelState=0x%llX + boneOffset=0x%llX），步长 0x%llX\n",
             usedOffset, (unsigned long long)Off("m_modelState"),
             (unsigned long long)Off("m_modelState_boneOffset"), (unsigned long long)usedStride);
    out += line;
    snprintf(line, sizeof(line), "  头=%d(dz=%.1f)  颈=%d  胸=%d  骨盆=%d  肩=%d/%d  手=%d/%d  脚=%d/%d\n",
             headPick, (double)headDz, neckPick, chestPick, pelvisPick,
             shoulderR, shoulderL, handR, handL, footR, footL);
    out += line;
    if (classified)
    {
        snprintf(line, sizeof(line), "  分类器：骨头 %d 根，左右轴 %.1f°，对称面误差 %.2f，头高 %.1f\n",
                 rep.usableBones, (double)rep.axisDeg, (double)rep.axisCost, (double)rep.headDz);
        out += line;
        out += "  已写回 offsets.ini，可直接看 ESP 骨架是否正确。";
    }
    else
    {
        snprintf(line, sizeof(line), "  分类器失败（%s）：只写回了头/骨骼数组偏移。\n", rep.reason);
        out += line;
        out += "  骨头仍能画出来，但四肢可能不对——把上面的原因发出来即可定位。";
    }
    FileLog("[bones] calibrated head=%d neck=%d chest=%d pelvis=%d feet=%d/%d classified=%d boneOffset=0x%X",
            headPick, neckPick, chestPick, pelvisPick, footR, footL, classified ? 1 : 0, usedOffset);
    return done(1);
}

// 校验 + 把关键行写进启动日志：注入版一进游戏就能从日志看到结论
int GameVerifyAndLog()
{
    std::string report;
    const int bad = GameVerifyOffsets(&report);
    FileLog("[verify] bad=%d readErrors=%llu", bad, g_mem.FailedReads());
    size_t start = 0;
    int lines = 0;
    while (start < report.size() && lines < 40)
    {
        const size_t nl = report.find('\n', start);
        const std::string ln = report.substr(start, (nl == std::string::npos ? report.size() : nl) - start);
        if (!ln.empty())
        {
            FileLog("[verify] %s", ln.c_str());
            ++lines;
        }
        if (nl == std::string::npos)
            break;
        start = nl + 1;
    }
    return bad;
}

int GameVerifyOffsets(std::string* report)
{
    EnsureSlotState();
    g_slotState.assign((size_t)OffsetSlotCount(), 0);
    g_mem.ResetFailedReads();
    std::string out;
    int bad = 0;
    auto fail = [&](const char* name, const std::string& why) { MarkSlot(name, 2, &out, why); ++bad; };
    auto pass = [&](const char* name, const std::string& what) { MarkSlot(name, 1, &out, what); };
    auto miss = [&](const char* name) { MarkSlot(name, 3, &out, "未填写"); ++bad; };

    if (!g_mem.Attached())
    {
        out = "未连接目标进程，先 Attach 再校验。\n";
        if (report) *report = out;
        return -1;
    }
    const uintptr_t base = g_status.moduleBase;
    if (base == 0)
    {
        out = "模块基址为 0：确认 Module 名字（例如 client.dll）。\n";
        if (report) *report = out;
        return -1;
    }
    for (int i = 0; i < OffsetSlotCount(); ++i)
    {
        OffsetSlot& s = OffsetSlotAt(i);
        if (s.required && !s.filled)
            miss(s.name);
    }

    // ---- 视矩阵 ----
    {
        float m[16] = { 0 };
        bool any = false;
        if (g_mem.ReadArray(base + Off("dwViewMatrix"), m, 16))
        {
            for (int i = 0; i < 16; ++i)
                any = any || (m[i] != 0.0f);
            const float rowMag = sqrtf(m[12] * m[12] + m[13] * m[13] + m[14] * m[14]);
            char buf[160];
            snprintf(buf, sizeof(buf), "row3=(%.2f,%.2f,%.2f) 模长 %.2f", (double)m[12], (double)m[13], (double)m[14], (double)rowMag);
            if (!any)
                fail("dwViewMatrix", "全 0，偏移不对或不在主模块里");
            else if (rowMag < 0.05f || rowMag > 20.0f)
                fail("dwViewMatrix", std::string(buf) + "（第 4 行应是朝向向量，模长≈1）");
            else
                pass("dwViewMatrix", buf);
        }
        else
            fail("dwViewMatrix", "读取失败");
    }

    // ---- 实体列表 ----
    int nonNull = 0, plausible = 0;
    int playerPawns = 0;
    uintptr_t firstPawn = 0;
    uintptr_t sample = 0;
    const int maxPlayers = EntityIndexLimit(g_mem, base);
    for (int i = 1; i < maxPlayers; ++i)
    {
        const uintptr_t ent = EntityFromIndex(g_mem, base, i);
        if (ent == 0)
            continue;
        ++nonNull;
        uintptr_t ctrl = 0;
        if (LooksLikePlayerPawn(g_mem, base, ent, &ctrl))
        {
            ++playerPawns;
            if (firstPawn == 0)
                firstPawn = ent;
        }
        const int hp = g_mem.Read<int32_t>(ent + Off("m_iHealth"));
        const int team = (int)g_mem.Read<uint8_t>(ent + Off("m_iTeamNum"));
        const int life = (int)g_mem.Read<uint8_t>(ent + Off("m_lifeState"));
        const int dormant = (int)g_mem.Read<uint8_t>(ent + Off("m_bDormant"));
        if (hp >= 0 && hp <= 100 && (team == 2 || team == 3) && life <= 2 && dormant <= 1)
        {
            ++plausible;
        }
        if (sample == 0)
            sample = ent;   // 只要有非空实体就拿来逐项体检，便于定位到底哪一项错
    }
    {
        char buf[200];
        snprintf(buf, sizeof(buf), "扫描 %d 个索引：非空 %d，字段合理 %d，玩家 Pawn %d（按 m_hController 判定）",
                 maxPlayers, nonNull, plausible, playerPawns);
        if (nonNull == 0)
            fail("dwEntityList", std::string(buf) + " → 检查 dwEntityList / kEntityScheme / kEntityStride");
        else if (plausible == 0)
            fail("dwEntityList", std::string(buf) + " → 列表能走通但字段不对，先修 m_iHealth / m_iTeamNum");
        else
            pass("dwEntityList", buf);
    }
    // 优先用"确认是玩家 Pawn"的实体做样本，否则退回任意可读实体
    if (firstPawn != 0)
        sample = firstPawn;
    // 关键诊断：在实体列表里定位"本地 pawn / 本地 controller"所在的索引
    {
        const uintptr_t localPawn = g_mem.Read<uintptr_t>(base + Off("dwLocalPlayerPawn"));
        const uintptr_t localCtrl = g_mem.Read<uintptr_t>(base + Off("dwLocalPlayerController"));
        int pawnIdx = -1;
        int ctrlIdx = -1;
        for (int i = 0; i < maxPlayers; ++i)
        {
            const uintptr_t e = EntityFromIndex(g_mem, base, i);
            if (e == 0)
                continue;
            if (localPawn != 0 && e == localPawn)
                pawnIdx = i;
            if (localCtrl != 0 && e == localCtrl)
                ctrlIdx = i;
        }
        char b[220];
        snprintf(b, sizeof(b), "本地 pawn 索引=%d，controller 索引=%d（列表里能对上号才说明索引方案正确）", pawnIdx, ctrlIdx);
        if (pawnIdx < 0)
        {
            // 扩大范围再找一次，并列出前若干个非空实体的字段，看清列表里到底有什么
            int wideIdx = -1;
            int dumped = 0;
            for (int i = 0; i < 4096; ++i)
            {
                const uintptr_t e = EntityFromIndex(g_mem, base, i);
                if (e == 0)
                    continue;
                if (localPawn != 0 && e == localPawn)
                {
                    wideIdx = i;
                    break;
                }
                if (dumped < 12)
                {
                    GameLogEntityFields(e, i);
                    ++dumped;
                }
            }
            char b2[220];
            snprintf(b2, sizeof(b2), "扩到 0..4096 再找：本地 pawn 索引=%d；上面 [probe] 是列表里前 12 个非空实体", wideIdx);
            FileLog("[elist] %s", b2);
        }
        if (pawnIdx < 0)
            fail("dwEntityList", std::string(b) + " → 索引方案还不对（步长/块偏移/条目类型）");
        else
        {
            pass("dwEntityList", b);
            GameLogEntityFields(localPawn, pawnIdx);
        }
    }
    if (sample == 0)
    {
        if (report) *report = out + "没有可用样本实体，先修实体列表相关项。\n";
        return bad;
    }

    // ---- 本地玩家字段（schema 对不对，看这里最准） ----
    {
        const uintptr_t pawn = g_mem.Read<uintptr_t>(base + Off("dwLocalPlayerPawn"));
        if (pawn != 0 && g_mem.IsValidRange(pawn, 0x40))
        {
            const int hp = g_mem.Read<int32_t>(pawn + Off("m_iHealth"));
            const int team = (int)g_mem.Read<uint8_t>(pawn + Off("m_iTeamNum"));
            const uintptr_t scene = g_mem.Read<uintptr_t>(pawn + Off("m_pGameSceneNode"));
            float origin[3] = { 0.0f, 0.0f, 0.0f };
            if (scene != 0 && g_mem.IsValidRange(scene, 0x120))
                g_mem.ReadRaw(scene + Off("m_vecAbsOrigin"), origin, sizeof(origin));
            char b[240];
            snprintf(b, sizeof(b), "本地 pawn：hp=%d team=%d origin=(%.1f, %.1f, %.1f) 场景节点=%s",
                     hp, team, (double)origin[0], (double)origin[1], (double)origin[2],
                     (scene != 0 && g_mem.IsValidRange(scene, 0x120)) ? "有效" : "无效");
            const bool okHp = (hp >= 0 && hp <= 100);
            const bool okTeam = (team == 2 || team == 3);
            const bool okOrigin = isfinite(origin[0]) && fabsf(origin[0]) < 20000.0f &&
                                  isfinite(origin[1]) && fabsf(origin[1]) < 20000.0f &&
                                  isfinite(origin[2]) && fabsf(origin[2]) < 4000.0f;
            if (okHp && okTeam && okOrigin)
                pass("dwLocalPlayerPawn", std::string(b) + " → schema 字段全部合理");
            else
                fail("dwLocalPlayerPawn", std::string(b) + " → schema 字段不合理（检查 m_iHealth/m_iTeamNum/m_vecAbsOrigin 是否与当前版本匹配）");
        }
        else
        {
            pass("dwLocalPlayerPawn", "此刻没有本地 pawn（主菜单/未进入地图属于正常）");
        }
    }

    // ---- 单实体字段 ----
    {
        const int hp = g_mem.Read<int32_t>(sample + Off("m_iHealth"));
        const int team = (int)g_mem.Read<uint8_t>(sample + Off("m_iTeamNum"));
        const int life = (int)g_mem.Read<uint8_t>(sample + Off("m_lifeState"));
        const int dormant = (int)g_mem.Read<uint8_t>(sample + Off("m_bDormant"));
        char buf[128];
        snprintf(buf, sizeof(buf), "样本实体读到 hp=%d team=%d lifeState=%d dormant=%d", hp, team, life, dormant);
        if (hp < 0 || hp > 100)
            fail("m_iHealth", std::string(buf));
        else
            pass("m_iHealth", buf);
        if (team != 2 && team != 3)
            fail("m_iTeamNum", "样本实体队伍值不是 2/3");
        else
            pass("m_iTeamNum", "2/3 正常");
        if (life > 2)
            fail("m_lifeState", "值超过 2，可能错位");
        else
            pass("m_lifeState", "正常");
        if (dormant > 1)
            fail("m_bDormant", "不是 0/1，可能错位或用了 bool 宽度不同的字段");
        else
            pass("m_bDormant", dormant == 1 ? "该实体休眠（正常）" : "0");
        // 休眠来源自检：两边都读一遍，告诉用户该用哪边
        {
            const uintptr_t sc = g_mem.Read<uintptr_t>(sample + Off("m_pGameSceneNode"));
            if (sc != 0)
            {
                const int fromScene = (int)g_mem.Read<uint8_t>(sc + Off("m_bDormant"));
                const int fromEnt = (int)g_mem.Read<uint8_t>(sample + Off("m_bDormant"));
                const bool sceneOk = (fromScene == 0 || fromScene == 1);
                const bool entOk = (fromEnt == 0 || fromEnt == 1);
                OffsetSlot* slot = FindOffsetSlot("kDormantFromSceneNode");
                char b2[200];
                snprintf(b2, sizeof(b2), "场景节点读到 %d，实体读到 %d", fromScene, fromEnt);
                if (sceneOk && !entOk)
                {
                    if (slot) { slot->value = 1; slot->filled = true; }
                    pass("kDormantFromSceneNode", std::string(b2) + " → 已设为 1（场景节点）");
                }
                else if (entOk && !sceneOk)
                {
                    if (slot) { slot->value = 0; slot->filled = true; }
                    pass("kDormantFromSceneNode", std::string(b2) + " → 已设为 0（实体）");
                }
                else if (sceneOk && entOk)
                    pass("kDormantFromSceneNode", std::string(b2) + " → 两边都合理");
                else
                    fail("kDormantFromSceneNode", std::string(b2) + " → 两边都不合理，m_bDormant 偏移可能错");
            }
        }
    }
    // ---- 坐标 / 场景节点 ----
    {
        // 骨骼检查优先用本地 pawn 当样本（与标定保持一致，避免两边互相打脸）
        {
            const uintptr_t localPawnForBones = g_mem.Read<uintptr_t>(base + Off("dwLocalPlayerPawn"));
            if (localPawnForBones != 0 && g_mem.IsValidRange(localPawnForBones, 0x40))
            {
                const uintptr_t localScene = g_mem.Read<uintptr_t>(localPawnForBones + Off("m_pGameSceneNode"));
                if (localScene != 0 && g_mem.IsValidRange(localScene, 0x120))
                {
                    float lo[3] = { 0.0f, 0.0f, 0.0f };
                    g_mem.ReadRaw(localScene + Off("m_vecAbsOrigin"), lo, sizeof(lo));
                    if (isfinite(lo[0]) && isfinite(lo[1]) && isfinite(lo[2]) &&
                        fabsf(lo[0]) < 20000.0f && fabsf(lo[1]) < 20000.0f && fabsf(lo[2]) < 4000.0f)
                        sample = localPawnForBones;
                }
            }
        }
        uintptr_t scene = g_mem.Read<uintptr_t>(sample + Off("m_pGameSceneNode"));
        const bool useScene = (Off("kOriginFromSceneNode") != 0);
        float origin[3] = { 0.0f, 0.0f, 0.0f };
        const uintptr_t addr = (useScene && scene != 0) ? scene + Off("m_vecAbsOrigin") : sample + Off("m_vecAbsOrigin");
        g_mem.ReadRaw(addr, origin, sizeof(origin));
        char buf[200];
        const bool finite = isfinite(origin[0]) && isfinite(origin[1]) && isfinite(origin[2]);
        const bool inRange = fabsf(origin[0]) < 20000.0f && fabsf(origin[1]) < 20000.0f && fabsf(origin[2]) < 4000.0f;
        snprintf(buf, sizeof(buf), "origin=(%.1f, %.1f, %.1f)  取自 %s",
                 (double)origin[0], (double)origin[1], (double)origin[2], useScene && scene ? "场景节点" : "实体本体");
        if (!finite || !inRange)
            fail("m_vecAbsOrigin", std::string(buf) + " → 坐标不合理，试 kOriginFromSceneNode 0/1 互换");
        else
            pass("m_vecAbsOrigin", buf);
        if (useScene)
        {
            if (scene == 0 || !g_mem.IsValid(scene))
                fail("m_pGameSceneNode", "指针无效（CS:GO 请把 kOriginFromSceneNode 设为 0）");
            else
                pass("m_pGameSceneNode", "指针有效");
        }
        // 骨骼
        const int layout = (int)Off("kBoneLayout");
        uintptr_t boneArr = 0;
        if (layout == 1)
            boneArr = g_mem.Read<uintptr_t>(sample + Off("m_dwBoneMatrix"));
        else if (scene != 0)
            boneArr = g_mem.Read<uintptr_t>(scene + Off("m_modelState") + Off("m_modelState_boneOffset"));
        const int boneCount = (int)Off("kBoneCount");
        const uintptr_t boneStride = (layout == 1) ? 0x30 : Off("kBoneStep");
        const uintptr_t bonePosOff = (layout == 1) ? 0x0C : 0;
        if (boneArr != 0 && g_mem.IsValid(boneArr))
        {
            const int headIdx = (int)Off("kBoneIdxHead");
            const uintptr_t stride = boneStride;
            const uintptr_t posoff = bonePosOff;
            float head[3] = { 0 };
            g_mem.ReadRaw(boneArr + stride * (uintptr_t)headIdx + posoff, head, sizeof(head));
            const float dz = head[2] - origin[2];
            const float dh = sqrtf((head[0] - origin[0]) * (head[0] - origin[0]) +
                                   (head[1] - origin[1]) * (head[1] - origin[1]));
            char buf2[220];
            snprintf(buf2, sizeof(buf2), "head bone=(%.1f, %.1f, %.1f)  相对脚底 dz=%.1f 平面上偏差 %.1f",
                     (double)head[0], (double)head[1], (double)head[2], (double)dz, (double)dh);
            const bool headSane = isfinite(head[0]) && isfinite(head[1]) && isfinite(head[2]) &&
                                  isfinite(dz) && isfinite(dh);
            if (!headSane || dz < 20.0f || dz > 120.0f || dh > 60.0f)
                fail("kBoneIdxHead", std::string(buf2) + " → 骨骼索引/步长/布局需要调");
            else
                pass("kBoneIdxHead", buf2);
            // 脚骨：CS2/CS:GO 索引差别大，这里单独体检
            for (int f = 0; f < 2; ++f)
            {
                const char* slotName = (f == 0) ? "kBoneIdxLeftFoot" : "kBoneIdxRightFoot";
                const int idx = (int)Off(slotName);
                float fp[3] = { 0.0f, 0.0f, 0.0f };
                if (idx < 0 || idx >= boneCount ||
                    !g_mem.ReadRaw(boneArr + stride * (uintptr_t)idx + posoff, fp, sizeof(fp)))
                {
                    fail(slotName, "读不到该骨骼");
                    continue;
                }
            const float dzf = fp[2] - origin[2];
            char bf[180];
            snprintf(bf, sizeof(bf), "脚骨=(%.1f, %.1f, %.1f)  相对脚底 dz=%.1f", (double)fp[0], (double)fp[1], (double)fp[2], (double)dzf);
                const bool sane = isfinite(fp[0]) && isfinite(fp[1]) && isfinite(fp[2]) && fabsf(dzf) <= 20.0f;
                if (!sane)
                    fail(slotName, std::string(bf) + " → 索引可能不对（脚骨应该几乎贴着脚底）");
                else
                    pass(slotName, bf);
            }
        }
        else
        {
            fail(layout == 1 ? "m_dwBoneMatrix" : "m_modelState", "骨骼数组指针无效");
        }
    }
    // ---- 名字 / 武器 / 状态位 ----
    {
        // 名字：CS2 在控制器上，试两个来源并报告哪个可用
        const std::string fromEnt = g_mem.ReadString(sample + Off("m_iszPlayerName"), 32);
        const uint32_t ctrlHandle = (Off("m_hController") != 0) ? g_mem.Read<uint32_t>(sample + Off("m_hController")) : 0;
        const uintptr_t controller = (ctrlHandle != 0) ? EntityFromIndex(g_mem, base, (int)(ctrlHandle & 0x7FFF)) : 0;
        const std::string fromCtrl = (controller != 0) ? g_mem.ReadString(controller + Off("m_iszPlayerName"), 32) : std::string();
            auto printable = [](const std::string& s)
        {
            if (s.empty())
                return false;
            for (char c : s)
            {
                // 允许中文等多字节名字，只拦控制字符
                const unsigned char u = (unsigned char)c;
                if (u < 32 || u == 127)
                    return false;
            }
            return true;
        };
        const bool entOk = printable(fromEnt);
        const bool ctrlOk = printable(fromCtrl);
        if (ctrlOk)
            pass("m_iszPlayerName", std::string("控制器读到 \"") + fromCtrl + "\"（CS2 走这条）");
        else if (entOk)
            pass("m_iszPlayerName", std::string("实体读到 \"") + fromEnt + "\"（CS:GO 走这条）");
        else
        {
            char b[200];
            snprintf(b, sizeof(b), "实体读 \"%s\"，控制器读 \"%s\"", fromEnt.c_str(), fromCtrl.c_str());
            fail("m_iszPlayerName", std::string(b) + " → 都不是可打印字符串，检查偏移或 m_hController");
        }
        if (Off("m_hController") != 0)
        {
            // 用"本地 pawn"做锚点验证，比用随便挑的样本实体可靠
            const uintptr_t localPawn = g_mem.Read<uintptr_t>(base + Off("dwLocalPlayerPawn"));
            const uintptr_t localCtrl = g_mem.Read<uintptr_t>(base + Off("dwLocalPlayerController"));
            if (localPawn != 0 && localCtrl != 0 && g_mem.IsValidRange(localPawn, 0x40))
            {
                const uint32_t h = g_mem.Read<uint32_t>(localPawn + Off("m_hController"));
                const bool linked = (EntityFromIndex(g_mem, base, (int)(h & 0x7FFF)) == localCtrl);
                char b3[200];
                snprintf(b3, sizeof(b3), "本地 pawn 句柄=0x%X → %s", h, linked ? "解析到控制器 ✓" : "解析失败");
                if (linked)
                    pass("m_hController", b3);
                else
                    fail("m_hController", std::string(b3) + " → 句柄偏移或实体列表参数不对（可点「实体列表标定」+ 句柄标定）");
            }
            else if (controller == 0)
                fail("m_hController", "样本实体 → controller 解析失败，且本地 pawn 不可用");
            else
                pass("m_hController", "样本实体解析到控制器实体");
        }
        const uintptr_t ws = g_mem.Read<uintptr_t>(sample + Off("m_pWeaponServices"));
        if (ws != 0 && g_mem.IsValid(ws))
        {
            const uint32_t handle = g_mem.Read<uint32_t>(ws + Off("m_hActiveWeapon"));
            const uintptr_t weapon = EntityFromIndex(g_mem, base, (int)(handle & 0x7FFF));
            if (weapon == 0)
                fail("m_hActiveWeapon", "句柄解析不到武器实体（先确认实体列表是对的）");
            else
            {
                pass("m_hActiveWeapon", "解析到武器实体");
                // 武器 ID 链：中间层偏移可能随版本变，这里直接试几个候选并写回最优值
                const uintptr_t am = Off("m_AttributeManager");
                OffsetSlot* amSlot = FindOffsetSlot("m_AttributeManager");
                if (amSlot == nullptr || !amSlot->filled)
                {
                    pass("m_AttributeManager", "未填写，跳过武器 ID 链检查（只影响武器名显示）");
                }
                else
                {
                const int candidates[5] = { 0x40, 0x48, 0x50, 0x58, 0x60 };
                int bestItem = -1;
                int bestIndex = 0;
                for (int c = 0; c < 5; ++c)
                {
                    const int idx = g_mem.Read<int32_t>(weapon + am + candidates[c] + Off("m_iItemDefinitionIndex"));
                    if (idx > 0 && idx <= 70)
                    {
                        bestItem = candidates[c];
                        bestIndex = idx;
                        break;
                    }
                }
                OffsetSlot* itemSlot = FindOffsetSlot("m_AttributeManager_Item");
                char b[220];
                if (bestItem > 0)
                {
                    if (itemSlot) { itemSlot->value = (uintptr_t)bestItem; itemSlot->filled = true; }
                    snprintf(b, sizeof(b), "武器 ID=%d（defIndex 链中间层 0x%X 可用）", bestIndex, bestItem);
                    pass("m_AttributeManager_Item", b);
                }
                else
                {
                    const int idx = g_mem.Read<int32_t>(weapon + Off("m_AttributeManager") + Off("m_AttributeManager_Item") + Off("m_iItemDefinitionIndex"));
                    snprintf(b, sizeof(b), "当前中间层 0x%llX 读出 %d，0x40/0x48/0x50/0x58/0x60 都没有合理值",
                             (unsigned long long)Off("m_AttributeManager_Item"), idx);
                    fail("m_AttributeManager_Item", std::string(b) + " → 检查 m_AttributeManager 与 m_iItemDefinitionIndex");
                }
                }
                OffsetSlot* clipSlot = FindOffsetSlot("m_iClip1");
                if (clipSlot != nullptr && clipSlot->filled)
                {
                    const int clip = g_mem.Read<int32_t>(weapon + Off("m_iClip1"));
                    char bc[120];
                    snprintf(bc, sizeof(bc), "弹匣 %d 发", clip);
                    if (clip < 0 || clip > 200)
                        fail("m_iClip1", std::string(bc) + "（不合理，可能偏移不对）");
                    else
                        pass("m_iClip1", bc);
                }
            }
        }
        else
            fail("m_pWeaponServices", "指针无效");
        // 可见性链
        {
            const int direct = (int)g_mem.Read<uint8_t>(sample + Off("m_bSpotted"));
            int nested = -1;
            if (Off("m_entitySpottedState") != 0)
                nested = (int)g_mem.Read<uint8_t>(sample + Off("m_entitySpottedState") + Off("m_bSpotted"));
            char b[200];
            snprintf(b, sizeof(b), "直接读 %d，EntitySpottedState 链读 %d", direct, nested);
            if ((direct == 0 || direct == 1) || (nested == 0 || nested == 1))
                pass("m_entitySpottedState", b);
            else
                fail("m_entitySpottedState", std::string(b) + " → 两边都不像 bool，检查这两个偏移");
        }
    }
    // ---- 本地玩家 ----
    {
        const uintptr_t pawn = g_mem.Read<uintptr_t>(base + Off("dwLocalPlayerPawn"));
        const uintptr_t ctrl = g_mem.Read<uintptr_t>(base + Off("dwLocalPlayerController"));
        char buf[200];
        snprintf(buf, sizeof(buf), "pawn=0x%llX controller=0x%llX", (unsigned long long)pawn, (unsigned long long)ctrl);
        if (pawn == 0 || !g_mem.IsValid(pawn))
            fail("dwLocalPlayerPawn", std::string(buf) + " → 指针无效（要在游戏内进入地图后校验）");
        else
            pass("dwLocalPlayerPawn", buf);
        if (ctrl == 0 || !g_mem.IsValid(ctrl))
            fail("dwLocalPlayerController", "指针无效");
        else
            pass("dwLocalPlayerController", "指针有效");
    }

    EnsureSlotState();
    char head[160];
    int checked = 0, okCount = 0;
    for (int v : g_slotState)
    {
        if (v == 1) { ++checked; ++okCount; }
        else if (v == 2 || v == 3) ++checked;
    }
    snprintf(head, sizeof(head), "偏移校验：通过 %d / 检查 %d 项，异常 %d 项\n\n", okCount, checked, bad);
    out = std::string(head) + out;
    {
        char rl[160];
        snprintf(rl, sizeof(rl), "\n读取失败次数：%llu（内部模式=指针链撞到不可读内存，说明某个偏移不对）\n",
                 g_mem.FailedReads());
        out += rl;
    }
    if (report) *report = out;
    FileLog("[verify] ok=%d checked=%d bad=%d", okCount, checked, bad);
    return bad;
}

bool GameAttachOnStartup(const Settings& s)
{
    if (!s.menu.autoAttach)
        return false;
    if (!Memory::ProcessExists(s.menu.targetProcess))
    {
        FileLog("[game] auto attach: process %s not running", s.menu.targetProcess);
        return false;
    }
    return GameAttach(s.menu.targetProcess, s.menu.targetModule);
}
}







