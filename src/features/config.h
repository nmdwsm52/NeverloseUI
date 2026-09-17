#pragma once
#include <imgui.h>
#include <string>
namespace nl {
struct MenuConfig {
    int   activeTab  = 0;
    int   preset     = 0;
    float accent[4]  = { 0.651f, 0.478f, 1.000f, 1.0f };
    float accent2[4] = { 0.373f, 0.545f, 1.000f, 1.0f };
    float uiScale    = 1.00f;
    bool  watermark  = true;
    bool  blur       = true;
    bool  hints      = true;
    bool  animations = true;
    bool  keyHints   = true;
    bool  autoAttach = false;      // 启动时自动连接目标进程
    float pos[2]     = { 0.0f, 0.0f };
    float size[2]    = { 920.0f, 580.0f };
    bool  hasPos     = false;
    char  search[64] = { 0 };
    char  configName[32] = "default";
    char  targetProcess[64] = "cs2.exe";
    char  targetModule[64] = "client.dll";
};
struct RageConfig {
    bool  enable = false;
    int   key = 0;
    int   hitbox = 1;
    int   priority = 0;
    float fov = 24.0f;
    float smooth = 6.0f;
    float hitchance = 55.0f;
    float mindamage = 100.0f;
    float mindamageOverride = 0.0f;
    float backtrack = 200.0f;
    bool  autowall = true;
    bool  autoScope = true;
    bool  autoStop = true;
    bool  multipoint = false;
    bool  forceSafePoint = false;
    bool  ignoreLimbs = false;
    bool  ignoreInvisible = true;
    bool  preferArmor = false;
    bool  doubleTap = false;
    int   doubleTapKey = 0;
    bool  hideShots = true;
    bool  aaOnShot = false;
    bool  betweenShots = false;
    bool  autoFire = true;
    bool  legitMode = false;
    float triggerDelay = 45.0f;
    float triggerHitchance = 60.0f;
    bool  triggerbot = false;
    bool  backtrackLegit = true;
};
struct AaConfig {
    bool  enable = true;
    int   pitch = 1;
    int   yawBase = 0;
    int   yawAdd = 0;
    float yawJitter = 0.0f;
    int   jitterMode = 0;
    float bodyYaw = 0.0f;
    float desync = 58.0f;
    int   desyncMode = 0;
    bool  fakeLag = true;
    int   fakeLagMode = 0;
    int   fakeLagTicks = 6;
    bool  fakeDuck = false;
    int   fakeDuckKey = 0;
    bool  fakePeek = false;
    int   fakePeekKey = 0;
    bool  airStuck = false;
    int   airStuckKey = 0;
    bool  slowWalk = false;
    int   slowWalkKey = 0;
};
struct VisualConfig {
    bool  enable = true;
    bool  box = true;
    bool  boxOutline = true;
    bool  filled = false;
    bool  name = true;
    bool  health = true;
    bool  healthText = false;
    // 护甲条已移除（保留字段以免旧配置/预览代码报错，但不再绘制）
    bool  weapon = true;
    bool  distance = false;
    bool  skeleton = true;
    bool  snapline = false;
    bool  ammo = false;
    bool  flags = true;
    bool  visibleOnly = false;
    bool  teamCheck = true;
    int   boxStyle = 0;
    float maxDistance = 1200.0f;
    float fillAlpha = 18.0f;
    float colBox[4]      = { 0.651f, 0.478f, 1.000f, 1.00f };
    float colVisible[4]  = { 1.000f, 1.000f, 1.000f, 1.00f };
    float colFill[4]     = { 0.651f, 0.478f, 1.000f, 1.00f };
    float colText[4]     = { 0.933f, 0.933f, 0.960f, 1.00f };
    float colSkeleton[4] = { 1.000f, 1.000f, 1.000f, 0.90f };
    bool  worldEnable = true;
    bool  nightMode = false;
    bool  removeScope = false;
    bool  removeSmoke = false;
    bool  radarHack = false;
    float radarScale = 0.70f;      // 雷达缩放（cl_radar_scale）
    bool  bulletImpacts = true;
    float fovOverride = 0.0f;
    int   fovKey = 0;
    int   aspectRatio = 0;
    bool  chams = false;
    bool  thirdPerson = false;     // 第三人称
    int   thirdPersonKey = 0;      // 第三人称切换键（Hold/Toggle 可选）
    int   chamsMaterial = 0;
    float chamsColor[4] = { 1.000f, 1.000f, 1.000f, 1.00f };
    bool  glow = false;
    float glowColor[4] = { 0.651f, 0.478f, 1.000f, 1.00f };
    bool  previewAnim = true;
    bool  bombTimer = true;
    int   previewTeam = 2;
};
struct MiscConfig {
    bool  bhop = true;
    int   bhopKey = 0;
    bool  autoStrafe = true;
    int   strafeMode = 0;
    bool  edgeJump = false;
    int   edgeJumpKey = 0;
    bool  jumpBug = false;
    bool  autoPeek = false;
    int   autoPeekKey = 0;
    bool  autoAccept = true;
    bool  revealRanks = true;
    bool  muteEnemy = false;
    bool  antiAfk = true;
    bool  noFlash = true;
    bool  autoPistol = false;
    bool  knifeBot = false;
    int   knifeMode = 0;
    bool  clantag = false;
    char  clantagText[32] = "neverlose.ui";
    bool  chatSpam = false;
    char  chatText[64] = "neverlose.ui on top";
    float chatDelay = 5.0f;
};
struct Settings {
    MenuConfig   menu;
    RageConfig   rage;
    RageConfig   legit;
    AaConfig     aa;
    VisualConfig vis;
    MiscConfig   misc;
};
extern Settings g_settings;
void SettingsDefaults(Settings& s);
std::string ConfigDirectory();
bool ConfigSave(const Settings& s, const char* name);
bool ConfigLoad(Settings& s, const char* name);
bool ConfigDelete(const char* name);
int  ConfigList(char names[][64], int maxCount);
bool ConfigLoadLast(Settings& s);
void ConfigSaveLast(const Settings& s);
}


