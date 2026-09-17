// -----------------------------------------------------------------------------
// NLTestTarget —— 自检用「假游戏」进程
// 目的：在我们模块内部摆出一份和真实射击游戏同构的数据布局
//      （实体列表块表 / 场景节点 / 骨骼数组 / 视矩阵），
//      用来验证 neverlose.ui 的 偏移 -> 内存 -> 实体 -> 屏幕投影 -> 绘制 全链路。
// 运行后会在同目录写出 offsets_nltest.ini（相对本模块基址的偏移），
// 把它粘贴进菜单 Memory 页即可看到 8 个假玩家的 ESP。
// -----------------------------------------------------------------------------
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string>

namespace {

constexpr int   kMaxPlayers = 8;
constexpr int   kBoneCount = 30;
constexpr float kPi = 3.14159265358979f;

alignas(16) unsigned char g_image[0x100000];

// ---- 镜像内布局 ----
constexpr size_t OFF_MAGIC = 0x0000;            // "NLUI_TEST_IMAGE"
constexpr size_t OFF_SIG = 0x0010;              // "NLUIEL01" 特征码目标
constexpr size_t OFF_ENTITY_LIST = 0x0018;      // uintptr_t -> 块表
constexpr size_t OFF_LOCAL_CONTROLLER = 0x0020; // uintptr_t
constexpr size_t OFF_LOCAL_PAWN = 0x0028;       // uintptr_t
constexpr size_t OFF_VIEW_MATRIX = 0x0100;      // float[16]
constexpr size_t OFF_CONTROLLER = 0x0200;       // 控制器对象
constexpr size_t OFF_LIST_ARRAY = 0x0040;       // 块指针数组（[2] 放块地址，模拟 CS2 结构）
constexpr size_t OFF_CHUNK = 0x1000;            // 实体块：64 个 0x78 条目，条目头 8 字节是指针
constexpr size_t OFF_ENTITIES = 0x4000;         // 实体本体（每个 0x1400）
constexpr size_t OFF_SCENES = 0x20000;          // 场景节点（每个 0x100）
constexpr size_t OFF_BONES = 0x22000;           // 骨骼数组（每个玩家 30 * 0x20）
constexpr size_t OFF_WEAPON_SERVICES = 0x28000; // 武器服务对象
constexpr size_t OFF_LOCAL_CONTROLLER_PTR = 0x0020;  // 控制器实体指针
constexpr size_t OFF_LOCAL_PAWN_PTR = 0x0028;        // 本地 pawn 指针
constexpr size_t OFF_C4_PTR = 0x0030;                // C4 实体指针
constexpr size_t OFF_ITEM_SERVICES = 0x29000;
constexpr size_t OFF_MONEY_SERVICES = 0x2A000;
// 武器实体内部（假布局，ini 里照抄）
constexpr size_t F_ACTIVE_WEAPON = 0x00;        // 武器服务对象里
constexpr size_t F_ITEM_DEF_INDEX = 0x04;       // 武器实体里
constexpr size_t F_CLIP = 0x80;                 // 武器实体里

constexpr size_t ENT_STRIDE = 0x78;
constexpr size_t ENT_BODY = 0x1400;      // 够放下 m_hController(0x1040) / C4 字段(0x11A0+) / 控制器名字(0x6F4)
constexpr size_t SCENE_BODY = 0x100;
constexpr size_t BONE_BODY = kBoneCount * 0x20;

// 实体字段
constexpr size_t F_ORIGIN = 0x00;        // 场景节点内
constexpr size_t F_HEALTH = 0x0C;
constexpr size_t F_TEAM = 0x10;
constexpr size_t F_LIFESTATE = 0x14;
constexpr size_t F_DORMANT = 0x18;
constexpr size_t F_SCENE = 0x20;
constexpr size_t F_NAME = 0x28;
constexpr size_t F_SPOTTED = 0x48;
constexpr size_t F_DEFUSING = 0x4C;
constexpr size_t F_SCOPED = 0x50;
constexpr size_t F_WEAPON_SERVICES = 0x54;
constexpr size_t F_BONE_PTR = 0x80;
// 新增：绘制用字段（偏移与 offsets_nltest.ini 一致）
constexpr size_t F_ARMOR = 0x58;
constexpr size_t F_FLASH = 0x5C;
constexpr size_t F_RELOAD = 0x60;
constexpr size_t F_SIMTIME = 0x64;
constexpr size_t F_ITEM_SERVICES = 0x68;
constexpr size_t F_MONEY_SERVICES = 0x70;
constexpr size_t F_CONTROLLER = 0x1040;
constexpr size_t F_PLAYER_PAWN = 0x914;
constexpr size_t F_MONEY = 0x40;
constexpr size_t F_HELMET = 0x49;
constexpr size_t F_PING = 0x830;
constexpr size_t F_MONEY_PTR = 0x810;
constexpr size_t F_NAME_CTRL = 0x6F4;
// C4
constexpr size_t F_BOMB_TICKING = 0x11A0;
constexpr size_t F_BOMB_SITE = 0x11A4;
constexpr size_t F_BOMB_BLOW = 0x11D0;
constexpr size_t F_BOMB_LEN = 0x11D8;
constexpr size_t F_BOMB_DEFUSING = 0x11DC;
constexpr size_t F_BOMB_DEFUSE_END = 0x11F0;
constexpr size_t F_BOMB_DEFUSER = 0x11F8;

constexpr int kWeaponIndex = 8;
constexpr int kControllerIndex = 9;
constexpr int kBombIndex = 10;

template <typename T>
T* At(size_t off) { return reinterpret_cast<T*>(g_image + off); }

float* ViewMatrix() { return At<float>(OFF_VIEW_MATRIX); }
uintptr_t* EntityListSlot() { return At<uintptr_t>(OFF_ENTITY_LIST); }
uintptr_t* LocalController() { return At<uintptr_t>(OFF_LOCAL_CONTROLLER); }
uintptr_t* LocalPawn() { return At<uintptr_t>(OFF_LOCAL_PAWN); }

uintptr_t EntityBody(int index) { return reinterpret_cast<uintptr_t>(g_image + OFF_ENTITIES + (size_t)index * ENT_BODY); }
uintptr_t SceneNode(int index) { return reinterpret_cast<uintptr_t>(g_image + OFF_SCENES + (size_t)index * SCENE_BODY); }
uintptr_t BoneArray(int index) { return reinterpret_cast<uintptr_t>(g_image + OFF_BONES + (size_t)index * BONE_BODY); }

void WriteBone(int player, int bone, float x, float y, float z)
{
    float* p = reinterpret_cast<float*>(BoneArray(player) + (size_t)bone * 0x20);
    p[0] = x;
    p[1] = y;
    p[2] = z;
    p[3] = 0.0f;
    p[4] = 0.0f;
    p[5] = 0.0f;
    p[6] = 1.0f;
}

// 一个站姿人形骨架（相对脚底坐标），骨骼索引与菜单里 kBoneIdx* 默认值一致
void BuildSkeleton(int player, float x, float y, float z)
{
    for (int b = 0; b < kBoneCount; ++b)
        WriteBone(player, b, x, y, z);
    WriteBone(player, 0, x, y, z + 4.0f);      // pelvis
    WriteBone(player, 4, x, y, z + 40.0f);     // chest
    WriteBone(player, 5, x, y, z + 56.0f);     // neck
    WriteBone(player, 6, x, y, z + 66.0f);     // head
    WriteBone(player, 9, x - 14.0f, y, z + 38.0f);
    WriteBone(player, 10, x + 14.0f, y, z + 38.0f);
    WriteBone(player, 23, x - 8.0f, y, z + 1.0f);
    WriteBone(player, 24, x + 8.0f, y, z + 1.0f);
}

void BuildViewMatrix(float camX, float camY, float camZ, float yawDeg, float pitchDeg, float fovDeg, float aspect)
{
    const float yaw = yawDeg * kPi / 180.0f;
    const float pitch = pitchDeg * kPi / 180.0f;
    // forward
    const float fx = cosf(pitch) * cosf(yaw);
    const float fy = cosf(pitch) * sinf(yaw);
    const float fz = -sinf(pitch);
    // right = normalize(cross(forward, worldUp))
    float rx = fy * 1.0f - fz * 0.0f;
    float ry = fz * 0.0f - fx * 1.0f;
    float rz = 0.0f;
    const float rl = sqrtf(rx * rx + ry * ry + rz * rz);
    if (rl > 0.0001f) { rx /= rl; ry /= rl; rz /= rl; }
    // up = cross(right, forward)
    const float ux = ry * fz - rz * fy;
    const float uy = rz * fx - rx * fz;
    const float uz = rx * fy - ry * fx;
    const float tanH = tanf(fovDeg * 0.5f * kPi / 180.0f);
    const float sx = 1.0f / (tanH * aspect);
    const float sy = 1.0f / tanH;
    auto dot = [](float ax, float ay, float az, float bx, float by, float bz) { return ax * bx + ay * by + az * bz; };
    float* m = ViewMatrix();
    m[0] = rx * sx;  m[1] = ry * sx;  m[2] = rz * sx;  m[3] = -dot(rx, ry, rz, camX, camY, camZ) * sx;
    m[4] = ux * sy;  m[5] = uy * sy;  m[6] = uz * sy;  m[7] = -dot(ux, uy, uz, camX, camY, camZ) * sy;
    m[8] = fx;       m[9] = fy;       m[10] = fz;      m[11] = -dot(fx, fy, fz, camX, camY, camZ);
    m[12] = fx;      m[13] = fy;      m[14] = fz;      m[15] = -dot(fx, fy, fz, camX, camY, camZ);
}

void WriteOffsetsFile()
{
    char path[MAX_PATH] = { 0 };
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    std::string dir(path);
    const size_t slash = dir.find_last_of("\\/");
    dir = (slash == std::string::npos) ? std::string(".") : dir.substr(0, slash);

    const uintptr_t base = (uintptr_t)GetModuleHandleA(nullptr);
    auto rva = [&](size_t off) { return (unsigned long long)((uintptr_t)(g_image + off) - base); };

    char file[MAX_PATH] = { 0 };
    snprintf(file, sizeof(file), "%s\\offsets_nltest.ini", dir.c_str());
    FILE* fp = nullptr;
    if (fopen_s(&fp, file, "wb") != 0 || fp == nullptr)
        return;
    fprintf(fp, "# NLTestTarget self-test offsets  (paste into menu > Memory)\n");
    fprintf(fp, "dwEntityList=0x%llX\n", rva(OFF_ENTITY_LIST));
    fprintf(fp, "dwLocalPlayerController=0x%llX\n", rva(OFF_LOCAL_CONTROLLER));
    fprintf(fp, "dwLocalPlayerPawn=0x%llX\n", rva(OFF_LOCAL_PAWN));
    fprintf(fp, "dwViewMatrix=0x%llX\n", rva(OFF_VIEW_MATRIX));
    fprintf(fp, "m_iHealth=0x%llX\n", (unsigned long long)F_HEALTH);
    fprintf(fp, "m_iTeamNum=0x%llX\n", (unsigned long long)F_TEAM);
    fprintf(fp, "m_lifeState=0x%llX\n", (unsigned long long)F_LIFESTATE);
    fprintf(fp, "m_bDormant=0x%llX\n", (unsigned long long)F_DORMANT);
    fprintf(fp, "m_pGameSceneNode=0x%llX\n", (unsigned long long)F_SCENE);
    fprintf(fp, "m_iszPlayerName=0x%llX\n", (unsigned long long)F_NAME);
    fprintf(fp, "m_bSpotted=0x%llX\n", (unsigned long long)F_SPOTTED);
    fprintf(fp, "m_bIsDefusing=0x%llX\n", (unsigned long long)F_DEFUSING);
    fprintf(fp, "m_bIsScoped=0x%llX\n", (unsigned long long)F_SCOPED);
    fprintf(fp, "m_pWeaponServices=0x%llX\n", (unsigned long long)F_WEAPON_SERVICES);
    fprintf(fp, "m_hActiveWeapon=0x0\n");
    fprintf(fp, "m_iItemDefinitionIndex=0x%llX\n", (unsigned long long)F_ITEM_DEF_INDEX);
    fprintf(fp, "m_hPlayerPawn=0x0\n");
    fprintf(fp, "m_iClip1=0x%llX\n", (unsigned long long)F_CLIP);
    fprintf(fp, "m_ArmorValue=0x%llX\n", (unsigned long long)F_ARMOR);
    fprintf(fp, "m_bHasHelmet=0x%llX\n", (unsigned long long)F_HELMET);
    fprintf(fp, "m_pItemServices=0x%llX\n", (unsigned long long)F_ITEM_SERVICES);
    fprintf(fp, "m_flFlashDuration=0x%llX\n", (unsigned long long)F_FLASH);
    fprintf(fp, "m_bInReload=0x%llX\n", (unsigned long long)F_RELOAD);
    fprintf(fp, "m_flSimulationTime=0x%llX\n", (unsigned long long)F_SIMTIME);
    fprintf(fp, "m_pInGameMoneyServices=0x%llX\n", (unsigned long long)F_MONEY_PTR);
    fprintf(fp, "m_iAccount=0x%llX\n", (unsigned long long)F_MONEY);
    fprintf(fp, "m_iPing=0x%llX\n", (unsigned long long)F_PING);
    fprintf(fp, "kDormantFromSceneNode=1\n");
    fprintf(fp, "dwPlantedC4=0x%llX\n", rva(OFF_C4_PTR));
    fprintf(fp, "m_bBombTicking=0x%llX\n", (unsigned long long)F_BOMB_TICKING);
    fprintf(fp, "m_flC4Blow=0x%llX\n", (unsigned long long)F_BOMB_BLOW);
    fprintf(fp, "m_flTimerLength=0x%llX\n", (unsigned long long)F_BOMB_LEN);
    fprintf(fp, "m_nBombSite=0x%llX\n", (unsigned long long)F_BOMB_SITE);
    fprintf(fp, "m_bBeingDefused=0x%llX\n", (unsigned long long)F_BOMB_DEFUSING);
    fprintf(fp, "m_flDefuseCountDown=0x%llX\n", (unsigned long long)F_BOMB_DEFUSE_END);
    fprintf(fp, "m_hBombDefuser=0x%llX\n", (unsigned long long)F_BOMB_DEFUSER);
    fprintf(fp, "m_vecAbsOrigin=0x%llX\n", (unsigned long long)F_ORIGIN);
    fprintf(fp, "m_modelState=0x0\n");
    fprintf(fp, "m_modelState_boneOffset=0x%llX\n", (unsigned long long)F_BONE_PTR);
    fprintf(fp, "kOriginFromSceneNode=1\n");
    fprintf(fp, "kMaxPlayers=%d\n", kMaxPlayers);
    fprintf(fp, "kBoneCount=%d\n", kBoneCount);
    fprintf(fp, "kBoneStep=0x20\n");
    fprintf(fp, "kEntityStride=0x%llX\n", (unsigned long long)ENT_STRIDE);
    fprintf(fp, "kViewMatrixFloats=16\n");
    fprintf(fp, "kBoneIdxHead=6\nkBoneIdxNeck=5\nkBoneIdxChest=4\nkBoneIdxPelvis=0\n");
    fprintf(fp, "kBoneIdxLeftFoot=23\nkBoneIdxRightFoot=24\n");
    fclose(fp);

    printf("NLTestTarget running\n");
    printf("  module base : 0x%llX\n", (unsigned long long)base);
    printf("  offsets file: %s\n", file);
    printf("  layout      : entityList=+0x%llX  viewMatrix=+0x%llX  entities=+0x%llX\n",
           rva(OFF_ENTITY_LIST), rva(OFF_VIEW_MATRIX), rva(OFF_ENTITIES));
    printf("  press CTRL+C to stop\n");
}
}

