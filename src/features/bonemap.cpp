#include "features/bonemap.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace nl {
namespace {

struct Cand {
    int   index = -1;
    float dx = 0.0f, dy = 0.0f, dz = 0.0f;   // 相对脚底原点
    float dh = 0.0f;                          // 离原点水平距离
    float lat = 0.0f;                         // 沿"左右轴"的分量（+ = 右侧）
    float fwd = 0.0f;                         // 沿"前后轴"的分量（+ = 身体前方）
};

bool Finite(float v)
{
    return std::isfinite(v);
}

void SetReason(BoneClassifyReport* report, const char* text)
{
    if (report != nullptr)
    {
        snprintf(report->reason, sizeof(report->reason), "%s", text);
    }
}

} // namespace

const char* BoneJointName(int joint)
{
    switch (joint)
    {
    case Bone_Head: return "head";
    case Bone_Neck: return "neck";
    case Bone_Chest: return "chest";
    case Bone_Pelvis: return "pelvis";
    case Bone_ShoulderL: return "shoulderL";
    case Bone_ShoulderR: return "shoulderR";
    case Bone_ElbowL: return "elbowL";
    case Bone_ElbowR: return "elbowR";
    case Bone_HandL: return "handL";
    case Bone_HandR: return "handR";
    case Bone_KneeL: return "kneeL";
    case Bone_KneeR: return "kneeR";
    case Bone_FootL: return "footL";
    case Bone_FootR: return "footR";
    default: return "?";
    }
}

bool BoneJointIsSpine(int joint)
{
    return joint >= Bone_Head && joint <= Bone_Pelvis;
}

