#pragma once
#include "features/config.h"
namespace nl {
// ---------------------------------------------------------------------------
//  Misc 行为层：移动辅助 + 会话/视觉类。
//
//  这一层刻意"不依赖 CreateMove 钩子"：
//    * 移动类（bhop / autoStrafe / autoPeek / edgeJump）通过输入模拟按键实现；
//    * 战斗类（triggerbot / autoPistol / knifeBot）同样走鼠标事件；
//    * noFlash 直接写本地的 m_flFlashMaxAlpha（注入版才有写权限）。
//  等 CreateMove 钩子接上之后，这些实现可以逐个替换成"写 cmd"的版本，
//  接口保持不变（MiscTick 由 GameTick 每帧调用一次）。
// ---------------------------------------------------------------------------
void MiscTick(const Settings& s);
// 卸载/关闭时把模拟按键全部松开，避免卡键
void MiscShutdown();
// 调试信息（写进 [misc] 日志行）
struct MiscStatus {
    bool bhopActive = false;
    bool bhopJumping = false;
    int  jumpConfirmCount = 0;     // "按下跳 → 真的离地"的次数（bhop 生效的客观证据）
    int  maxJumpGapMs = 0;     // 连跳期间两次起跳的最大间隔（越小越顺）
    int  jumpGapOver300 = 0;   // 间隔超过 300ms 的次数 = 断链次数
    int  airTransitions = 0;   // 真的起跳成功次数（地面->空中）
    int  landings = 0;         // 落地次数
    int  repressCount = 0;     // 注入周期数
    bool strafeActive = false;
    int  strafeSide = 0;
    bool triggerFiring = false;
    int  triggerFireCount = 0;
    bool autoPistolFiring = false;
    bool noFlashApplied = false;
    bool autoPeekWalking = false;
    bool jumpKeyAsync = false;     // GetAsyncKeyState 读到的原始跳键状态
    bool jumpKeyUser = false;     // 最终判定（= 真人按住、失效安全）
    bool jumpKeyPhys = false;     // 低级钩子报告的真人按住状态      // 判定为「用户真的按着跳键」
    float crosshairDist = 9999.0f;   // 准星到最近可见敌人的像素距离（triggerbot 的诊断）
    int   crosshairCandidate = -1;
    const char* note = "";
};
const MiscStatus& MiscStatusGet();
}

