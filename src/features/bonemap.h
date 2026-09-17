#pragma once
// ---------------------------------------------------------------------------
//  骨骼关节契约 + 骨骼索引分类器
//
//  背景：不同 agent / 模型的骨骼数组长度和顺序都不一样，写死索引在换模型后
//  就会画出一团乱线。这里把"索引 -> 关节"的推导集中成一个纯函数
//  ClassifyBones()：只依赖骨骼世界坐标 + 脚底原点 + 玩家朝向，
//  不碰内存、不碰 UI，因此可以离线用合成骨骼表做回归（NLBoneProbe --selftest）。
// ---------------------------------------------------------------------------
#include <cstdint>

namespace nl {

enum BoneId {
    Bone_Head = 0,
    Bone_Neck,
    Bone_Chest,
    Bone_Pelvis,        // 以上 4 个是躯干
    Bone_ShoulderL, Bone_ShoulderR,
    Bone_ElbowL, Bone_ElbowR,
    Bone_HandL, Bone_HandR,
    Bone_KneeL, Bone_KneeR,
    Bone_FootL, Bone_FootR,
    Bone_Count
};

// 旧名字（脚踝关节）保留，避免旧代码编译不过
constexpr int Bone_LFoot = Bone_FootL;
constexpr int Bone_RFoot = Bone_FootR;

struct BonePoint {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct BoneSample {
    int       index = -1;      // 骨骼数组下标
    BonePoint pos;             // 世界坐标
};

struct BoneClassifyReport {
    char  reason[192] = { 0 };
    int   usableBones = 0;         // 参与分类的骨骼数量
    float headDz = 0.0f;           // 推定头顶高度（相对脚底，单位同游戏）
    float axisDeg = 0.0f;          // 拟合出来的左右轴角度（度，世界 XY 平面）
    float axisCost = 0.0f;         // 对称面拟合的平均镜像误差（cm），越小越可信
    float maxSpineDeviation = 0.0f; // 躯干关节离中轴的最大水平距离
    float maxLimbDeviation = 0.0f;  // 四肢关节离中轴的最大水平距离
};

const char* BoneJointName(int joint);
bool        BoneJointIsSpine(int joint);

// 从骨骼表里推导关节索引；成功返回 true（jointIndex 全部填好，推不出的关节为 -1）
//   origin : 玩家脚底世界坐标（实体原点）
//   yawDeg : 玩家朝向（度，CS 约定：0 面向 +X，右手方向 = (sin yaw, -cos yaw)）
bool ClassifyBones(const BoneSample* bones, int count, const BonePoint& origin, float yawDeg,
                   int jointIndex[Bone_Count], BoneClassifyReport* report = nullptr);

} // namespace nl