// ---------------------------------------------------------------------------
//  ClassifyBones —— 从一张骨骼表里认出人形关节
//
//  关键点（都是实测踩过的坑）：
//   1. 玩家实体的原点在"脚底"，但身体可以前倾/奔跑，此时头和胸口都会离开中轴，
//      所以不能拿世界垂直轴当躯干轴，要用 "两脚中点 -> 头" 这条身体轴。
//   2. 也不能用 m_angEyeAngles 当身体朝向：眼睛可以看向别处，身体朝向由
//      "左脚 -> 右脚" 这条左右轴决定（本函数自己从骨骼里算，不依赖外部朝向）。
//   3. 左右肢必须成对取，并且要求左右对称（同高度、同距离），
//      否则很容易把背包/背心/手指骨当成肩或手，画出一条横穿屏幕的斜线。
//   4. 离身体太远（dh>60）的骨一律丢掉：CS2 骨骼表里存在未初始化的垃圾项
//      （实测有 y=988 这种值），这正是旧版画出"飞到屏幕角落的长线"的原因。
// ---------------------------------------------------------------------------
bool ClassifyBones(const BoneSample* bones, int count, const BonePoint& origin, float yawDeg,
                   int jointIndex[Bone_Count], BoneClassifyReport* report)
{
    if (report != nullptr)
    {
        *report = BoneClassifyReport();
    }
    if (jointIndex == nullptr || bones == nullptr || count <= 0)
    {
        SetReason(report, "没有骨骼数据");
        return false;
    }
    for (int j = 0; j < Bone_Count; ++j)
    {
        jointIndex[j] = -1;
    }

    const float yawRad = yawDeg * 3.14159265358979323846f / 180.0f;
    float ax = sinf(yawRad), ay = -cosf(yawRad);   // 备用左右轴：眼睛朝向的右手方向
    float bx = cosf(yawRad), by = sinf(yawRad);    // 备用前后轴：眼睛朝向

    std::vector<Cand> c;
    c.reserve((size_t)count);
    for (int i = 0; i < count; ++i)
    {
        const BonePoint& p = bones[i].pos;
        if (!Finite(p.x) || !Finite(p.y) || !Finite(p.z))
            continue;
        const float dx = p.x - origin.x;
        const float dy = p.y - origin.y;
        const float dz = p.z - origin.z;
        if (dz < -12.0f || dz > 95.0f)      // 站立身高 72，留出跳跃/蹲下的余量
            continue;
        const float dh = sqrtf(dx * dx + dy * dy);
        if (dh > 60.0f)                     // 离身体太远：垃圾骨 / 别人的骨骼
            continue;
        Cand cand;
        cand.index = bones[i].index;
        cand.dx = dx;
        cand.dy = dy;
        cand.dz = dz;
        cand.dh = dh;
        cand.lat = dx * ax + dy * ay;
        cand.fwd = dx * bx + dy * by;
        c.push_back(cand);
    }
    if (report != nullptr)
    {
        report->usableBones = (int)c.size();
    }
    if (c.size() < 4)
    {
        SetReason(report, "可用骨骼太少（坐标不合法或离身体太远）");
        return false;
    }

    auto byIndex = [&](int idx) -> const Cand*
    {
        if (idx < 0)
            return nullptr;
        for (const Cand& b : c)
        {
            if (b.index == idx)
                return &b;
        }
        return nullptr;
    };
    auto dzOf = [&](int idx) -> float
    {
        const Cand* b = byIndex(idx);
        return (b != nullptr) ? b->dz : -1000.0f;
    };
    auto latOf = [&](int idx) -> float
    {
        const Cand* b = byIndex(idx);
        return (b != nullptr) ? b->lat : 0.0f;
    };

    // ---- 1. 贴地的一对脚骨：给出脚踝高度（左右轴用第 4 步的对称面拟合）----
    int footA = -1, footB = -1;
    float footSep = 0.0f;
    for (const Cand& p : c)
    {
        if (p.dz > 16.0f || p.dz < -10.0f || p.dh < 1.0f || p.dh > 28.0f)
            continue;
        for (const Cand& q : c)
        {
            if (q.index <= p.index)
                continue;
            if (q.dz > 16.0f || q.dz < -10.0f || q.dh < 1.0f || q.dh > 28.0f)
                continue;
            const float sep = sqrtf((p.dx - q.dx) * (p.dx - q.dx) + (p.dy - q.dy) * (p.dy - q.dy));
            if (sep > footSep && sep < 60.0f)
            {
                footSep = sep;
                footA = p.index;
                footB = q.index;
            }
        }
    }

    // ---- 2. 头：贴近身体中轴的最高骨 ----
    //  实测踩坑：把中轴半径放宽到 15 时，8 个不同模型全部选中了"头顶高度、但在身前
    //  约 30cm"的那根骨（举枪的手/帽子挂件），方框就会整体偏到枪上、从侧面看明显脱框。
    //  收紧到 6 之后，8 个模型一致落在真正的中轴头骨上（dz≈63~64、dh≈4.3~5.5）。
    //  仍然保留逐级放宽：万一某个模型头骨就是偏轴的，也不至于找不到。
    int head = -1;
    float headDz = -1.0f;
    const float headLimits[3] = { 6.0f, 10.0f, 15.0f };
    for (int pass = 0; pass < 3 && head < 0; ++pass)
    {
        for (const Cand& b : c)
        {
            if (b.dh > headLimits[pass] || b.dz < 40.0f || b.dz > 88.0f)
                continue;
            if (b.dz > headDz)
            {
                headDz = b.dz;
                head = b.index;
            }
        }
    }
    if (head < 0)
    {
        SetReason(report, "找不到头骨骼（中轴附近 40~88cm 之间没有骨骼）");
        return false;
    }
    if (report != nullptr)
    {
        report->headDz = headDz;
    }

    // ---- 3. 躯干：实体原点（脚底）上方、贴着身体中轴、并且一节接一节的骨 ----
    //  说明：不能拿"两脚中点 -> 头"这条直线当中轴——行进时两只脚一前一后，
    //  中点会被拉到身体侧面，实测会把小腿骨当成骨盆。这里用"离实体原点水平距离"
    //  作为中轴判据（原点就在身体正下方），再按头高的比例选高度带；
    //  另外加上"离上一节脊柱的水平距离"惩罚，避免胸口/骨盆左右横跳画出折线。
    auto pickSpine = [&](float targetDz, float tol, float maxDh, float belowDz, const Cand* prev,
                         float minDz = -1e9f) -> int
    {
        int best = -1;
        float bestScore = 1e9f;
        float bestHeight = 1e9f;
        for (const Cand& b : c)
        {
            if (b.dh > maxDh || b.dz >= belowDz || b.dz <= minDz)
                continue;
            const float height = fabsf(b.dz - targetDz);
            float score = height + 0.30f * b.dh;
            if (prev != nullptr)
            {
                const float dx = b.dx - prev->dx;
                const float dy = b.dy - prev->dy;
                score += 0.45f * sqrtf(dx * dx + dy * dy);
            }
            if (score < bestScore)
            {
                bestScore = score;
                bestHeight = height;
                best = b.index;
            }
        }
        return (bestHeight <= tol) ? best : -1;
    };
    const Cand* headCand = byIndex(head);
    const int neck = pickSpine(headDz * 0.90f, headDz * 0.12f, 8.0f, headDz - 2.0f, headCand);
    const Cand* neckCand = byIndex(neck);
    float neckDz = (neckCand != nullptr) ? neckCand->dz : headDz;
    // 用脖子校正一次头：头必须在脖子**正上方**（水平不超过 6cm），而不是身前那根举枪/挂件骨。
    // 这是"从侧面看脱框"的直接修法：只有当粗选出来的头离脖子太远时才替换。
    int headFixed = head;
    float headFixedDz = headDz;
    if (neckCand != nullptr)
    {
        int bestIdx = -1;
        float bestDz = -1.0f;
        for (const Cand& b : c)
        {
            if (b.index == neck || b.index == head)
                continue;
            if (b.dz <= neckDz + 1.0f || b.dz > neckDz + 22.0f)
                continue;
            const float dx = b.dx - neckCand->dx;
            const float dy = b.dy - neckCand->dy;
            if (sqrtf(dx * dx + dy * dy) > 6.0f)
                continue;
            if (b.dz > bestDz)
            {
                bestDz = b.dz;
                bestIdx = b.index;
            }
        }
        // 粗选的头离脖子太远（>8cm 水平）而脖子正上方有骨 → 换成脖子正上方那根
        const float pickedDx = headCand != nullptr ? headCand->dx - neckCand->dx : 0.0f;
        const float pickedDy = headCand != nullptr ? headCand->dy - neckCand->dy : 0.0f;
        if (bestIdx >= 0 && sqrtf(pickedDx * pickedDx + pickedDy * pickedDy) > 8.0f)
        {
            headFixed = bestIdx;
            headFixedDz = bestDz;
        }
    }
    const int headFinal = headFixed;
    const float headDzFinal = headFixedDz;
    if (report != nullptr) { report->headDz = headDzFinal; }
    // 顺序改成 脖子 -> 骨盆 -> 胸：
    //   骨盆先定下来，胸才有"必须夹在骨盆和脖子中间、且在中点之上"的约束 ——
    //   CS2 骨骼表里有一根固定在 origin+(0,0,40) 的静止骨（实测不随姿势动），
    //   不加这个约束它会被当成胸口。
    // 盆骨必须**贴中轴**：CS2 骨架里左右胯骨在轴外约 8~10cm，用和脖子一样的 8 容差会选到胯骨
    // （用户反馈"盆骨点没居中"就是它）。先按 3.5 找；实在没有（模型异常）才放宽到 6。
    int pelvis = pickSpine(headDzFinal * 0.53f, headDzFinal * 0.16f, 3.5f, neckDz - 6.0f, neckCand);
    if (pelvis < 0)
        pelvis = pickSpine(headDzFinal * 0.53f, headDzFinal * 0.16f, 6.0f, neckDz - 6.0f, neckCand);
    const float pelvisDz = (pelvis >= 0) ? dzOf(pelvis) : headDzFinal * 0.53f;
    const Cand* pelvisCand = byIndex(pelvis);
    const int chest = pickSpine(headDzFinal * 0.72f, headDzFinal * 0.16f, 8.0f, neckDz - 1.0f, neckCand,
                                (pelvisDz + neckDz) * 0.5f);
    const Cand* chestCand = byIndex(chest);
    const float chestDz = (chestCand != nullptr) ? chestCand->dz : neckDz;
    (void)pelvisCand;

    // ---- 3.5 左右轴：用"人形骨架左右对称"这个事实拟合出矢状面法线 ----
    //  为什么不能直接用两脚连线：行进时两只脚一前一后，脚连线其实是前后方向；
    //  也不能完全相信 m_angEyeAngles：走路/侧身时身体朝向可以甩开视线几十度。
    //  实测（NLBoneProbe --watch 的对称面拟合）：站姿转身的玩家，拟合结果与
    //  "眼睛朝向的左右轴"一致，而脚连线差了 120 度，所以以拟合为准、视轴兜底。
    {
        struct SymPt {
            float dx, dy, dz, dh;
        };
        std::vector<SymPt> pts;
        pts.reserve(c.size());
        for (const Cand& b : c)
        {
            if (b.dh < 2.0f || b.dh > 40.0f || b.dz < 4.0f || b.dz > 80.0f)
                continue;
            pts.push_back(SymPt{ b.dx, b.dy, b.dz, b.dh });
        }
        if (pts.size() >= 8)
        {
            auto costAt = [&](float ux, float uy) -> float
            {
                float cost = 0.0f, wsum = 0.0f;
                for (const SymPt& p : pts)
                {
                    const float dot = p.dx * ux + p.dy * uy;
                    const float mx = p.dx - 2.0f * dot * ux;
                    const float my = p.dy - 2.0f * dot * uy;
                    float best = 12.0f;
                    for (const SymPt& q : pts)
                    {
                        if (&q == &p)
                            continue;
                        if (fabsf(q.dz - p.dz) > 5.0f)
                            continue;
                        const float d = sqrtf((mx - q.dx) * (mx - q.dx) + (my - q.dy) * (my - q.dy));
                        if (d < best)
                            best = d;
                    }
                    const float w = (p.dh < 20.0f) ? p.dh : 20.0f;
                    cost += w * best;
                    wsum += w;
                }
                return (wsum > 0.0f) ? cost / wsum : 1e9f;
            };
            float bestCost = 1e9f;
            float bestAng = atan2f(ay, ax);
            for (int deg = 0; deg < 180; deg += 2)
            {
                const float r = (float)deg * 3.14159265358979323846f / 180.0f;
                const float cost = costAt(cosf(r), sinf(r));
                if (cost < bestCost)
                {
                    bestCost = cost;
                    bestAng = r;
                }
            }
            for (int step = -4; step <= 4; ++step)
            {
                const float r = bestAng + (float)step * 0.25f * 3.14159265358979323846f / 180.0f;
                const float cost = costAt(cosf(r), sinf(r));
                if (cost < bestCost)
                {
                    bestCost = cost;
                    bestAng = r;
                }
            }
            // 拟合质量太差（平均镜像误差 > 6cm）时不要它，退回视轴
            if (bestCost < 6.0f)
            {
                ax = cosf(bestAng);
                ay = sinf(bestAng);
            }
            if (report != nullptr)
            {
                report->axisCost = bestCost;
            }
        }
        // 拟合出来的只是一条"平面法线"，正负号无意义；用视线左右轴定一下符号，
        // 这样左右脚的标签才和"玩家自己的左右"一致（身体朝向一般不会和视线差 90 度以上）
        if (ax * sinf(yawRad) + ay * (-cosf(yawRad)) < 0.0f)
        {
            ax = -ax;
            ay = -ay;
        }
        bx = -ay;
        by = ax;
        for (Cand& bone : c)
        {
            bone.lat = bone.dx * ax + bone.dy * ay;
            bone.fwd = bone.dx * bx + bone.dy * by;
        }
        if (report != nullptr)
        {
            report->axisDeg = atan2f(ay, ax) * 180.0f / 3.14159265358979323846f;
        }
    }

    // ---- 4. 肩：成对、左右对称、尽量靠外 ----
    struct LimbPair {
        int a = -1;
        int b = -1;
        float score = -1e9f;
    };
    auto pickSymmetricPair = [&](float dzLo, float dzHi, float minLat, float maxLat,
                                 float maxDzDiff, float maxLatDiff) -> LimbPair
    {
        LimbPair best;
        for (const Cand& p : c)
        {
            const float lp = fabsf(p.lat);
            if (p.dz < dzLo || p.dz > dzHi || lp < minLat || lp > maxLat)
                continue;
            for (const Cand& q : c)
            {
                if (q.index <= p.index)
                    continue;
                const float lq = fabsf(q.lat);
                if (q.dz < dzLo || q.dz > dzHi || lq < minLat || lq > maxLat)
                    continue;
                if ((p.lat > 0.0f) == (q.lat > 0.0f))
                    continue;                        // 必须一左一右
                const float dzDiff = fabsf(p.dz - q.dz);
                const float latDiff = fabsf(lp - lq);
                if (dzDiff > maxDzDiff || latDiff > maxLatDiff)
                    continue;                        // 左右不对称的一对不要
                const float midX = (p.dx + q.dx) * 0.5f;
                const float midY = (p.dy + q.dy) * 0.5f;
                const float mid = sqrtf(midX * midX + midY * midY);
                const float fwdAvg = (fabsf(p.fwd) + fabsf(q.fwd)) * 0.5f;
                // 肩/胯应该在身体的"冠状面"上：越靠前/靠后（背包、武器）扣分越多
                const float score = (lp + lq) * 0.5f - 1.6f * (dzDiff + latDiff) -
                                    1.2f * fwdAvg - 1.0f * mid;
                if (score > best.score)
                {
                    best.score = score;
                    best.a = p.index;
                    best.b = q.index;
                }
            }
        }
        return best;
    };
    // 先按严格的对称要求找；找不到再放宽一次（宁可略微不对称，也别整个手臂不画）
    LimbPair shoulders = pickSymmetricPair(headDzFinal * 0.78f, headDzFinal * 1.02f, 4.0f, 24.0f, 4.5f, 5.0f);
    if (shoulders.a < 0)
        shoulders = pickSymmetricPair(headDzFinal * 0.74f, headDzFinal * 1.06f, 3.0f, 26.0f, 8.0f, 9.0f);
    int shoulderR = -1, shoulderL = -1;
    if (shoulders.a >= 0)
    {
        shoulderR = (latOf(shoulders.a) > 0.0f) ? shoulders.a : shoulders.b;
        shoulderL = (shoulderR == shoulders.a) ? shoulders.b : shoulders.a;
    }

    // ---- 5. 手：离身体中轴最远的那根（握枪时两只手都在身前，不强行左右对称）----
    auto pickHand = [&](int shoulder, int sideSign) -> int
    {
        const Cand* sh = byIndex(shoulder);
        const float minDh = (sh != nullptr) ? (sh->dh + 2.0f) : 9.0f;
        int best = -1;
        float bestScore = -1e9f;
        for (const Cand& b : c)
        {
            if (b.dz < headDzFinal * 0.42f || b.dz > headDzFinal * 0.94f)
                continue;
            if (b.dh < minDh || b.dh > 40.0f)
                continue;
            if (b.lat * (float)sideSign < -2.0f)
                continue;   // 不能跑到身体另外一侧去
            if (sh != nullptr)
            {
                // 手必须离肩够远（上臂 + 前臂），否则多半是背心/装备上的骨
                const float sx = b.dx - sh->dx, sy = b.dy - sh->dy, sz = b.dz - sh->dz;
                if (sqrtf(sx * sx + sy * sy + sz * sz) < headDzFinal * 0.15f)
                    continue;
            }
            // 取离身体轴最远的骨（手腕/手指末端），并偏好接近 0.68 倍头高的高度
            const float score = b.dh - 0.25f * fabsf(b.dz - headDzFinal * 0.68f);
            if (score > bestScore)
            {
                bestScore = score;
                best = b.index;
            }
        }
        return best;
    };
    int handR = (shoulderR >= 0) ? pickHand(shoulderR, 1) : -1;
    int handL = (shoulderL >= 0) ? pickHand(shoulderL, -1) : -1;

    // ---- 6. 肘：肩-手中点附近，并且要在两者之间 ----
    auto pickBetween = [&](int from, int to) -> int
    {
        const Cand* a = byIndex(from);
        const Cand* b = byIndex(to);
        if (a == nullptr || b == nullptr)
            return -1;
        const float mx = (a->dx + b->dx) * 0.5f;
        const float my = (a->dy + b->dy) * 0.5f;
        const float mz = (a->dz + b->dz) * 0.5f;
        const float lo = (a->dz < b->dz) ? a->dz : b->dz;
        const float hi = (a->dz > b->dz) ? a->dz : b->dz;
        int best = -1;
        float bestD = 14.0f;
        for (const Cand& k : c)
        {
            if (k.index == a->index || k.index == b->index)
                continue;
            if (k.dz < lo - 2.0f || k.dz > hi + 2.0f)
                continue;
            const float d = sqrtf((k.dx - mx) * (k.dx - mx) + (k.dy - my) * (k.dy - my) + (k.dz - mz) * (k.dz - mz));
            if (d < bestD)
            {
                bestD = d;
                best = k.index;
            }
        }
        return best;
    };

    // ---- 7. 膝：同侧脚与骨盆之间、靠近 0.30 倍头高 ----
    int kneeR = -1, kneeL = -1;
    if (footA >= 0 && footB >= 0)
    {
        const Cand* fa = byIndex(footA);
        const Cand* fb = byIndex(footB);
        auto pickKnee = [&](const Cand* foot, const Cand* otherFoot, int sideSign) -> int
        {
            if (foot == nullptr)
                return -1;
            int best = -1;
            float bestD = 1e9f;
            for (const Cand& b : c)
            {
                if (b.index == foot->index)
                    continue;
                if (b.dh < 1.5f || b.dh > 26.0f)
                    continue;
                if (b.dz < foot->dz + 6.0f || b.dz > pelvisDz + 3.0f)
                    continue;
                // 归属这条腿：离这只脚明显比离另一只脚近
                const float da = sqrtf((b.dx - foot->dx) * (b.dx - foot->dx) +
                                       (b.dy - foot->dy) * (b.dy - foot->dy));
                if (otherFoot != nullptr)
                {
                    const float db = sqrtf((b.dx - otherFoot->dx) * (b.dx - otherFoot->dx) +
                                           (b.dy - otherFoot->dy) * (b.dy - otherFoot->dy));
                    if (da > db)
                        continue;
                }
                else if ((b.lat > 0.0f) != (sideSign > 0))
                {
                    continue;
                }
                const float d = fabsf(b.dz - headDzFinal * 0.31f);
                if (d < bestD)
                {
                    bestD = d;
                    best = b.index;
                }
            }
            return best;
        };
        {
            const Cand* rightFoot = (latOf(footA) > 0.0f) ? fa : fb;
            const Cand* leftFoot = (rightFoot == fa) ? fb : fa;
            kneeR = pickKnee(rightFoot, leftFoot, 1);
            kneeL = pickKnee(leftFoot, rightFoot, -1);
            jointIndex[Bone_FootR] = (rightFoot != nullptr) ? rightFoot->index : -1;
            jointIndex[Bone_FootL] = (leftFoot != nullptr) ? leftFoot->index : -1;
        }
    }

    jointIndex[Bone_Head] = headFinal;
    jointIndex[Bone_Neck] = neck;
    jointIndex[Bone_Chest] = chest;
    jointIndex[Bone_Pelvis] = pelvis;
    jointIndex[Bone_ShoulderR] = shoulderR;
    jointIndex[Bone_ShoulderL] = shoulderL;
    jointIndex[Bone_ElbowR] = pickBetween(shoulderR, handR);
    jointIndex[Bone_ElbowL] = pickBetween(shoulderL, handL);
    jointIndex[Bone_HandR] = handR;
    jointIndex[Bone_HandL] = handL;
    jointIndex[Bone_KneeR] = kneeR;
    jointIndex[Bone_KneeL] = kneeL;

    // ---- 8. 结果校验 ----
    int found = 0;
    for (int j = 0; j < Bone_Count; ++j)
    {
        if (jointIndex[j] >= 0)
            ++found;
    }
    if (pelvis < 0 || neck < 0 || chest < 0)
    {
        SetReason(report, "躯干中轴骨找不到（骨盆 / 胸 / 脖子缺失）");
        return false;
    }
    if (footA < 0)
    {
        SetReason(report, "找不到贴地的一对脚骨");
        return false;
    }
    if (found < 8)
    {
        SetReason(report, "关节数量不足（<8），骨架不可信");
        return false;
    }
    if (!(dzOf(pelvis) < dzOf(chest) && dzOf(chest) < dzOf(neck) && dzOf(neck) < dzOf(headFinal)))
    {
        SetReason(report, "躯干高度顺序异常（骨盆 < 胸 < 脖 < 头 不成立）");
        return false;
    }
    if (shoulderR >= 0 && shoulderL >= 0)
    {
        const float shoulderMid = (dzOf(shoulderR) + dzOf(shoulderL)) * 0.5f;
        if (shoulderMid < dzOf(chest) - 4.0f || shoulderMid > dzOf(neck) + 8.0f)
        {
            SetReason(report, "肩的高度不在胸与脖子之间");
            return false;
        }
    }
    if (report != nullptr)
    {
        for (int j = 0; j < Bone_Count; ++j)
        {
            if (jointIndex[j] < 0)
                continue;
            const Cand* b = byIndex(jointIndex[j]);
            if (b == nullptr)
                continue;
            float& slot = BoneJointIsSpine(j) ? report->maxSpineDeviation : report->maxLimbDeviation;
            if (b->dh > slot)
                slot = b->dh;
        }
    }
    SetReason(report, "ok");
    return true;
}

} // namespace nl

