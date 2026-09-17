#include "features/offsets.h"
#include "core/memory.h"
#include "core/sigscan.h"
#include "core/paths.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <algorithm>

namespace nl {

namespace {

std::vector<OffsetSlot>& Table()
{
    static std::vector<OffsetSlot> table = []()
    {
        std::vector<OffsetSlot> t;
        auto add = [&](const char* name, const char* group, const char* desc, SlotKind kind,
                       bool required, const char* sig = nullptr, uintptr_t def = 0, int sigDisp = 0)
        {
            OffsetSlot s;
            s.name = name;
            s.group = group;
            s.description = desc;
            s.kind = kind;
            s.required = required;
            s.signature = sig;
            s.defaultValue = def;
            s.sigDisp = sigDisp;
            s.value = def;
            s.filled = (kind == SlotKind::Constant && def != 0);
            t.push_back(s);
        };
        // ---------------- 模块偏移 ----------------
        add("dwEntityList", "client.dll", "实体列表根指针：实体系统/块表起点", SlotKind::ModuleOffset, true);
        add("dwLocalPlayerController", "client.dll", "本地玩家控制器指针（拿名字、队伍、金钱）", SlotKind::ModuleOffset, true);
        add("dwLocalPlayerPawn", "client.dll", "本地玩家 Pawn 指针（拿血量、坐标、武器）", SlotKind::ModuleOffset, true);
        add("dwViewMatrix", "client.dll", "世界->屏幕矩阵（16 个 float，行主序）", SlotKind::ModuleOffset, true);
        add("dwViewAngles", "client.dll", "当前视角角度（写入即为改视角）", SlotKind::ModuleOffset, false);
        add("dwGlobalVars", "client.dll", "GlobalVars：帧时间、tick、地图名等", SlotKind::ModuleOffset, false);
        add("dwGameEntitySystem", "client.dll", "实体系统对象（用于块表遍历）", SlotKind::ModuleOffset, false);
        add("dwGameEntitySystem_highestEntityIndex", "client.dll", "实体系统中最高实体索引", SlotKind::ModuleOffset, false);
        add("dwSensitivity", "client.dll", "sensitivity 那个 convar 对象的指针（convar 扫描的锚点）", SlotKind::ModuleOffset, false, nullptr, 0x23C9F18);

        add("dwCSGOInput", "client.dll", "输入对象（用于自动化输入/瞄准）", SlotKind::ModuleOffset, false);
        add("dwPlantedC4", "client.dll", "已安放 C4 指针（炸弹计时绘制）", SlotKind::ModuleOffset, false);
        add("dwNetworkGameClient", "client.dll", "网络客户端（延迟、连接状态）", SlotKind::ModuleOffset, false);
        add("dwPrediction", "client.dll", "Prediction 对象（外挂移动类功能常用）", SlotKind::ModuleOffset, false);
        // ---------------- 实体字段 ----------------
        add("m_iHealth", "schema", "当前血量", SlotKind::NetVar, true);
        add("m_iMaxHealth", "schema", "最大血量", SlotKind::NetVar, false);
        add("m_iTeamNum", "schema", "队伍编号（2=T / 3=CT）", SlotKind::NetVar, true);
        add("m_lifeState", "schema", "生命状态（0=存活）", SlotKind::NetVar, true);
        add("m_vecAbsOrigin", "schema", "世界坐标（部分版本叫 m_vOldOrigin）", SlotKind::NetVar, true);
        add("m_pGameSceneNode", "schema", "场景节点指针（坐标/骨骼挂在这里）", SlotKind::NetVar, true);
        add("m_modelState", "schema", "模型状态（骨骼矩阵基址 = modelState + 0x80）", SlotKind::NetVar, true);
        add("m_vecViewOffset", "schema", "眼睛相对坐标偏移（画骨骼头节点用）", SlotKind::NetVar, false);
        add("m_hPlayerPawn", "schema", "控制器 -> Pawn 句柄", SlotKind::NetVar, true);
        add("m_hController", "schema", "Pawn -> 控制器 句柄（反查名字）", SlotKind::NetVar, false);
        add("m_iszPlayerName", "schema", "玩家名字字符串（旧版 m_szPlayerName）", SlotKind::NetVar, false);
        add("m_bDormant", "schema", "休眠标记：为 1 时不要绘制", SlotKind::NetVar, true);
        add("m_bSpotted", "schema", "被点亮（雷达可见）", SlotKind::NetVar, false);
        add("m_entitySpottedState", "schema", "EntitySpottedState_t 在实体内的偏移（CS2 的 m_bSpotted 藏在里面）", SlotKind::NetVar, false);
        add("m_bSpottedByMask", "schema", "被谁看见的位掩码（可见性判断）", SlotKind::NetVar, false);
        add("m_iShotsFired", "schema", "开火计数（后坐力/瞄准判断）", SlotKind::NetVar, false);
        add("m_aimPunchAngle", "schema", "瞄准冲量（无后坐力需要）", SlotKind::NetVar, false);
        add("m_Glow", "schema", "CGlowProperty 在实体内的偏移（发光描边，schema 实测 0xDE0）", SlotKind::NetVar, false, nullptr, 0xDE0);
        add("m_clrRender", "schema", "渲染颜色（chams 用，schema 实测 0xC98）", SlotKind::NetVar, false, nullptr, 0xC98);
        add("m_bGlowing", "schema", "是否发光（相对 CGlowProperty）", SlotKind::NetVar, false, nullptr, 0x51);
        add("m_glowColorOverride", "schema", "发光颜色覆盖（相对 CGlowProperty，ARGB）", SlotKind::NetVar, false, nullptr, 0x40);
        add("m_pCameraServices", "schema", "相机服务对象指针（FOV 在它里面，schema 实测 0x1240）", SlotKind::NetVar, false, nullptr, 0x1240);        add("m_iFOV", "schema", "当前视野角度（FOV 修改）", SlotKind::NetVar, false);
        add("m_bIsScoped", "schema", "是否开镜", SlotKind::NetVar, false);
        add("m_flFlashMaxAlpha", "schema", "闪光最大白度（去闪）", SlotKind::NetVar, false);
        add("m_ArmorValue", "schema", "护甲值", SlotKind::NetVar, false);
        add("m_bHasHelmet", "schema", "是否有头盔", SlotKind::NetVar, false);
        add("m_pItemServices", "schema", "C_BasePlayerPawn -> CPlayer_ItemServices（读头盔）", SlotKind::NetVar, false);
        add("m_flFlashDuration", "schema", "当前闪光强度（0..1，画 FLASH 标记）", SlotKind::NetVar, false);
        add("m_bInReload", "schema", "武器是否在换弹（画 RELOAD 标记）", SlotKind::NetVar, false);
        add("m_pInGameMoneyServices", "schema", "控制器 -> 金钱服务（读 $）", SlotKind::NetVar, false);
        // C4 / 炸弹计时
        add("m_bBombTicking", "c4", "已安放且正在倒计时", SlotKind::NetVar, false);
        add("m_flC4Blow", "c4", "爆炸时刻（全局时间）", SlotKind::NetVar, false);
        add("m_flTimerLength", "c4", "炸弹总时长", SlotKind::NetVar, false);
        add("m_nBombSite", "c4", "包点编号（0=A / 1=B）", SlotKind::NetVar, false);
        add("m_bBeingDefused", "c4", "是否正在被拆", SlotKind::NetVar, false);
        add("m_flDefuseCountDown", "c4", "拆包完成时刻", SlotKind::NetVar, false);
        add("m_hBombDefuser", "c4", "拆包人句柄", SlotKind::NetVar, false);
        add("m_bIsDefusing", "schema", "是否正在拆包（KIT 标记）", SlotKind::NetVar, false);
        add("m_vecVelocity", "schema", "速度（部分版本叫 m_vecAbsVelocity）", SlotKind::NetVar, false);
        add("m_flSimulationTime", "schema", "实体模拟时间（拿它当服务器当前时间，算炸弹倒计时）", SlotKind::NetVar, false);
        add("m_fFlags", "schema", "状态标志（在地面 FL_ONGROUND 等）", SlotKind::NetVar, false);
        add("m_pWeaponServices", "schema", "武器服务指针", SlotKind::NetVar, false);
        add("m_hActiveWeapon", "schema", "当前武器句柄", SlotKind::NetVar, false);
        add("m_AttributeManager", "schema", "C_EconEntity -> CAttributeManager（武器 ID 链中间层）", SlotKind::NetVar, false);
        add("m_AttributeManager_Item", "schema", "CAttributeManager -> C_EconItemView（CS2 一般 0x50）", SlotKind::Constant, false, nullptr, 0x50);
        add("m_iClip1", "schema", "当前弹匣子弹数", SlotKind::NetVar, false);
        add("m_iItemDefinitionIndex", "schema", "武器 ID（用于显示武器名）", SlotKind::NetVar, false);
        add("m_iAccount", "schema", "金钱", SlotKind::NetVar, false);
        add("m_iPing", "schema", "延迟", SlotKind::NetVar, false);
        add("m_iCompetitiveRanking", "schema", "竞技段位", SlotKind::NetVar, false);
        add("m_angEyeAngles", "schema", "眼睛角度（画朝向/瞄准线用）", SlotKind::NetVar, false);
        add("kOriginFromSceneNode", "schema", "坐标来自场景节点=1 / 来自实体本体=0（CS2 为 1）", SlotKind::Constant, false, nullptr, 1);
        add("kDormantFromSceneNode", "schema", "休眠标记取自场景节点=1 / 实体=0（CS2 的 m_bDormant 在 CGameSceneNode 上）", SlotKind::Constant, false, nullptr, 1);
        add("kEntityScheme", "const", "实体列表方案：0=CS2 块表 / 1=CS:GO 经典数组（list + 0x10*index）", SlotKind::Constant, false, nullptr, 0);
        // ---------------- 骨骼 ----------------
        add("m_pBoneArray", "bone", "骨骼数组指针（旧版直接给指针；CS2 一般用 modelState+0x80）", SlotKind::NetVar, false);
        add("m_dwBoneMatrix", "bone", "CS:GO 骨骼矩阵指针偏移（kEntityScheme=1 时用）", SlotKind::NetVar, false);
        add("kBoneLayout", "bone", "骨骼布局：0=CS2 位置+四元数(0x20) / 1=CS:GO matrix3x4(0x30，平移在 +0x0C)", SlotKind::Constant, false, nullptr, 0);
        add("m_modelState_boneOffset", "bone", "骨骼数组相对 modelState 的偏移（CS2 通常 0x80）", SlotKind::Constant, false, nullptr, 0x80);
        add("kBoneIdxHead", "bone", "头骨骼索引（骨架位置不对就调这里）", SlotKind::Constant, false, nullptr, 6);
        add("kBoneIdxNeck", "bone", "脖子骨骼索引", SlotKind::Constant, false, nullptr, 5);
        add("kBoneIdxChest", "bone", "胸口骨骼索引", SlotKind::Constant, false, nullptr, 4);
        add("kBoneIdxPelvis", "bone", "骨盆骨骼索引", SlotKind::Constant, false, nullptr, 0);
        add("kBoneIdxLeftFoot", "bone", "左脚骨骼索引", SlotKind::Constant, false, nullptr, 23);
        add("kBoneIdxRightFoot", "bone", "右脚骨骼索引", SlotKind::Constant, false, nullptr, 24);
        // ---------------- 结构性常量 ----------------
        add("kMaxPlayers", "const", "最大玩家数", SlotKind::Constant, false, nullptr, 64);
        add("kEntityIndexLimit", "const", "实体索引扫描上限（CS2 玩家索引常在几百，给 4096）", SlotKind::Constant, false, nullptr, 4096);
        add("kBoneCount", "const", "骨骼索引上限（自动标定会扫描这个范围，CS2 一般给 128）", SlotKind::Constant, false, nullptr, 128);
        add("kBoneStep", "const", "每根骨骼的字节数（位置 3 float + 旋转 4 float = 0x20）", SlotKind::Constant, false, nullptr, 0x20);
        add("kCreateMoveIndex", "const", "CCSGOInput 虚表里 CreateMove 的索引（默认 5，调试器侦察确认）", SlotKind::Constant, false, nullptr, 25);
        add("kEntityListChunkSize", "const", "实体列表每块指针个数（CS2 常见 0x8）", SlotKind::Constant, false, nullptr, 0x8);
        add("kEntityStride", "const", "实体列表内条目步长（版本相关，用来暴力遍历时的兜底）", SlotKind::Constant, false, nullptr, 0x78);
        add("kEntityChunkOffset", "const", "实体列表：块指针在列表里的偏移（CS2 一般 0x10）", SlotKind::Constant, false, nullptr, 0x10);
        add("kEntityEntryPointer", "const", "块内条目存的是指针=1 / 实体本体=0", SlotKind::Constant, false, nullptr, 1);
        add("kViewMatrixFloats", "const", "视矩阵 float 数量", SlotKind::Constant, false, nullptr, 16);
        return t;
    }();
    return table;
}

std::string ConfigPath()
{
    // 先找现成的（模块目录 -> %LOCALAPPDATA%\NeverloseUI -> 主 EXE 目录 -> 工作目录），
    // 都没有时给"数据目录"下的路径（手动映射也能落到 %LOCALAPPDATA%\NeverloseUI）
    return ResolveDataFile("offsets.ini");
}

std::string Trim(const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n'))
        ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n'))
        --b;
    return s.substr(a, b - a);
}

bool ParseNumber(const std::string& text, uintptr_t* out)
{
    std::string s = Trim(text);
    if (s.empty())
        return false;
    // 去掉可能的前缀：client.dll+0x1234 / "module" + 0x1234
    const size_t plus = s.find_last_of('+');
    if (plus != std::string::npos && s.find("0x") != std::string::npos && plus < s.find("0x"))
        s = s.substr(plus + 1);
    const size_t lastHex = s.rfind("0x");
    if (lastHex != std::string::npos)
        s = s.substr(lastHex);
    while (!s.empty() && (s.back() == ';' || s.back() == ',' || s.back() == 'h' || s.back() == 'H'))
        s.pop_back();
    if (s.empty())
        return false;
    char* end = nullptr;
    const unsigned long long v = _strtoui64(s.c_str(), &end, 0);
    if (end == s.c_str())
        return false;
    *out = (uintptr_t)v;
    return true;
}
} // namespace

const char* SlotKindName(SlotKind kind)
{
    switch (kind)
    {
    case SlotKind::ModuleOffset: return "模块偏移";
    case SlotKind::NetVar:       return "类字段";
    default:                     return "常量";
    }
}

int OffsetSlotCount() { return (int)Table().size(); }
OffsetSlot& OffsetSlotAt(int index) { return Table()[(size_t)index]; }

OffsetSlot* FindOffsetSlot(const char* name)
{
    if (name == nullptr)
        return nullptr;
    for (auto& s : Table())
    {
        if (_stricmp(s.name, name) == 0)
            return &s;
    }
    return nullptr;
}

void OffsetsReset()
{
    for (auto& s : Table())
    {
        s.value = s.defaultValue;
        s.filled = (s.kind == SlotKind::Constant && s.defaultValue != 0);
    }
}

bool OffsetsLoad()
{
    OffsetsReset();
    FILE* fp = nullptr;
    if (fopen_s(&fp, ConfigPath().c_str(), "rb") != 0 || fp == nullptr)
        return false;
    char line[512];
    int loaded = 0;
    while (fgets(line, sizeof(line), fp))
    {
        const std::string raw = Trim(line);
        if (raw.empty() || raw[0] == '#' || raw[0] == ';' || raw.rfind("//", 0) == 0)
            continue;
        const size_t eq = raw.find('=');
        if (eq == std::string::npos)
            continue;
        OffsetSlot* slot = FindOffsetSlot(Trim(raw.substr(0, eq)).c_str());
        if (slot == nullptr)
            continue;
        uintptr_t value = 0;
        if (!ParseNumber(raw.substr(eq + 1), &value))
            continue;
        slot->value = value;
        slot->filled = true;
        ++loaded;
    }
    fclose(fp);
    return loaded > 0;
}

bool OffsetsSave()
{
    FILE* fp = nullptr;
    if (fopen_s(&fp, ConfigPath().c_str(), "wb") != 0 || fp == nullptr)
        return false;
    fprintf(fp, "# neverlose.ui offsets  (name = value, 0x 前缀十六进制)\n");
    for (const auto& s : Table())
    {
        if (!s.filled)
            continue;
        fprintf(fp, "%s=0x%llX\n", s.name, (unsigned long long)s.value);
    }
    fclose(fp);
    return true;
}

int OffsetsFilledCount()
{
    int n = 0;
    for (const auto& s : Table())
        n += s.filled ? 1 : 0;
    return n;
}

int OffsetsRequiredMissing()
{
    int n = 0;
    for (const auto& s : Table())
        if (s.required && !s.filled)
            ++n;
    return n;
}

std::string OffsetsDumpFilled()
{
    std::string out;
    char buf[256];
    for (const auto& s : Table())
    {
        if (!s.filled)
            continue;
        snprintf(buf, sizeof(buf), "%s = 0x%llX\n", s.name, (unsigned long long)s.value);
        out += buf;
    }
    return out;
}

std::string OffsetsExportTemplate()
{
    std::string out = "# neverlose.ui 偏移模板：把 = 右边补上数值即可（支持 0x 十六进制 / 十进制）\n"
                      "# 也可以直接在菜单 Memory 页把整段粘贴进去，会自动匹配名字\n\n";
    const char* lastGroup = nullptr;
    char buf[512];
    for (const auto& s : Table())
    {
        if (lastGroup == nullptr || strcmp(lastGroup, s.group) != 0)
        {
            lastGroup = s.group;
            snprintf(buf, sizeof(buf), "\n# ---- %s ----\n", s.group);
            out += buf;
        }
        if (s.filled)
            snprintf(buf, sizeof(buf), "# %s%s\n%s=0x%llX\n", s.description,
                     s.required ? "  [必需]" : "", s.name, (unsigned long long)s.value);
        else
            snprintf(buf, sizeof(buf), "# %s%s\n%s=\n", s.description,
                     s.required ? "  [必需]" : "", s.name);
        out += buf;
    }
    return out;
}

int OffsetsApplyPaste(const char* text, std::string* report)
{
    if (text == nullptr)
        return 0;
    std::string src(text);
    int applied = 0;
    int ignored = 0;
    std::string detail;
    size_t pos = 0;
    while (pos <= src.size())
    {
        size_t nl = src.find('\n', pos);
        std::string line = Trim(src.substr(pos, (nl == std::string::npos ? src.size() : nl) - pos));
        pos = (nl == std::string::npos) ? src.size() + 1 : nl + 1;
        if (line.empty())
            continue;
        // 去掉注释
        const size_t hash = line.find('#');
        if (hash != std::string::npos)
            line = Trim(line.substr(0, hash));
        const size_t slashes = line.find("//");
        if (slashes != std::string::npos)
            line = Trim(line.substr(0, slashes));
        if (line.empty())
            continue;
        // 分离名字与数值
        size_t sep = line.find('=');
        if (sep == std::string::npos)
            sep = line.find(':');
        if (sep == std::string::npos)
        {
            // 允许 "name 0x1234" / "name\t0x1234"
            const size_t sp = line.find_first_of(" \t");
            if (sp != std::string::npos && line.find("0x") != std::string::npos)
                sep = sp;
        }
        // 容错：[client.dll + 0x1234] 之类
        std::string name = (sep == std::string::npos) ? std::string() : Trim(line.substr(0, sep));
        std::string valueText = (sep == std::string::npos) ? line : Trim(line.substr(sep + 1));
        if (!name.empty() && (name[0] == '['))
            name = Trim(name.substr(1));
        if (!valueText.empty() && valueText.back() == ']')
            valueText.pop_back();
        uintptr_t value = 0;
        if (name.empty() || !ParseNumber(valueText, &value))
        {
            ++ignored;
            continue;
        }
        OffsetSlot* slot = FindOffsetSlot(name.c_str());
        if (slot == nullptr)
        {
            ++ignored;
            detail += "  未识别的名字: " + name + "\n";
            continue;
        }
        slot->value = value;
        slot->filled = true;
        ++applied;
        char buf[160];
        snprintf(buf, sizeof(buf), "  %s = 0x%llX\n", slot->name, (unsigned long long)value);
        detail += buf;
    }
    if (report != nullptr)
    {
        char buf[128];
        snprintf(buf, sizeof(buf), "解析完成：填入 %d 项，忽略 %d 行\n", applied, ignored);
        *report = buf + detail;
    }
    if (applied > 0)
        OffsetsSave();
    return applied;
}

int OffsetsResolveBySignature(const Memory& mem, uintptr_t moduleBase, size_t moduleSize, std::string* report)
{
    int hits = 0;
    std::string detail;
    for (auto& s : Table())
    {
        if (s.filled || s.signature == nullptr || *s.signature == 0)
            continue;
        const uintptr_t addr = ScanForSignature(mem, moduleBase, moduleSize, s.signature);
        if (addr == 0)
            continue;
        s.value = addr - moduleBase + (uintptr_t)s.sigDisp;
        s.filled = true;
        ++hits;
        char buf[192];
        snprintf(buf, sizeof(buf), "  特征码命中 %s -> +0x%llX\n", s.name, (unsigned long long)s.value);
        detail += buf;
    }
    if (report != nullptr)
    {
        char buf[96];
        snprintf(buf, sizeof(buf), "特征码解析：命中 %d 项\n", hits);
        *report = buf + detail;
    }
    if (hits > 0)
        OffsetsSave();
    return hits;
}
}





