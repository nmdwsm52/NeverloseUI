// ---------------------------------------------------------------------------
//  NLBoneProbe - 骨骼表诊断工具（只读，外部模式）
//
//  用法：
//    NLBoneProbe.exe [进程名] [--module client.dll] [--dump work\bone_table.txt]
//                    [--pawns 3] [--selftest]
//
//  做的事：
//    1. 连接目标进程，按 offsets.ini 的实体列表方案遍历实体，找出真正的玩家 pawn；
//    2. 对每个玩家把整张骨骼表（默认 128 根）逐根 dump 出来：
//       idx / 世界坐标 / dz（相对脚底）/ dh（离中轴水平距离）/ side（按 yaw 的左右）
//       以及 pos@0x00 与 pos@0x10 两种解释（用来确认 CS2 的 CTransform 布局）；
//    3. 用 ClassifyBones() 推导关节索引，并把结果与实测几何一起写进报告，
//       同时打印每根推出来的关节是否通过几何校验。
//
//  这是纯诊断工具：不写目标进程内存，不改目标进程任何状态。
// ---------------------------------------------------------------------------
#include "core/memory.h"
#include "features/offsets.h"
#include "features/bonemap.h"

#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace nl;

namespace {

struct Args {
    std::string process = "cs2.exe";
    std::string module = "client.dll";
    std::string dump = "bone_table.txt";
    int  pawns = 3;
    bool selfTest = false;
    int  watch = 0;          // >0：对同一个玩家连续采样 N 次（每 150ms 一次）
    int  memTest = 0;        // >0：跑内存检查微基准（迭代次数）
};

Args ParseArgs(int argc, char** argv)
{
    Args a;
    for (int i = 1; i < argc; ++i)
    {
        const std::string s = argv[i];
        auto next = [&](std::string* out) -> bool
        {
            if (i + 1 >= argc)
                return false;
            *out = argv[++i];
            return true;
        };
        if (s == "--module")
            next(&a.module);
        else if (s == "--dump")
            next(&a.dump);
        else if (s == "--pawns")
        {
            std::string v;
            if (next(&v))
                a.pawns = atoi(v.c_str());
        }
        else if (s == "--selftest")
            a.selfTest = true;
        else if (s == "--memtest")
        {
            std::string v;
            a.memTest = next(&v) ? atoi(v.c_str()) : 20000;
        }
        else if (s == "--watch")
        {
            std::string v;
            if (next(&v))
                a.watch = atoi(v.c_str());
        }
        else if (!s.empty() && s[0] != '-')
            a.process = s;
    }
    return a;
}

uintptr_t Off(const char* name)
{
    OffsetSlot* s = FindOffsetSlot(name);
    return s ? s->value : 0;
}

uintptr_t EntityFromIndex(const Memory& mem, uintptr_t base, int index, std::string* err)
{
    const uintptr_t list = mem.Read<uintptr_t>(base + Off("dwEntityList"));
    if (list == 0)
    {
        if (err) *err = "dwEntityList=0";
        return 0;
    }
    if (!mem.IsValidRange(list, sizeof(uintptr_t)))
    {
        if (err) *err = "实体列表指针不可读";
        return 0;
    }
    const int scheme = (int)Off("kEntityScheme");
    const uintptr_t stride = Off("kEntityStride");
    const uintptr_t chunkOffset = Off("kEntityChunkOffset");
    const bool entryIsPointer = (Off("kEntityEntryPointer") != 0);
    auto pick = [&](uintptr_t slot) -> uintptr_t
    {
        const uintptr_t value = entryIsPointer ? mem.Read<uintptr_t>(slot) : slot;
        return mem.IsValidRange(value, 0x10) ? value : 0;
    };
    if (scheme == 1)
        return pick(list + stride * (uintptr_t)index + chunkOffset);
    const uintptr_t chunk = mem.Read<uintptr_t>(list + 0x8 * ((index & 0x7FFF) >> 9) + chunkOffset);
    if (chunk == 0 || !mem.IsValidRange(chunk, sizeof(uintptr_t)))
        return 0;
    return pick(chunk + stride * (uintptr_t)(index & 0x1FF));
}

struct BoneRaw {
    int   index = 0;
    float p0[3] = { 0, 0, 0 };    // 按 pos@0x00 解释
    float p1[3] = { 0, 0, 0 };    // 按 pos@0x10 解释（quat 在前）
    float quat[4] = { 0, 0, 0, 0 };
    float dz = 0.0f;
    float dh = 0.0f;
    float side = 0.0f;            // 沿玩家右手方向的分量
    float fwd = 0.0f;             // 沿玩家正面向量的分量
};

// 玩家朝向：CS2 的 m_angEyeAngles 是 QAngle { pitch, yaw, roll }（角度制）
// forward = (cos yaw, sin yaw)   right = (sin yaw, -cos yaw)
void YawFrame(float yawDeg, float* fx, float* fy, float* rx, float* ry)
{
    const float r = yawDeg * 3.14159265358979323846f / 180.0f;
    *fx = cosf(r);
    *fy = sinf(r);
    *rx = sinf(r);
    *ry = -cosf(r);
}

std::string HexDump(const std::vector<uint8_t>& b)
{
    char buf[8];
    std::string out;
    for (size_t i = 0; i < b.size(); ++i)
    {
        snprintf(buf, sizeof(buf), "%02X ", b[i]);
        out += buf;
        if ((i % 16) == 15 && i + 1 < b.size())
            out += "\n            ";
    }
    return out;
}

// ---------------------------------------------------------------- 内存检查微基准
// 目的：内部模式下"每次读之前先 VirtualQuery"的成本有多高 ——
// 这正是注入版帧时间被打到 80ms 的元凶（一帧里成百上千次检查）。
// 这里用同一段内存做 A/B：裸 VirtualQuery vs Memory::IsValidRange（带 region 缓存）。
int RunMemTest(int iterations)
{
    nl::Memory mem;
    if (!mem.Open("self"))
    {
        printf("[x] 打不开 self（内部模式）\n");
        return 2;
    }
    std::vector<uint8_t> buf(1 << 20, 0x5A);
    const size_t span = buf.size() - 64;

    LARGE_INTEGER f, a, b;
    QueryPerformanceFrequency(&f);
    auto ms = [&](const LARGE_INTEGER& x, const LARGE_INTEGER& y)
    {
        return (double)(y.QuadPart - x.QuadPart) * 1000.0 / (double)f.QuadPart;
    };

    volatile size_t sink = 0;
    QueryPerformanceCounter(&a);
    for (int i = 0; i < iterations; ++i)
    {
        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(buf.data() + (size_t)(i * 64) % span, &mbi, sizeof(mbi)) != 0)
            sink += mbi.RegionSize;
    }
    QueryPerformanceCounter(&b);
    const double rawMs = ms(a, b);