int main(int argc, char** argv)
{
    const bool staticMode = (argc > 1 && strstr(argv[1], "static") != nullptr);
    memcpy(g_image + OFF_MAGIC, "NLUI_TEST_IMAGE", 16);
    memcpy(g_image + OFF_SIG, "NLUIEL01", 8);

    // 块指针数组：索引 2（偏移 0x10）放块地址，与 CS2 的 list[0x8*(i>>9)+0x10] 结构一致
    uintptr_t* listArray = At<uintptr_t>(OFF_LIST_ARRAY);
    listArray[2] = (uintptr_t)(g_image + OFF_CHUNK);
    // 块内条目：每 0x78 字节，头部 8 字节是实体指针
    unsigned char* chunk = g_image + OFF_CHUNK;
    for (int i = 0; i < kMaxPlayers; ++i)
        *(uintptr_t*)(chunk + ENT_STRIDE * (size_t)i) = EntityBody(i);
    *(uintptr_t*)(chunk + ENT_STRIDE * kWeaponIndex) = EntityBody(kWeaponIndex);
    *(uintptr_t*)(chunk + ENT_STRIDE * kControllerIndex) = EntityBody(kControllerIndex);
    *(uintptr_t*)(chunk + ENT_STRIDE * kBombIndex) = EntityBody(kBombIndex);

    *EntityListSlot() = (uintptr_t)listArray;
    *LocalController() = EntityBody(kControllerIndex);
    *LocalPawn() = EntityBody(0);
    *(uintptr_t*)(g_image + OFF_C4_PTR) = EntityBody(kBombIndex);

    // 控制器实体（索引 9）：名字 / 延迟 / 金钱
    {
        unsigned char* ctrl = (unsigned char*)EntityBody(kControllerIndex);
        snprintf((char*)(ctrl + F_NAME_CTRL), 32, "小鱼");
        *(int*)(ctrl + F_PING) = 23;
        *(uintptr_t*)(ctrl + F_MONEY_PTR) = (uintptr_t)(g_image + OFF_MONEY_SERVICES);
        *(uint32_t*)(ctrl + F_PLAYER_PAWN) = 0;             // 指回本地 pawn（索引 0）
        *(int*)(g_image + OFF_MONEY_SERVICES + F_MONEY) = 4750;
    }

    // 物品服务（读头盔）
    {
        *(uintptr_t*)(g_image + OFF_ITEM_SERVICES + F_HELMET) = 0;
        *(unsigned char*)(g_image + OFF_ITEM_SERVICES + F_HELMET) = 1;
    }

    // C4（索引 10）：正在倒计时 + 有人在拆
    {
        unsigned char* c4 = (unsigned char*)EntityBody(kBombIndex);
        *(unsigned char*)(c4 + F_BOMB_TICKING) = 1;
        *(int*)(c4 + F_BOMB_SITE) = 1;
        *(float*)(c4 + F_BOMB_LEN) = 40.0f;
        *(float*)(c4 + F_BOMB_BLOW) = 0.0f;                 // 运行时按模拟时间填充
        *(unsigned char*)(c4 + F_BOMB_DEFUSING) = 1;
        *(uint32_t*)(c4 + F_BOMB_DEFUSER) = kControllerIndex;
        *(uintptr_t*)(c4 + F_SCENE) = SceneNode(kBombIndex);
    }

    // 本地玩家（索引 0）：T 阵营，站在原点
    {
        unsigned char* ent = (unsigned char*)EntityBody(0);
        *(int*)(ent + F_HEALTH) = 100;
        *(unsigned char*)(ent + F_TEAM) = 2;
        *(unsigned char*)(ent + F_LIFESTATE) = 0;
        *(unsigned char*)(ent + F_DORMANT) = 0;
        *(uintptr_t*)(ent + F_SCENE) = SceneNode(0);
        snprintf((char*)(ent + F_NAME), 32, "local");
        *(uint32_t*)(ent + F_CONTROLLER) = kControllerIndex;   // 本地 pawn -> 控制器
        *(uintptr_t*)(ent + F_ITEM_SERVICES) = (uintptr_t)(g_image + OFF_ITEM_SERVICES);
        *(int*)(ent + F_ARMOR) = 100;
        *(float*)(ent + F_FLASH) = 0.0f;
        *(unsigned char*)(ent + F_RELOAD) = 0;
        float* origin = (float*)(SceneNode(0) + F_ORIGIN);
        origin[0] = 0.0f; origin[1] = 0.0f; origin[2] = 0.0f;
        *(uintptr_t*)(SceneNode(0) + F_BONE_PTR) = BoneArray(0);
        BuildSkeleton(0, 0.0f, 0.0f, 0.0f);
    }

    // 武器服务 + 一把 ak47（索引 8）
    {
        *(uint32_t*)(g_image + OFF_WEAPON_SERVICES + F_ACTIVE_WEAPON) = (uint32_t)kWeaponIndex;
        unsigned char* wpn = (unsigned char*)EntityBody(kWeaponIndex);
        *(int*)(wpn + F_ITEM_DEF_INDEX) = 7;    // ak47
        *(int*)(wpn + F_CLIP) = 30;
    }

    for (int i = 1; i < kMaxPlayers; ++i)
    {
        unsigned char* ent = (unsigned char*)EntityBody(i);
        *(int*)(ent + F_HEALTH) = 100;
        *(unsigned char*)(ent + F_TEAM) = (i % 2 == 0) ? 2 : 3;
        *(unsigned char*)(ent + F_LIFESTATE) = 0;
        *(unsigned char*)(ent + F_DORMANT) = 0;
        *(uintptr_t*)(ent + F_SCENE) = SceneNode(i);
        *(uintptr_t*)(ent + F_WEAPON_SERVICES) = (uintptr_t)(g_image + OFF_WEAPON_SERVICES);
        *(uintptr_t*)(ent + F_ITEM_SERVICES) = (uintptr_t)(g_image + OFF_ITEM_SERVICES);
        *(int*)(ent + F_ARMOR) = (i == 1) ? 100 : ((i == 2) ? 65 : 0);
        *(float*)(ent + F_FLASH) = (i == 3) ? 0.85f : 0.0f;
        *(unsigned char*)(ent + F_RELOAD) = (i == 4) ? 1 : 0;
        snprintf((char*)(ent + F_NAME), 32, "player%d", i);
        *(uintptr_t*)(SceneNode(i) + F_BONE_PTR) = BoneArray(i);
    }
    *(uintptr_t*)(EntityBody(0) + F_WEAPON_SERVICES) = (uintptr_t)(g_image + OFF_WEAPON_SERVICES);

    WriteOffsetsFile();

    const float aspect = 1920.0f / 1080.0f;
    float t = 0.0f;
    if (staticMode)
        printf("  static mode: camera and players frozen at t=0\n");
    for (;;)
    {
        if (!staticMode)
            t += 0.016f;
        // 模拟时间：所有实体 + C4 共用一个推进的服务器时间
        const float simTime = 100.0f + t;
        for (int i = 0; i <= kBombIndex && i < kMaxPlayers + 3; ++i)
        {
            unsigned char* ent = (unsigned char*)EntityBody(i);
            *(float*)(ent + F_SIMTIME) = simTime;
        }
        {
            unsigned char* c4 = (unsigned char*)EntityBody(kBombIndex);
            // 炸了就重新安放，方便反复验证倒计时
            if (*(float*)(c4 + F_BOMB_BLOW) <= simTime)
            {
                *(float*)(c4 + F_BOMB_BLOW) = simTime + 26.0f;     // 26 秒后炸
                *(float*)(c4 + F_BOMB_DEFUSE_END) = simTime + 6.5f; // 拆包还剩 6.5 秒
            }
        }
        for (int i = 1; i < kMaxPlayers; ++i)
        {
            const float ang = (float)i * (2.0f * kPi / (float)(kMaxPlayers - 1)) + t * 0.35f;
            const float radius = 260.0f + 60.0f * sinf(t * 0.7f + (float)i);
            const float px = cosf(ang) * radius;
            const float py = sinf(ang) * radius;
            const float pz = 0.0f;
            unsigned char* ent = (unsigned char*)EntityBody(i);
            float* origin = (float*)(SceneNode(i) + F_ORIGIN);
            origin[0] = px;
            origin[1] = py;
            origin[2] = pz;
            BuildSkeleton(i, px, py, pz);
            *(int*)(ent + F_HEALTH) = 100 - (int)(35.0f * (0.5f + 0.5f * sinf(t * 0.9f + (float)i)));
            *(unsigned char*)(ent + F_SPOTTED) = (sinf(t * 0.6f + (float)i) > 0.0f) ? 1 : 0;
            *(unsigned char*)(ent + F_DEFUSING) = (i == 1) ? 1 : 0;
            *(unsigned char*)(ent + F_SCOPED) = (i == 2) ? 1 : 0;
        }
        BuildViewMatrix(0.0f, 0.0f, 64.0f, t * 22.0f, -6.0f, 90.0f, aspect);
        Sleep(16);
    }
    return 0;
}
