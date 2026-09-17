#pragma once
#include "features/config.h"
#include "features/esp.h"
#include <string>
#include <vector>

// 游戏数据层：把内存里的实体翻译成 PlayerView，绘制完全交给 esp.cpp
namespace nl {

struct GameStatus {
    bool        attached = false;
    bool        ready = false;
    std::string process = "—";
    std::string moduleName = "client.dll";
    unsigned long pid = 0;
    uintptr_t   moduleBase = 0;
    size_t      moduleSize = 0;
    uintptr_t   localPawn = 0;
    uintptr_t   localController = 0;
    int         entityScanned = 0;
    int         playersFound = 0;
    int         boneHits = 0;
    float       tickMs = 0.0f;
    std::string message = "未连接";
};

bool GameAttach(const char* processName, const char* moduleName);
void GameDetach();
void GameTick(const Settings& s);
void GameDrawEsp(const Settings& s, ImDrawList* dl);
const GameStatus& GameStatusGet();
// client.dll 基址（CreateMove 钩子等需要它换算对象地址）
uintptr_t GameModuleBase();
// 取偏移槽位当前值（等价于 offsets.ini 里那一项）
uintptr_t Off(const char* name);
const std::vector<PlayerView>& GamePlayerList();
// 偏移校验：逐项做合理性检查，报告里写明每一项的实际值与可疑原因
int  GameVerifyOffsets(std::string* report);
// 骨骼自动标定：扫描骨骼数组，自动找出 头/颈/胸/骨盆/脚 的索引并写回偏移表
int  GameCalibrateBones(std::string* report);
// 实体列表自动标定：用已知的 controller/pawn 指针反推正确的块表结构（版本变了也能一键修）
int  GameCalibrateEntityList(std::string* report);
// 用已知的 pawn/controller 指针反推 m_hController / m_hPlayerPawn 句柄偏移
int  GameCalibrateHandleOffsets(std::string* report);
// 用多个控制器的名字字符串反推 m_iszPlayerName 偏移
int  GameCalibrateNameOffset(std::string* report);
// 校验并把报告写进启动日志（EXE 与注入版共用）
int  GameVerifyAndLog();
// 槽位校验状态：0=未检查 1=通过 2=异常 3=缺失
int  GameSlotState(int slotIndex);
bool GameAttachOnStartup(const Settings& s);
// 本地 pawn 的模拟时间（当服务器时间用，算炸弹倒计时）
float GameLocalSimulationTime();
// 调试：把某个实体的玩家字段原始值写进日志（排查"连上了但没画出来"）
void GameLogEntityFields(uintptr_t entity, int index);
// 实体索引上限（读实体系统里的 highestEntityIndex，玩家索引常远超 64）
int  EntityIndexLimit(const class Memory& mem, uintptr_t base);
uintptr_t EntityFromIndex(const class Memory& mem, uintptr_t base, int index);

// 世界坐标 -> 屏幕坐标（行主序 4x4 视矩阵）
bool WorldToScreen(const float matrix[16], const float world[3], ImVec2* out);

// GameTick 各段耗时（毫秒，按 120 帧窗口平均）：找实体 / 读实体 / 读骨骼 / 其它
struct GameTickPhases {
    double scanMs = 0.0;
    double entityMs = 0.0;
    double boneMs = 0.0;
    double otherMs = 0.0;
    double scanMaxMs = 0.0;
    double entityMaxMs = 0.0;
    double boneMaxMs = 0.0;
    double otherMaxMs = 0.0;
    double preMs = 0.0;        // 函数入口 -> 找实体之前（视矩阵/本地玩家）
    double preMaxMs = 0.0;
    double postMs = 0.0;       // 躯干之后每个玩家的零碎读取
    double postMaxMs = 0.0;
    double bombMs = 0.0;       // C4 / 炸弹计时
    double bombMaxMs = 0.0;
    double matrixErrAvg = 0.0; // "用上一帧矩阵重投影"与当前投影的平均/最大像素差
    double matrixErrMax = 0.0;
    int    frames = 0;
};
void GameTickPhasesGet(GameTickPhases* out);
void GameTickPhasesAdd(int which, double ms);
void GameTickPhasesFrame();

// 上一帧用过的视矩阵（排查"转身时方框脱离人物"：拿它重投影画第二个框做对比）
const float* GamePreviousViewMatrix();

// 本地玩家状态：Misc（bhop/strafe/trigger/autoPeek）与后续 aimbot 都从这里取数据
struct LocalState {
    bool  valid = false;
    uintptr_t pawn = 0;
    float origin[3] = { 0.0f, 0.0f, 0.0f };
    float velocity[3] = { 0.0f, 0.0f, 0.0f };
    float viewAngles[3] = { 0.0f, 0.0f, 0.0f };   // dwViewAngles
    int   flags = 0;                              // m_fFlags（&1 = 站在地面）
    int   health = 0;
    int   team = 0;
    int   weaponId = 0;                           // m_iItemDefinitionIndex
    bool  scoped = false;
    bool  onGround = false;
    float speed2D = 0.0f;
    float eyeZ = 0.0f;                            // 眼睛高度（origin.z + viewOffset）
    uintptr_t flashAlphaAddr = 0;                 // m_flFlashMaxAlpha 地址（noFlash 用）
};
const LocalState& GameLocalState();
}