    size_t ok = 0;
    QueryPerformanceCounter(&a);
    for (int i = 0; i < iterations; ++i)
    {
        if (mem.IsValidRange((uintptr_t)buf.data() + (size_t)(i * 64) % span, 8))
            ++ok;
    }
    QueryPerformanceCounter(&b);
    const double cachedMs = ms(a, b);
    (void)sink;

    printf("迭代 %d 次，同一段 1MB 内存：\n", iterations);
    printf("  裸 VirtualQuery            %8.2f ms  (%6.3f us/次)\n", rawMs, rawMs * 1000.0 / iterations);
    printf("  IsValidRange(带 region 缓存) %8.2f ms  (%6.3f us/次)  命中 %zu/%d\n",
           cachedMs, cachedMs * 1000.0 / iterations, ok, iterations);
    printf("  加速比 %.1fx（注入版一帧大约上千次这样的检查）\n", (cachedMs > 0.0) ? (rawMs / cachedMs) : 0.0);
    return 0;
}

// ---------------------------------------------------------------- 合成自检
// 1) 合成骨架：128~160 根骨，含脊柱台阶、帽子抖动骨、躯干干扰骨、离身 200cm 的废骨；
// 2) 真实骨架：从活着的 cs2.exe 里采下来的一张真实骨骼表（见 NLBoneProbe 的 dump），
//    用同一套断言检查分类器在真实数据上的表现（头/脚具体索引、四肢高度区间、不许选垃圾骨）；
// 3) 反向用例：整张表都是垃圾时必须判定失败，而不是硬凑一副骨架。
struct JointExpect {
    int   joint;
    int   exact;                 // -1 = 不校验具体索引
    float dzLo, dzHi;
    float latLo, latHi;
    int   sideSign;              // 0 = 不校验左右
};

bool CheckJoint(FILE* fp, const std::vector<BoneSample>& bones, const BonePoint& origin,
                const int jointIndex[Bone_Count], const JointExpect& e, float axX, float axY,
                const char* indent)
{
    const int id = jointIndex[e.joint];
    if (id < 0)
    {
        fprintf(fp, "%s[x] %-10s 缺失\n", indent, BoneJointName(e.joint));
        return false;
    }
    const BoneSample* s = nullptr;
    for (const BoneSample& b : bones)
    {
        if (b.index == id)
        {
            s = &b;
            break;
        }
    }
    if (s == nullptr)
    {
        fprintf(fp, "%s[x] %-10s 索引 %d 不在表里\n", indent, BoneJointName(e.joint), id);
        return false;
    }
    const float dx = s->pos.x - origin.x;
    const float dy = s->pos.y - origin.y;
    const float dz = s->pos.z - origin.z;
    const float dh = sqrtf(dx * dx + dy * dy);
    const float lat = dx * axX + dy * axY;
    const float alat = fabsf(lat);
    bool ok = (dz >= e.dzLo && dz <= e.dzHi) && dh <= 60.0f;
    if (e.exact >= 0 && id != e.exact)
        ok = false;
    if ((axX != 0.0f || axY != 0.0f) && e.latHi > 0.0f)
    {
        if (alat < e.latLo || alat > e.latHi)
            ok = false;
        if (e.sideSign != 0 && ((lat > 0.0f) != (e.sideSign > 0)))
            ok = false;
    }
    fprintf(fp, "%s%-4s %-10s idx=%-4d dz=%-7.2f dh=%-7.2f lat=%-7.2f %s%s\n", indent,
            ok ? "ok" : "FAIL", BoneJointName(e.joint), id, dz, dh, lat,
            ok ? "" : "  <- 不在期望区间", (e.exact >= 0 && id != e.exact) ? "（索引不符）" : "");
    return ok;
}

