#pragma once
namespace nl {
// ---------------------------------------------------------------------------
//  Anti-Aim / 静默角度
//
//  原理（参考 TKazer 的 CS2 外部静默分析）：命令结构里的 pitch/yaw 在 `+0x18` / `+0x1C`，
//  是 CreateMove 内部写进去的。所以：
//    * 在 CreateMove 钩子里**先调用原函数**（让它把角度填好）；
//    * 返回前再把我们要的 pitch/yaw 覆盖进命令 —— 这一份是"发给服务器的朝向"；
//    * 至此**镜头（客户端相机）不受影响** → 真静默。
//
//  当前实现的能力：
//    - 俯仰：关闭 / 朝上(±89) / 朝下
//    - 偏航：原视角 / 背面(+180) / 自定义偏移 / 抖动（每 tick 交替）
//  后续 aimbot 的静默打点也复用这条路径（把角度改成瞄向目标的解算结果）。
// ---------------------------------------------------------------------------
struct AntiAimStats {
    bool  applied = false;
    int   writes = 0;
    int   eyeWrites = 0;      // 写 m_angEyeAngles 的次数（别人看到的朝向）
    float lastPitch = 0.0f;
    float lastYaw = 0.0f;
};
// 在 CreateMove 里（原函数返回后）调用；viewAngles 传当前视角 {pitch, yaw, roll}
void AntiAimApplyToCmd(void* cmd, const float* viewAngles);
const AntiAimStats& AntiAimStatsGet();
// 调试：把每次写入打到日志（每秒最多一行）
void AntiAimSetLog(bool on);
}