int RunSelfTest(const std::string& outPath)
{
    const float yaws[3] = { 0.0f, 37.0f, 210.0f };
    FILE* fp = fopen(outPath.c_str(), "wb");
    if (fp == nullptr)
    {
        printf("[x] 无法写入 %s\n", outPath.c_str());
        return 2;
    }
    fprintf(fp, "# NLBoneProbe --selftest  (合成骨骼表 -> ClassifyBones 回归)\n");
    fprintf(fp, "# 骨架：脊柱台阶 bone 100..110(dz 30..67) + 帽子抖动骨 111(dz 70, 比头骨高)\n");
    fprintf(fp, "#       肩 120/121(±9) 肘 122/123(±11) 手 124/125(±13) 膝 130/131(±6.5) 踝 140/141(±7)\n");
    fprintf(fp, "#       另有 24 根离轴 8cm 的装备/手指干扰骨、以及一堆离轴 200cm 的废骨\n");
    int failures = 0;
    for (int t = 0; t < 3; ++t)
    {
        const float yaw = yaws[t];
        float fx, fy, rx, ry;
        YawFrame(yaw, &fx, &fy, &rx, &ry);
        const BonePoint origin = { 100.0f, -50.0f, 0.0f };

        std::vector<BoneSample> bones(160);
        for (size_t i = 0; i < bones.size(); ++i)   // 默认全部丢到"离身体 200cm"，等价于无效骨
            bones[i] = BoneSample{ (int)i, BonePoint{ origin.x + 200.0f * fx, origin.y + 200.0f * fy, origin.z - 40.0f } };

        auto put = [&](int index, float side, float fwd, float dz)
        {
            BonePoint p;
            p.x = origin.x + side * rx + fwd * fx;
            p.y = origin.y + side * ry + fwd * fy;
            p.z = origin.z + dz;
            bones[(size_t)index] = BoneSample{ index, p };
        };
        // 脊柱台阶（贴着中轴，side 抖动 < 0.6cm）
        const float ladder[11] = { 30.0f, 33.0f, 35.0f, 40.0f, 44.0f, 48.0f, 52.0f, 56.0f, 60.0f, 63.5f, 67.0f };
        for (int i = 0; i < 11; ++i)
            put(100 + i, (i % 2 == 0) ? 0.4f : -0.4f, 0.2f, ladder[i]);
        put(111, 1.2f, 0.6f, 70.0f);           // 头顶点会落在最高的那根（帽子/头发抖动骨）
        // 干扰项：举枪时"头顶高度、但在身前 30cm"的那根骨（真实模型上就是它把方框拽到枪上去的）
        put(112, 0.8f, 12.0f, 68.5f);
        // 躯干两侧的干扰骨：离轴 8cm，正卡在肩/手的高度带里，但比肩窄，不该被选中
        for (int i = 0; i < 12; ++i)
        {
            const float dz = 30.0f + (float)i * 2.5f;
            put(20 + i * 2, 8.0f, 1.0f, dz);
            put(21 + i * 2, -8.0f, 1.0f, dz);
        }
        put(120, 9.0f, 1.0f, 59.0f);    // shoulderR
        put(121, -9.0f, 1.0f, 59.0f);   // shoulderL
        put(122, 11.0f, 3.5f, 53.5f);   // elbowR（正好在肩-手中点）
        put(123, -11.0f, 3.5f, 53.5f);  // elbowL
        put(124, 13.0f, 6.0f, 48.0f);   // handR
        put(125, -13.0f, 6.0f, 48.0f);  // handL
        put(130, 6.5f, 0.5f, 20.0f);    // kneeR
        put(131, -6.5f, 0.5f, 20.0f);   // kneeL
        put(140, 7.0f, -1.0f, 8.0f);    // footR
        put(141, -7.0f, -1.0f, 8.0f);   // footL

        // 期望值锁"解剖学区间"（索引是模型相关的，只对无歧义的锁具体索引）
        const JointExpect expect[] = {
            { Bone_Head,      111, 68.0f, 72.0f,  0.0f,  6.0f,  0 },
            { Bone_Neck,       -1, 57.0f, 66.0f,  0.0f,  8.0f,  0 },
            { Bone_Chest,      -1, 46.0f, 56.0f,  0.0f,  8.0f,  0 },
            { Bone_Pelvis,     -1, 33.0f, 42.0f,  0.0f,  8.0f,  0 },
            { Bone_ShoulderR,  -1, 56.0f, 62.0f,  8.0f, 12.0f,  1 },
            { Bone_ShoulderL,  -1, 56.0f, 62.0f,  8.0f, 12.0f, -1 },
            { Bone_ElbowR,     -1, 50.0f, 57.0f,  8.0f, 14.0f,  1 },
            { Bone_ElbowL,     -1, 50.0f, 57.0f,  8.0f, 14.0f, -1 },
            { Bone_HandR,      -1, 44.0f, 52.0f, 11.0f, 18.0f,  1 },
            { Bone_HandL,      -1, 44.0f, 52.0f, 11.0f, 18.0f, -1 },
            { Bone_KneeR,      -1, 18.0f, 23.0f,  5.0f,  9.0f,  1 },
            { Bone_KneeL,      -1, 18.0f, 23.0f,  5.0f,  9.0f, -1 },
            { Bone_FootR,     140,  5.0f, 11.0f,  5.0f,  9.0f,  1 },
            { Bone_FootL,     141,  5.0f, 11.0f,  5.0f,  9.0f, -1 },
        };

        int jointIndex[Bone_Count] = { 0 };
        BoneClassifyReport rep;
        const bool ok = ClassifyBones(bones.data(), (int)bones.size(), origin, yaw, jointIndex, &rep);
        fprintf(fp, "\n[case %d] yaw=%.0f  classify=%s  reason=%s\n", t, yaw, ok ? "OK" : "FAIL", rep.reason);
        bool caseOk = ok;
        for (const JointExpect& e : expect)
            caseOk = CheckJoint(fp, bones, origin, jointIndex, e, rx, ry, "") && caseOk;
        if (rep.maxSpineDeviation > 25.0f || rep.maxLimbDeviation > 40.0f)
        {
            fprintf(fp, "  [!] 偏差过大 spine=%.1f limb=%.1f\n", rep.maxSpineDeviation, rep.maxLimbDeviation);
            caseOk = false;
        }
        fprintf(fp, "  -> %s\n", caseOk ? "PASS" : "FAIL");
        if (!caseOk)
            ++failures;
    }
    // ---- 真实数据回归：从活着的 cs2.exe 采下来的一张骨骼表（101 根）----
    // 采集现场：CT 玩家，origin=(1216.00, -16.00, -163.97)，行进姿态（一脚前一脚后、
    // 身体前倾），骨骼表尾部 101+ 是未初始化的 0，index 27 是 y=988 的垃圾骨。
    {
        static const BoneSample kRecorded[] = {
        { 0, { 1216.00f, -16.00f, -165.68f } }, { 1, { 1210.62f, -19.72f, -127.03f } }, { 2, { 1210.66f, -19.73f, -126.10f } },
        { 3, { 1211.14f, -19.28f, -122.08f } }, { 4, { 1211.87f, -18.51f, -117.46f } }, { 5, { 1212.61f, -18.16f, -111.16f } },
        { 6, { 1214.30f, -15.38f, -105.52f } }, { 7, { 1215.93f, -11.34f, -101.75f } }, { 8, { 1213.14f, -13.38f, -108.71f } },
        { 9, { 1206.36f, -13.83f, -108.61f } }, { 10, { 1207.25f, -5.43f, -116.83f } }, { 11, { 1214.78f, 1.51f, -112.60f } },
        { 12, { 1215.54f, -14.15f, -109.05f } }, { 13, { 1221.45f, -17.44f, -109.74f } }, { 14, { 1223.00f, -8.31f, -117.03f } },
        { 15, { 1217.63f, -0.22f, -111.69f } }, { 16, { 1216.22f, -26.68f, -114.57f } }, { 17, { 1208.24f, -16.50f, -130.59f } },
        { 18, { 1210.17f, -4.03f, -143.29f } }, { 19, { 1205.93f, -5.29f, -159.64f } }, { 20, { 1214.21f, -20.85f, -130.85f } },
        { 21, { 1222.45f, -21.19f, -146.74f } }, { 22, { 1218.52f, -28.95f, -161.28f } }, { 23, { 1212.61f, -18.16f, -111.16f } },
        { 24, { 1216.85f, 5.37f, -108.07f } }, { 25, { 1215.24f, -7.13f, -100.41f } }, { 26, { 1217.91f, -7.50f, -100.68f } },
        { 27, { 1182.58f, 988.10f, -101.45f } }, { 28, { 1214.88f, 2.75f, -112.13f } }, { 29, { 1215.18f, 5.51f, -111.68f } },
        { 30, { 1217.01f, 6.04f, -111.89f } }, { 31, { 1218.44f, 5.53f, -111.96f } }, { 32, { 1215.26f, 2.86f, -113.40f } },
        { 33, { 1215.70f, 4.98f, -113.57f } }, { 34, { 1217.36f, 5.01f, -113.79f } }, { 35, { 1217.94f, 4.45f, -113.59f } },
        { 36, { 1214.92f, 2.54f, -111.46f } }, { 37, { 1215.07f, 5.28f, -110.65f } }, { 38, { 1216.80f, 6.07f, -110.77f } },
        { 39, { 1218.10f, 6.15f, -110.88f } }, { 40, { 1215.63f, 2.42f, -111.15f } }, { 41, { 1215.71f, 3.47f, -109.62f } },
        { 42, { 1215.76f, 4.84f, -109.08f } }, { 43, { 1214.98f, 2.81f, -112.75f } }, { 44, { 1215.37f, 5.33f, -112.73f } },
        { 45, { 1217.19f, 5.77f, -112.95f } }, { 46, { 1218.43f, 5.33f, -112.98f } }, { 47, { 1209.76f, -3.12f, -115.42f } },
        { 48, { 1212.27f, -0.80f, -114.01f } }, { 49, { 1206.66f, -11.03f, -111.35f } }, { 50, { 1206.95f, -8.23f, -114.09f } },
        { 51, { 1217.92f, 0.90f, -111.02f } }, { 52, { 1218.93f, 3.43f, -110.38f } }, { 53, { 1217.91f, 5.04f, -110.63f } },
        { 54, { 1216.40f, 5.21f, -110.68f } }, { 55, { 1217.94f, 1.28f, -112.29f } }, { 56, { 1218.45f, 3.39f, -112.31f } },
        { 57, { 1217.22f, 4.52f, -112.29f } }, { 58, { 1216.53f, 4.08f, -112.17f } }, { 59, { 1217.63f, 0.65f, -110.42f } },
        { 60, { 1218.51f, 3.17f, -109.38f } }, { 61, { 1218.60f, 5.08f, -109.33f } }, { 62, { 1217.85f, 6.13f, -109.49f } },
        { 63, { 1216.88f, 0.83f, -110.27f } }, { 64, { 1216.19f, 2.16f, -109.18f } }, { 65, { 1215.81f, 3.39f, -108.46f } },
        { 66, { 1218.01f, 1.05f, -111.62f } }, { 67, { 1218.72f, 3.48f, -111.40f } }, { 68, { 1217.52f, 4.92f, -111.56f } },
        { 69, { 1216.29f, 4.43f, -111.44f } }, { 70, { 1221.21f, -5.61f, -115.25f } }, { 71, { 1219.42f, -2.91f, -113.47f } },
        { 72, { 1221.97f, -14.40f, -112.17f } }, { 73, { 1222.48f, -11.35f, -114.60f } }, { 74, { 1207.10f, 0.08f, -162.78f } },
        { 75, { 1208.89f, -12.35f, -134.83f } }, { 76, { 1209.53f, -8.19f, -139.06f } }, { 77, { 1223.44f, -26.53f, -164.44f } },
        { 78, { 1216.96f, -20.96f, -136.15f } }, { 79, { 1219.70f, -21.07f, -141.45f } }, { 80, { 1215.11f, -13.36f, -103.63f } },
        { 81, { 1207.16f, -13.31f, -106.66f } }, { 82, { 1221.21f, -16.63f, -107.75f } }, { 83, { 1213.87f, -17.65f, -103.40f } },
        { 84, { 1215.80f, -22.41f, -113.60f } }, { 85, { 1207.04f, -18.86f, -112.77f } }, { 86, { 1205.26f, -9.45f, -134.56f } },
        { 87, { 1213.99f, -26.21f, -127.26f } }, { 88, { 1216.55f, -25.53f, -127.51f } }, { 89, { 1219.17f, -22.47f, -127.71f } },
        { 90, { 1219.76f, -19.91f, -127.80f } }, { 91, { 1218.98f, -17.34f, -127.62f } }, { 92, { 1209.52f, -24.79f, -109.51f } },
        { 93, { 1216.22f, -26.68f, -114.57f } }, { 94, { 1207.95f, -24.65f, -125.43f } }, { 95, { 1210.43f, -24.90f, -122.47f } },
        { 96, { 1217.44f, -3.61f, -96.95f } }, { 97, { 1216.85f, 5.37f, -108.07f } }, { 98, { 1216.82f, 2.59f, -111.26f } },
        { 99, { 1216.63f, 3.79f, -112.26f } }, { 100, { 1216.00f, -16.00f, -123.97f } },
        };
        const std::vector<BoneSample> bones(kRecorded, kRecorded + sizeof(kRecorded) / sizeof(kRecorded[0]));
        const BonePoint origin = { 1216.00f, -16.00f, -163.97f };
        int jointIndex[Bone_Count] = { 0 };
        BoneClassifyReport rep;
        const bool ok = ClassifyBones(bones.data(), (int)bones.size(), origin, 91.91f, jointIndex, &rep);
        fprintf(fp, "\n[case 4] 真实 CS2 骨骼表（101 根，行进姿态）classify=%s reason=%s 左右轴=%.1f 度 对称面误差=%.2f\n",
                ok ? "OK" : "FAIL", rep.reason, rep.axisDeg, rep.axisCost);
        bool caseOk = ok;
        // 用分类器自己拟合出来的左右轴来校验左右，和被测代码同一套约定
        const float repRad = rep.axisDeg * 3.14159265358979323846f / 180.0f;
        const float ax = cosf(repRad), ay = sinf(repRad);
        const JointExpect expect[] = {
            { Bone_Head,        7, 58.0f, 70.0f,  0.0f,  0.0f, 0 },
            { Bone_Neck,       -1, 50.0f, 62.0f,  0.0f, 10.0f, 0 },
            { Bone_Chest,      -1, 42.0f, 55.0f,  0.0f, 10.0f, 0 },
            { Bone_Pelvis,     -1, 32.0f, 42.0f,  0.0f, 10.0f, 0 },
            { Bone_ShoulderR,  -1, 45.0f, 60.0f,  3.0f, 16.0f,  1 },
            { Bone_ShoulderL,  -1, 45.0f, 60.0f,  3.0f, 16.0f, -1 },
            { Bone_HandR,      -1, 40.0f, 62.0f,  0.0f,  0.0f, 0 },
            { Bone_HandL,      -1, 40.0f, 62.0f,  0.0f,  0.0f, 0 },
            { Bone_KneeR,      -1, 12.0f, 30.0f,  2.0f, 16.0f,  1 },
            { Bone_KneeL,      -1, 12.0f, 30.0f,  2.0f, 16.0f, -1 },
        };
        for (const JointExpect& e : expect)
            caseOk = CheckJoint(fp, bones, origin, jointIndex, e, ax, ay, "  ") && caseOk;
        // 手必须比肩更远离身体中轴（手臂总得伸出去）
        {
            auto dhOf = [&](int id) -> float
            {
                for (const BoneSample& b : bones)
                {
                    if (b.index == id)
                    {
                        const float dx = b.pos.x - origin.x;
                        const float dy = b.pos.y - origin.y;
                        return sqrtf(dx * dx + dy * dy);
                    }
                }
                return -1.0f;
            };
            const float shR = dhOf(jointIndex[Bone_ShoulderR]);
            const float shL = dhOf(jointIndex[Bone_ShoulderL]);
            const float handR = dhOf(jointIndex[Bone_HandR]);
            const float handL = dhOf(jointIndex[Bone_HandL]);
            const bool armsOk = (handR > shR + 2.0f) && (handL > shL + 2.0f);
            fprintf(fp, "  %-4s 手比肩更靠外：handR dh=%.1f > 肩 dh=%.1f，handL dh=%.1f > 肩 dh=%.1f\n",
                    armsOk ? "ok" : "FAIL", handR, shR, handL, shL);
            caseOk = caseOk && armsOk;
        }
        // 两只脚必须是贴地那对（74/77），且不能是 index 27 那种垃圾骨
        {
            const bool feetOk = ((jointIndex[Bone_FootR] == 74 && jointIndex[Bone_FootL] == 77) ||
                                 (jointIndex[Bone_FootR] == 77 && jointIndex[Bone_FootL] == 74));
            fprintf(fp, "  %-4s 脚骨必须是贴地那对 74/77，实际 %d/%d\n", feetOk ? "ok" : "FAIL",
                    jointIndex[Bone_FootR], jointIndex[Bone_FootL]);
            caseOk = caseOk && feetOk;
            bool garbage = false;
            for (int j = 0; j < Bone_Count; ++j)
            {
                if (jointIndex[j] == 27)
                {
                    fprintf(fp, "  [!] 关节 %s 选中了垃圾骨 %d\n", BoneJointName(j), jointIndex[j]);
                    garbage = true;
                }
            }
            if (garbage)
                caseOk = false;
        }
        if (rep.maxSpineDeviation > 25.0f)
        {
            fprintf(fp, "  [!] 躯干偏离身体轴过大 %.1f\n", rep.maxSpineDeviation);
            caseOk = false;
        }
        fprintf(fp, "  -> %s\n", caseOk ? "PASS" : "FAIL");
        if (!caseOk)
            ++failures;
    }

    // 反向用例：整张表都是垃圾（全部离轴 300+）——必须判定失败，而不是硬凑一副骨架
    {
        BonePoint origin = { 0.0f, 0.0f, 0.0f };
        std::vector<BoneSample> bones;
        bones.resize(128);
        for (int i = 0; i < 128; ++i)
            bones[(size_t)i] = BoneSample{ i, BonePoint{ 400.0f + (float)i, 400.0f, (float)i * 3.0f } };
        int jointIndex[Bone_Count] = { 0 };
        BoneClassifyReport rep;
        const bool ok = ClassifyBones(bones.data(), (int)bones.size(), origin, 0.0f, jointIndex, &rep);
        fprintf(fp, "\n[case 3] 垃圾骨骼表（全部离轴 565+）: classify=%s reason=%s -> %s\n",
                ok ? "OK" : "FAIL", rep.reason, ok ? "FAIL(不该通过)" : "PASS");
        if (ok)
            ++failures;
    }
    fprintf(fp, "\n==== selftest %s (failures=%d) ====\n", failures == 0 ? "PASS" : "FAIL", failures);
    fclose(fp);
    printf("[selftest] %s  (failures=%d)  报告: %s\n", failures == 0 ? "PASS" : "FAIL", failures, outPath.c_str());
    return failures == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv)
{
    const Args args = ParseArgs(argc, argv);
    if (!OffsetsLoad())
    {
        printf("[x] 读不到 offsets.ini（应与本 exe 同目录）\n");
        return 2;
    }
    if (args.selfTest)
        return RunSelfTest(args.dump);
    if (args.memTest > 0)
        return RunMemTest(args.memTest);

    Memory mem;
    if (!mem.Open(args.process.c_str()))
    {
        printf("[x] 打不开进程 %s（需要管理员权限 / 进程未运行）\n", args.process.c_str());
        return 2;
    }
    const uintptr_t base = mem.ModuleBase(args.module.c_str());
    printf("[+] %s pid=%lu  %s base=0x%llX\n", args.process.c_str(), mem.Pid(), args.module.c_str(),
           (unsigned long long)base);
    if (base == 0)
        return 2;

    const int boneCount = (int)Off("kBoneCount");
    const int layout = (int)Off("kBoneLayout");
    const uintptr_t stride = (layout == 1) ? 0x30 : Off("kBoneStep");
    const uintptr_t posOff = (layout == 1) ? 0x0C : 0;

    FILE* fp = fopen(args.dump.c_str(), "wb");
    if (fp == nullptr)
    {
        printf("[x] 无法写入 %s\n", args.dump.c_str());
        return 2;
    }
    fprintf(fp, "# NLBoneProbe 进程=%s pid=%lu 模块=%s base=0x%llX\n", args.process.c_str(), mem.Pid(),
            args.module.c_str(), (unsigned long long)base);
    fprintf(fp, "# kBoneLayout=%d kBoneStep=0x%llX m_modelState=0x%llX m_modelState_boneOffset=0x%llX kBoneCount=%d\n",
            layout, (unsigned long long)Off("kBoneStep"), (unsigned long long)Off("m_modelState"),
            (unsigned long long)Off("m_modelState_boneOffset"), boneCount);
    fprintf(fp, "# 配置里的固定索引: head=%llu neck=%llu chest=%llu pelvis=%llu lfoot=%llu rfoot=%llu\n",
            (unsigned long long)Off("kBoneIdxHead"), (unsigned long long)Off("kBoneIdxNeck"),
            (unsigned long long)Off("kBoneIdxChest"), (unsigned long long)Off("kBoneIdxPelvis"),
            (unsigned long long)Off("kBoneIdxLeftFoot"), (unsigned long long)Off("kBoneIdxRightFoot"));

    // 本地 pawn：用来确认原点/朝向读取方式
    const uintptr_t localPawn = mem.Read<uintptr_t>(base + Off("dwLocalPlayerPawn"));
    fprintf(fp, "# dwLocalPlayerPawn=0x%llX\n", (unsigned long long)localPawn);

    const int limit = (int)Off("kEntityIndexLimit");
    int found = 0;
    for (int idx = 0; idx < limit && found < args.pawns; ++idx)
    {
        std::string err;
        const uintptr_t ent = EntityFromIndex(mem, base, idx, &err);
        if (ent == 0)
            continue;
        const int health = mem.Read<int32_t>(ent + Off("m_iHealth"));
        if (health <= 0 || health > 200)
            continue;
        const int team = (int)mem.Read<uint8_t>(ent + Off("m_iTeamNum"));
        if (team != 2 && team != 3)
            continue;
        const uintptr_t scene = mem.Read<uintptr_t>(ent + Off("m_pGameSceneNode"));
        if (!mem.IsValidRange(scene, 0x40))
            continue;
        const int lifeState = (int)mem.Read<uint8_t>(ent + Off("m_lifeState"));
        if (lifeState != 0)
            continue;
        const uintptr_t boneArr = (layout == 1)
                                      ? mem.Read<uintptr_t>(ent + Off("m_dwBoneMatrix"))
                                      : mem.Read<uintptr_t>(scene + Off("m_modelState") + Off("m_modelState_boneOffset"));
        if (!mem.IsValidRange(boneArr, 0x20))
            continue;

        float origin[3] = { 0, 0, 0 };
        const uintptr_t originAddr = (Off("kOriginFromSceneNode") != 0)
                                         ? scene + Off("m_vecAbsOrigin")
                                         : ent + Off("m_vecAbsOrigin");
        mem.ReadRaw(originAddr, origin, sizeof(origin));
        float eye[3] = { 0, 0, 0 };
        mem.ReadRaw(ent + Off("m_angEyeAngles"), eye, sizeof(eye));
        const bool isLocal = (ent == localPawn);

        ++found;
        fprintf(fp, "\n===== pawn #%d idx=%d ent=0x%llX%s hp=%d team=%d =====\n", found, idx,
                (unsigned long long)ent, isLocal ? " (local)" : "", health, team);
        fprintf(fp, "origin=(%.2f, %.2f, %.2f)  eyeAngles=(%.2f, %.2f, %.2f)  scene=0x%llX boneArr=0x%llX\n",
                origin[0], origin[1], origin[2], eye[0], eye[1], eye[2],
                (unsigned long long)scene, (unsigned long long)boneArr);

        // 原始字节：确认 CTransform 布局（位置在前还是四元数在前）
        fprintf(fp, "-- 原始 0x20 字节（bone 0..3）--\n");
        for (int i = 0; i < 4 && i < boneCount; ++i)
        {
            const std::vector<uint8_t> raw = mem.ReadBytes(boneArr + stride * (uintptr_t)i, 0x20);
            fprintf(fp, "  bone[%d] @0x%llX: %s\n", i, (unsigned long long)(boneArr + stride * (uintptr_t)i),
                    HexDump(raw).c_str());
        }

        float fx, fy, rx, ry;
        YawFrame(eye[1], &fx, &fy, &rx, &ry);
        fprintf(fp, "-- 骨骼表（pos@0x00 解释；dz=相对脚底高度, dh=离中轴水平距离, side=右向分量, fwd=前向分量）--\n");
        fprintf(fp, "%-4s %-10s %-10s %-10s %-9s %-8s %-8s %-8s %-10s\n",
                "idx", "x", "y", "z", "dz", "dh", "side", "fwd", "pos@0x10.z");

        std::vector<BoneSample> samples;
        std::vector<BoneRaw> table;
        for (int i = 0; i < boneCount; ++i)
        {
            const uintptr_t addr = boneArr + stride * (uintptr_t)i + posOff;
            if (!mem.IsValidRange(addr, 0x20))
                break;
            BoneRaw b;
            b.index = i;
            mem.ReadRaw(addr, b.p0, sizeof(b.p0));
            mem.ReadRaw(addr + 0x10, b.p1, sizeof(b.p1));
            mem.ReadRaw(addr, b.quat, sizeof(b.quat));
            const float dx = b.p0[0] - origin[0];
            const float dy = b.p0[1] - origin[1];
            b.dz = b.p0[2] - origin[2];
            b.dh = sqrtf(dx * dx + dy * dy);
            b.side = dx * rx + dy * ry;
            b.fwd = dx * fx + dy * fy;
            table.push_back(b);
            samples.push_back(BoneSample{ i, BonePoint{ b.p0[0], b.p0[1], b.p0[2] } });
            fprintf(fp, "%-4d %-10.2f %-10.2f %-10.2f %-9.2f %-8.2f %-8.2f %-8.2f %-10.2f\n",
                    i, b.p0[0], b.p0[1], b.p0[2], b.dz, b.dh, b.side, b.fwd, b.p1[2]);
        }

        // 分类器结果
        int jointIndex[Bone_Count] = { 0 };
        BoneClassifyReport rep;
        const bool ok = ClassifyBones(samples.data(), (int)samples.size(),
                                      BonePoint{ origin[0], origin[1], origin[2] }, eye[1], jointIndex, &rep);
        fprintf(fp, "\n-- ClassifyBones: %s  (%s)  左右轴=%.1f 度  对称面误差=%.2f cm  头高=%.1f\n", ok ? "OK" : "FAIL",
                rep.reason, rep.axisDeg, rep.axisCost, rep.headDz);
        // 打印时用分类器自己拟合出来的左右轴，才能和它的左右判定对得上
        const float repRad = rep.axisDeg * 3.14159265358979323846f / 180.0f;
        const float axF = cosf(repRad), ayF = sinf(repRad);
        fprintf(fp, "%-14s %-5s %-9s %-8s %-8s %-8s\n", "关节", "idx", "dz", "dh", "lat", "fwd");
        for (int j = 0; j < Bone_Count; ++j)
        {
            const int id = jointIndex[j];
            const BoneRaw* br = nullptr;
            for (const BoneRaw& b : table)
            {
                if (b.index == id)
                {
                    br = &b;
                    break;
                }
            }
            if (br == nullptr)
            {
                fprintf(fp, "%-14s %-5d %-9s %-8s %-8s %-8s\n", BoneJointName(j), id, "-", "-", "-", "-");
                continue;
            }
            const float dx = br->p0[0] - origin[0];
            const float dy = br->p0[1] - origin[1];
            fprintf(fp, "%-14s %-5d %-9.2f %-8.2f %-8.2f %-8.2f\n", BoneJointName(j), id, br->dz, br->dh,
                    dx * axF + dy * ayF, dx * (-ayF) + dy * axF);
        }
        // 与配置里的固定索引对比
        fprintf(fp, "-- 对比配置索引: head %d/%llu  neck %d/%llu  chest %d/%llu  pelvis %d/%llu  footL %d/%llu  footR %d/%llu\n",
                jointIndex[Bone_Head], (unsigned long long)Off("kBoneIdxHead"),
                jointIndex[Bone_Neck], (unsigned long long)Off("kBoneIdxNeck"),
                jointIndex[Bone_Chest], (unsigned long long)Off("kBoneIdxChest"),
                jointIndex[Bone_Pelvis], (unsigned long long)Off("kBoneIdxPelvis"),
                jointIndex[Bone_FootL], (unsigned long long)Off("kBoneIdxLeftFoot"),
                jointIndex[Bone_FootR], (unsigned long long)Off("kBoneIdxRightFoot"));

        // 连续采样：用来判定"身体朝向"到底该用哪根轴
        //（眼睛朝向可以甩开身体；两只脚一前一后时，脚连线方向也不一定是左右轴）
        if (args.watch > 0 && found == 1)
        {
            fprintf(fp, "\n-- 连续采样 %d 次（每 150ms 一次）：每次给出原点、眼睛朝向、以及全部骨骼世界坐标 --\n", args.watch);
            for (int s = 0; s < args.watch; ++s)
            {
                Sleep(150);
                float o[3] = { 0, 0, 0 };
                float eye2[3] = { 0, 0, 0 };
                mem.ReadRaw(originAddr, o, sizeof(o));
                mem.ReadRaw(ent + Off("m_angEyeAngles"), eye2, sizeof(eye2));
                fprintf(fp, "[watch %d] origin=(%.2f, %.2f, %.2f) eye=(%.2f, %.2f, %.2f)\n",
                        s, o[0], o[1], o[2], eye2[0], eye2[1], eye2[2]);
                for (int i = 0; i < boneCount; ++i)
                {
                    float p[3] = { 0, 0, 0 };
                    if (!mem.ReadRaw(boneArr + stride * (uintptr_t)i + posOff, p, sizeof(p)))
                        break;
                    fprintf(fp, "  b%-3d %.2f %.2f %.2f\n", i, p[0], p[1], p[2]);
                }
            }
        }
    }

    fclose(fp);
    printf("[+] 找到 %d 个玩家，报告: %s\n", found, args.dump.c_str());
    return found > 0 ? 0 : 1;
}
