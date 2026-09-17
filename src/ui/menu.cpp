#include "ui/menu.h"
#include "ui/widgets.h"
#include "ui/style.h"
#include "features/esp.h"
#include "ui/tab_memory.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
using namespace ImGui;
namespace nl { namespace ui {
static const char* kTabs[] = { "Rage", "Legit", "Anti-Aim", "Visuals", "Misc", "Scripts", "Memory", "Settings" };
static const icons::Icon kTabIcons[] = {
    icons::Icon_Target, icons::Icon_Crosshair, icons::Icon_Skull, icons::Icon_Eye,
    icons::Icon_Sliders, icons::Icon_Code, icons::Icon_Planet, icons::Icon_Gear
};
static const char* kTabDesc[] = {
    "ragebot  ·  target selection", "legit  ·  humanized aim assist", "anti-aim  ·  desync & fake lag",
    "visuals  ·  player drawing", "misc  ·  movement & world", "scripts  ·  lua runtime",
    "memory  ·  target & offsets", "settings  ·  interface & configs"
};
enum { kTabCount = 8 };
static const char* kHitbox[]      = { "Head", "Neck", "Body", "Nearest" };
static const char* kPriority[]    = { "Field of view", "Distance", "Health", "Damage" };
static const char* kPitch[]       = { "None", "Down", "Up", "Fake down", "Fake up" };
static const char* kYawBase[]     = { "Local view", "At targets", "Spin", "Static" };
static const char* kJitterMode[]  = { "None", "Center", "Random", "Offset" };
static const char* kDesyncMode[]  = { "Static", "Jitter", "Random" };
static const char* kFakeLagMode[] = { "Static", "Adaptive", "Random", "Fluctuate" };
static const char* kBoxStyle[]    = { "Full box", "Corners", "Rounded" };
static const char* kAspect[]      = { "Default", "4:3 stretched", "16:9", "16:10" };
static const char* kChamsMat[]    = { "Flat", "Latex", "Glass", "Glow", "Wireframe" };
static const char* kStrafeMode[]  = { "Legit", "Rage", "Directional" };
static const char* kKnifeMode[]   = { "Always", "Auto" };
static const char* kPreviewTeam[] = { "CT", "T" };
static std::vector<std::string> g_log;
static bool  g_closeRequest = false;
static bool  g_restyleRequest = false;
static PanelRect g_panel;
PanelRect GetMenuPanelRect() { return g_panel; }
void LogPush(const char* fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    const int secs = (int)ImGui::GetTime();
    char line[320];
    snprintf(line, sizeof(line), "[%02d:%02d] %s", secs / 60, secs % 60, buf);
    g_log.emplace_back(line);
    if (g_log.size() > 260)
        g_log.erase(g_log.begin(), g_log.begin() + 60);
}
bool MenuCloseRequested()
{
    const bool v = g_closeRequest;
    g_closeRequest = false;
    return v;
}
bool ConsumeRestyleRequest()
{
    const bool v = g_restyleRequest;
    g_restyleRequest = false;
    return v;
}
static void PresetToAccent(int idx, float a[4], float b[4])
{
    const ImVec4 va = ColorConvertU32ToFloat4(PresetAccent(idx));
    const ImVec4 vb = ColorConvertU32ToFloat4(PresetAccent2(idx));
    a[0] = va.x; a[1] = va.y; a[2] = va.z; a[3] = 1.0f;
    b[0] = vb.x; b[1] = vb.y; b[2] = vb.z; b[3] = 1.0f;
}
static const char* H(const char* hint, const Settings& s) { return s.menu.hints ? hint : nullptr; }
static void CheckboxRow2(const char* l0, bool* v0, const char* l1, bool* v1)
{
    const ImVec2 p = GetCursorScreenPos();
    const float half = (RowWidth() - S(8.0f)) * 0.5f;
    SetCursorScreenPos(p);
    Checkbox(l0, v0, half);
    SetCursorScreenPos(ImVec2(p.x + half + S(8.0f), p.y));
    Checkbox(l1, v1, half);
    SetCursorScreenPos(ImVec2(p.x, p.y + RowH(false) + S(1.0f)));
}
static void ButtonRow2(const char* l0, const char* l1, bool accent0, bool* r0, bool* r1)
{
    const ImVec2 p = GetCursorScreenPos();
    const float half = (RowWidth() - S(8.0f)) * 0.5f;
    SetCursorScreenPos(p);
    *r0 = Button(l0, ImVec2(half, S(23.0f)), accent0);
    SetCursorScreenPos(ImVec2(p.x + half + S(8.0f), p.y));
    *r1 = Button(l1, ImVec2(half, S(23.0f)), false);
    SetCursorScreenPos(ImVec2(p.x, p.y + S(23.0f) + S(2.0f)));
}
static void PreviewBlock(Settings& s)
{
    const ImVec2 p = GetCursorScreenPos();
    const float w = RowWidth();
    const float h = S(220.0f);
    SetCursorScreenPos(p);
    InvisibleButton("##preview", ImVec2(w, h));
    ImDrawList* dl = GetWindowDrawList();
    const float t = s.vis.previewAnim ? (float)GetTime() : 0.0f;
    DrawPlayerPreview(dl, p, ImVec2(p.x + w, p.y + h), s.vis, t, 1.0f);
    SetCursorScreenPos(ImVec2(p.x, p.y + h + S(4.0f)));
}
static void DesyncView(Settings& s)
{
    const ImVec2 p = GetCursorScreenPos();
    const float w = RowWidth();
    const float h = S(158.0f);
    SetCursorScreenPos(p);
    InvisibleButton("##desync", ImVec2(w, h));
    ImDrawList* dl = GetWindowDrawList();
    const ImVec2 c(p.x + w * 0.5f, p.y + h * 0.5f);
    const float r = ImMin(w, h) * 0.33f;
    dl->AddCircleFilled(c, r + S(8.0f), ColAlpha(g_accent, 0.05f), 48);
    dl->AddCircle(c, r, ColAlpha(pal.text, 0.10f), 48, S(1.0f));
    dl->AddCircle(c, r * 0.55f, ColAlpha(pal.text, 0.05f), 32, S(1.0f));
    for (int i = 0; i < 8; ++i)
    {
        const float a = i * IM_PI * 0.25f;
        const ImVec2 d(sinf(a), -cosf(a));
        dl->AddLine(c + d * (r * 0.88f), c + d * r, ColAlpha(pal.text, 0.10f), S(1.0f));
    }
    const float t = (float)GetTime();
    const float spin = (s.aa.yawBase == 2) ? t * 90.0f : 0.0f;
    const float realYaw = ((s.aa.yawAdd + spin) * (IM_PI / 180.0f));
    const float fakeYaw = ((s.aa.yawAdd + spin + s.aa.desync) * (IM_PI / 180.0f));
    const ImVec2 dirReal(sinf(realYaw), -cosf(realYaw));
    const ImVec2 dirFake(sinf(fakeYaw), -cosf(fakeYaw));
    const float a0 = atan2f(dirReal.y, dirReal.x);
    const float a1 = atan2f(dirFake.y, dirFake.x);
    dl->PathArcTo(c, r * 0.92f, ImMin(a0, a1), ImMax(a0, a1), 32);
    dl->PathStroke(ColAlpha(g_accent, 0.35f), 0, S(2.0f));
    dl->AddLine(c, c + dirReal * (r * 1.02f), ColAlpha(g_accent2, 0.95f), S(2.0f));
    dl->AddCircleFilled(c + dirReal * (r * 1.02f), S(3.0f), g_accent2, 16);
    dl->AddLine(c, c + dirFake * (r * 1.02f), ColAlpha(g_accent, 0.95f), S(2.0f));
    dl->AddCircleFilled(c + dirFake * (r * 1.02f), S(3.0f), g_accent, 16);
    dl->AddCircleFilled(c, S(2.5f), ColAlpha(pal.text, 0.7f), 12);
    char buf[64];
    snprintf(buf, sizeof(buf), "client  %.0f°", (double)(s.aa.yawAdd + spin));
    TextSmall(buf, ColAlpha(g_accent2, 0.9f));
    snprintf(buf, sizeof(buf), "server  %.0f°   desync %.0f°", (double)(s.aa.yawAdd + spin + s.aa.desync), (double)s.aa.desync);
    TextSmall(buf, ColAlpha(g_accent, 0.9f));
    SetCursorScreenPos(ImVec2(p.x, p.y + h + S(2.0f)));
}
static void AccentDots(Settings& s, float dot)
{
    const ImVec2 p = GetCursorScreenPos();
    const int n = PresetCount();
    const float gap = S(8.0f);
    ImDrawList* dl = GetWindowDrawList();
    for (int i = 0; i < n; ++i)
    {
        const ImVec2 dc(p.x + i * (dot + gap) + dot * 0.5f, p.y + dot * 0.5f);
        PushID(400 + i);
        SetCursorScreenPos(ImVec2(dc.x - dot * 0.5f, dc.y - dot * 0.5f));
        InvisibleButton("##ad", ImVec2(dot, dot));
        const bool hov = IsItemHovered();
        const bool sel = (s.menu.preset == i);
        const float t = Tween(GetID("##ad_t"), hov ? 1.0f : 0.0f, 18.0f);
        dl->AddCircleFilled(dc, dot * 0.38f + S(1.4f) * t, PresetAccent(i), 24);
        if (sel)
            dl->AddCircle(dc, dot * 0.60f, ColAlpha(PresetAccent(i), 0.95f), 24, S(1.4f));
        if (IsItemClicked())
        {
            s.menu.preset = i;
            PresetToAccent(i, s.menu.accent, s.menu.accent2);
            LogPush("accent -> %s", PresetName(i));
        }
        if (hov)
        {
            BeginTooltip();
            TextUnformatted(PresetName(i));
            EndTooltip();
        }
        PopID();
    }
    SetCursorScreenPos(ImVec2(p.x, p.y + dot + S(4.0f)));
}
static void TabRage(Settings& s, Column& a, Column& b)
{
    BeginGroup(a, "rage_aim", "Aimbot", icons::Icon_Target, &s.rage.enable);
    Keybind("Aim key", &s.rage.key, H("Hold to aim", s));
    Combo("Hitbox", &s.rage.hitbox, kHitbox, 4, H("Preferred hit point", s));
    SliderFloat("Field of view", &s.rage.fov, 0.0f, 180.0f, "%.0f", H("Maximum aim angle", s));
    SliderFloat("Smoothing", &s.rage.smooth, 1.0f, 20.0f, "%.0f", H("Higher = slower aim", s));
    SliderFloat("Hitchance", &s.rage.hitchance, 0.0f, 100.0f, "%.0f", H("Minimum hit probability", s));
    EndGroup(a);
    BeginGroup(a, "rage_accuracy", "Accuracy", icons::Icon_Shield);
    SliderFloat("Min damage", &s.rage.mindamage, 0.0f, 130.0f, "%.0f", H("Required damage per shot", s));
    SliderFloat("Damage override", &s.rage.mindamageOverride, 0.0f, 130.0f, "%.0f", H("Applied when lethal", s));
    Toggle("Auto wall", &s.rage.autowall, H("Shoot through penetrable walls", s));
    Toggle("Auto scope", &s.rage.autoScope, H("Scope before shooting", s));
    Toggle("Auto stop", &s.rage.autoStop, H("Stop movement for accuracy", s));
    Toggle("Multi point", &s.rage.multipoint, H("Scan multiple hit points", s));
    EndGroup(a);
    BeginGroup(a, "rage_target", "Target selection", icons::Icon_Crosshair);
    Combo("Priority", &s.rage.priority, kPriority, 4, H("How targets are ranked", s));
    CheckboxRow2("Ignore limbs", &s.rage.ignoreLimbs, "Safe point", &s.rage.forceSafePoint);
    CheckboxRow2("Ignore invisible", &s.rage.ignoreInvisible, "Prefer armor", &s.rage.preferArmor);
    SliderFloat("Backtrack", &s.rage.backtrack, 0.0f, 400.0f, "%.0f", H("Backtrack window in ms", s));
    Toggle("Auto fire", &s.rage.autoFire, H("Fire without pressing attack", s));
    EndGroup(a);
    BeginGroup(b, "rage_exploits", "Exploits", icons::Icon_Zap);
    Toggle("Double tap", &s.rage.doubleTap, H("Charge a second shot", s));
    Keybind("DT key", &s.rage.doubleTapKey, H("Double tap hold key", s));
    Toggle("Hide shots", &s.rage.hideShots, H("Fake lag while shooting", s));
    Toggle("Anti-aim on shot", &s.rage.aaOnShot, H("Desync while shooting", s));
    Toggle("Between shots", &s.rage.betweenShots, H("Skip unnecessary shots", s));
    EndGroup(b);
    BeginGroup(b, "rage_state", "State", icons::Icon_Info);
    Badge("PENDING HOOK", pal.warn);
    TextSmall("AimbotTick() 目前是占位实现。", pal.textFaint);
    TextSmall("接入 CreateMove 后这里显示实时目标。", pal.textFaint);
    EndGroup(b);
}
static void TabLegit(Settings& s, Column& a, Column& b)
{
    BeginGroup(a, "legit_aim", "Aim assist", icons::Icon_Crosshair, &s.legit.enable);
    Keybind("Aim key", &s.legit.key, H("Hold to assist", s));
    Combo("Hitbox", &s.legit.hitbox, kHitbox, 4, nullptr);
    SliderFloat("Field of view", &s.legit.fov, 0.0f, 40.0f, "%.1f", H("Assist radius in degrees", s));
    SliderFloat("Smoothing", &s.legit.smooth, 1.0f, 30.0f, "%.0f", H("Humanized aim speed", s));
    SliderFloat("Hitchance", &s.legit.hitchance, 0.0f, 100.0f, "%.0f", nullptr);
    EndGroup(a);
    BeginGroup(a, "legit_trigger", "Triggerbot", icons::Icon_Target, &s.legit.triggerbot);
    SliderFloat("Delay", &s.legit.triggerDelay, 0.0f, 250.0f, "%.0f", H("Reaction time in ms", s));
    SliderFloat("Hitchance", &s.legit.triggerHitchance, 0.0f, 100.0f, "%.0f", nullptr);
    Toggle("Backtrack", &s.legit.backtrackLegit, H("Use backtrack records", s));
    Toggle("Auto scope", &s.legit.autoScope, nullptr);
    Toggle("Auto stop", &s.legit.autoStop, nullptr);
    EndGroup(a);
    BeginGroup(b, "legit_human", "Humanization", icons::Icon_User);
    SliderFloat("Reaction time", &s.legit.triggerDelay, 20.0f, 300.0f, "%.0f", H("Delay before aim starts", s));
    SliderFloat("Micro movement", &s.legit.fov, 0.0f, 12.0f, "%.1f", H("Small aim jitter", s));
    Combo("Priority", &s.legit.priority, kPriority, 4, nullptr);
    CheckboxRow2("Visible only", &s.legit.ignoreInvisible, "Prefer head", &s.legit.multipoint);
    EndGroup(b);
    BeginGroup(b, "legit_notes", "Notes", icons::Icon_Info);
    TextSmall("Legit 页复用 Rage 的数据结构，", pal.textFaint);
    TextSmall("后续单独挂钩鼠标输入路径。", pal.textFaint);
    EndGroup(b);
}
static void TabAntiAim(Settings& s, Column& a, Column& b)
{
    BeginGroup(a, "aa_main", "Anti-aim", icons::Icon_Skull, &s.aa.enable);
    Combo("Pitch", &s.aa.pitch, kPitch, 5, H("Vertical angle override", s));
    Combo("Yaw base", &s.aa.yawBase, kYawBase, 4, H("Base yaw direction", s));
    SliderInt("Yaw add", &s.aa.yawAdd, -180, 180, H("Offset from base yaw", s));
    SliderFloat("Yaw jitter", &s.aa.yawJitter, 0.0f, 100.0f, "%.0f", H("Jitter intensity", s));
    Combo("Jitter mode", &s.aa.jitterMode, kJitterMode, 4, nullptr);
    SliderFloat("Desync", &s.aa.desync, -60.0f, 60.0f, "%.0f", H("Server / client yaw delta", s));
    Combo("Desync mode", &s.aa.desyncMode, kDesyncMode, 3, nullptr);
    SliderFloat("Body yaw", &s.aa.bodyYaw, -180.0f, 180.0f, "%.0f", nullptr);
    EndGroup(a);
    BeginGroup(a, "aa_view", "Desync view", icons::Icon_Target);
    DesyncView(s);
    EndGroup(a);
    BeginGroup(b, "aa_fakelag", "Fake lag", icons::Icon_Planet, &s.aa.fakeLag);
    Combo("Mode", &s.aa.fakeLagMode, kFakeLagMode, 4, H("Choke behaviour", s));
    SliderInt("Ticks", &s.aa.fakeLagTicks, 1, 16, H("Choke amount per tick", s));
    Toggle("Fake duck", &s.aa.fakeDuck, nullptr);
    Keybind("Duck key", &s.aa.fakeDuckKey, nullptr);
    Toggle("Fake peek", &s.aa.fakePeek, nullptr);
    Keybind("Peek key", &s.aa.fakePeekKey, nullptr);
    Toggle("Air stuck", &s.aa.airStuck, nullptr);
    Keybind("Stuck key", &s.aa.airStuckKey, nullptr);
    Toggle("Slow walk", &s.aa.slowWalk, nullptr);
    Keybind("Walk key", &s.aa.slowWalkKey, nullptr);
    EndGroup(b);
    BeginGroup(b, "aa_info", "Notices", icons::Icon_Info);
    TextSmall("角度目前只写入配置，接入 CreateMove 后生效。", pal.textFaint);
    EndGroup(b);
}
static void TabVisuals(Settings& s, Column& a, Column& b)
{
    BeginGroup(a, "vis_players", "Players", icons::Icon_Eye, &s.vis.enable);
    Toggle("Box", &s.vis.box, H("Draw player box", s));
    Toggle("Filled", &s.vis.filled, H("Fill the box", s));
    Toggle("Name", &s.vis.name, H("Show player name", s));
    Toggle("Health", &s.vis.health, H("Health bar next to box", s));
    // Armor bar 已按要求移除（护甲条不画了）
    Toggle("Weapon", &s.vis.weapon, H("Show held weapon", s));
    Toggle("Skeleton", &s.vis.skeleton, H("Draw bone lines", s));
    Toggle("Snapline", &s.vis.snapline, H("Line from screen bottom", s));
    Toggle("Ammo", &s.vis.ammo, H("Ammo bar under the box", s));
    Toggle("Bomb timer", &s.vis.bombTimer, H("顶部炸弹倒计时面板", s));
    Toggle("Flags", &s.vis.flags, H("Kit / zoom markers", s));
    CheckboxRow2("Visible only", &s.vis.visibleOnly, "Team check", &s.vis.teamCheck);
    Combo("Box style", &s.vis.boxStyle, kBoxStyle, 3, nullptr);
    SliderFloat("Max distance", &s.vis.maxDistance, 0.0f, 3000.0f, "%.0f", H("Ignore players further away", s));
    SliderFloat("Fill alpha", &s.vis.fillAlpha, 0.0f, 100.0f, "%.0f", nullptr);
    EndGroup(a);
    BeginGroup(a, "vis_colors", "Colors", icons::Icon_Palette);
    ColorEdit("Box", s.vis.colBox, nullptr);
    ColorEdit("Visible", s.vis.colVisible, H("Color when visible", s));
    ColorEdit("Fill", s.vis.colFill, nullptr);
    ColorEdit("Name", s.vis.colText, nullptr);
    ColorEdit("Skeleton", s.vis.colSkeleton, nullptr);
    EndGroup(a);
    BeginGroup(b, "vis_preview", "Preview", icons::Icon_Box);
    PreviewBlock(s);
    TabBar("##team", kPreviewTeam, 2, &s.vis.previewTeam, RowWidth());
    EndGroup(b);
    BeginGroup(b, "vis_world", "World", icons::Icon_Earth);
    Toggle("Night mode", &s.vis.nightMode, nullptr);
    Toggle("Remove scope", &s.vis.removeScope, nullptr);
    Toggle("Remove smoke", &s.vis.removeSmoke, nullptr);
    Toggle("Radar", &s.vis.radarHack, H("Reveal enemies on radar", s));
                SliderFloat("Radar scale", &s.vis.radarScale, 0.30f, 1.00f, "%.2f", H("cl_radar_scale", s));
    SliderFloat("FOV override", &s.vis.fovOverride, 0.0f, 140.0f, "%.0f", H("0 = game default", s));
    Keybind("FOV key", &s.vis.fovKey, nullptr);
    Combo("Aspect ratio", &s.vis.aspectRatio, kAspect, 4, nullptr);
    EndGroup(b);
    BeginGroup(b, "vis_chams", "Chams", icons::Icon_Shield, &s.vis.chams);
    Combo("Material", &s.vis.chamsMaterial, kChamsMat, 5, nullptr);
    ColorEdit("Chams color", s.vis.chamsColor, nullptr);
    Toggle("Third person", &s.vis.thirdPerson, H("写游戏自带的第三人称状态字节", s));
                Keybind("TP key", &s.vis.thirdPersonKey, nullptr);
                Toggle("Glow", &s.vis.glow, H("Outline glow around players", s));
    ColorEdit("Glow color", s.vis.glowColor, nullptr);
    EndGroup(b);
}
static void TabMisc(Settings& s, Column& a, Column& b)
{
    BeginGroup(a, "misc_move", "Movement", icons::Icon_Skate);
    Toggle("Bunny hop", &s.misc.bhop, H("按住跳键时自动连跳，松手即停", s));
    Keybind("Bhop key", &s.misc.bhopKey, nullptr);
    Toggle("Auto strafe", &s.misc.autoStrafe, H("按住跳键且离地时自动转向", s));
    Combo("Strafe mode", &s.misc.strafeMode, kStrafeMode, 3, nullptr);
    Toggle("Edge jump", &s.misc.edgeJump, H("Jump before leaving a ledge", s));
    Keybind("Edge key", &s.misc.edgeJumpKey, nullptr);
    Toggle("Jump bug", &s.misc.jumpBug, nullptr);
    Toggle("Auto peek", &s.misc.autoPeek, H("Peek and return to position", s));
    Keybind("Peek key", &s.misc.autoPeekKey, nullptr);
    EndGroup(a);
    BeginGroup(a, "misc_world", "World", icons::Icon_Earth);
    Toggle("Auto accept", &s.misc.autoAccept, nullptr);
    Toggle("Reveal ranks", &s.misc.revealRanks, nullptr);
    Toggle("Mute enemy", &s.misc.muteEnemy, nullptr);
    Toggle("Anti AFK", &s.misc.antiAfk, nullptr);
    Toggle("No flash", &s.misc.noFlash, nullptr);
    Toggle("Auto pistol", &s.misc.autoPistol, nullptr);
    Toggle("Knife bot", &s.misc.knifeBot, nullptr);
    Combo("Knife mode", &s.misc.knifeMode, kKnifeMode, 2, nullptr);
    EndGroup(a);
    BeginGroup(b, "misc_chat", "Chat & tag", icons::Icon_Text);
    Toggle("Clan tag", &s.misc.clantag, H("Animated clan tag", s));
    InputText("##clantag", s.misc.clantagText, sizeof(s.misc.clantagText), "clan tag", RowWidth(), nullptr);
    Toggle("Chat spam", &s.misc.chatSpam, nullptr);
    InputText("##chat", s.misc.chatText, sizeof(s.misc.chatText), "message", RowWidth(), nullptr);
    SliderFloat("Delay", &s.misc.chatDelay, 1.0f, 30.0f, "%.1f", H("Seconds between messages", s));
    EndGroup(b);
    BeginGroup(b, "misc_info", "Key binds", icons::Icon_Key);
    TextSmall("这些绑定会写入配置并在注入后生效。", pal.textFaint);
    TextSmall("当前为独立窗口模式，按键仅记录。", pal.textFaint);
    EndGroup(b);
}
static void TabScripts(Settings& s, Column& a, Column& b)
{
    static bool enabled[5] = { true, false, true, false, false };
    static const char* names[5] = { "neverlose_ui.lua", "custom_esp.lua", "movement.lua", "skin_changer.lua", "binds.lua" };
    static const char* notes[5] = { "core ui  ·  4.2 kb", "player drawing  ·  11.8 kb", "bhop & strafe  ·  2.1 kb", "models  ·  26.4 kb", "key binds  ·  1.3 kb" };
    BeginGroup(a, "scripts", "Lua runtime", icons::Icon_Code, nullptr);
    for (int i = 0; i < 5; ++i)
    {
        if (Toggle(names[i], &enabled[i], H(notes[i], s)))
            LogPush(enabled[i] ? "script loaded  %s" : "script unloaded  %s", names[i]);
    }
    Separator("runtime");
    bool r0 = false, r1 = false;
    ButtonRow2("Reload all", "Open folder", true, &r0, &r1);
    if (r0)
        LogPush("reloading 5 scripts...");
    if (r1)
        LogPush("opening scripts folder");
    EndGroup(a);
    BeginGroup(a, "scripts_state", "State", icons::Icon_Info);
    TextSmall("lua 5.4  ·  sandbox on  ·  jit off", pal.textDim);
    TextSmall("hooks: on_paint / on_createmove", pal.textDim);
    Badge("SANDBOXED", pal.success);
    EndGroup(a);
    BeginGroup(b, "log", "Console", icons::Icon_Sliders);
    const ImVec2 lp = GetCursorScreenPos();
    const float lw = RowWidth();
    const float lh = S(196.0f);
    SetCursorScreenPos(lp);
    InvisibleButton("##logbox", ImVec2(lw, lh));
    ImDrawList* dl = GetWindowDrawList();
    dl->AddRectFilled(lp, ImVec2(lp.x + lw, lp.y + lh), IM_COL32(8, 8, 11, 215), S(6.0f));
    dl->AddRect(lp, ImVec2(lp.x + lw, lp.y + lh), ColAlpha(pal.text, 0.06f), S(6.0f), S(1.0f));
    PushFont(font.regular, S(11.0f));
    const int maxLines = (int)((lh - S(12.0f)) / S(13.0f));
    int start = (int)g_log.size() - maxLines;
    if (start < 0)
        start = 0;
    float ly = lp.y + S(6.0f);
    for (int i = start; i < (int)g_log.size(); ++i)
    {
        const std::string& ln = g_log[i];
        ImU32 col = pal.textDim;
        if (ln.find("error") != std::string::npos)
            col = pal.danger;
        else if (ln.find("saved") != std::string::npos || ln.find("loaded") != std::string::npos)
            col = pal.success;
        dl->AddText(ImVec2(lp.x + S(8.0f), ly), col, ln.c_str());
        ly += S(13.0f);
    }
    PopFont();
    SetCursorScreenPos(ImVec2(lp.x, lp.y + lh + S(3.0f)));
    bool c0 = false, c1 = false;
    ButtonRow2("Clear", "Copy", false, &c0, &c1);
    if (c0)
    {
        g_log.clear();
        LogPush("console cleared");
    }
    if (c1)
        LogPush("log copied to clipboard");
    EndGroup(b);
}
static void TabSettings(Settings& s, Column& a, Column& b)
{
    BeginGroup(a, "ui", "Interface", icons::Icon_Palette);
    if (SliderFloat("UI scale", &s.menu.uiScale, 0.85f, 1.35f, "%.2f", H("Interface zoom (reloads fonts)", s)))
        g_restyleRequest = true;
    Toggle("Watermark", &s.menu.watermark, H("Overlay watermark", s));
    Toggle("Row hints", &s.menu.hints, H("Descriptions under labels", s));
    Toggle("Animations", &s.menu.animations, H("Smooth transitions", s));
    Toggle("Key hints", &s.menu.keyHints, H("Key bind hints in the status bar", s));
    Separator("accent");
    AccentDots(s, S(20.0f));
    EndGroup(a);
    BeginGroup(a, "cfg", "Configs", icons::Icon_Save);
    InputText("##cfgname", s.menu.configName, sizeof(s.menu.configName), "config name", RowWidth(), nullptr);
    bool r0 = false, r1 = false, r2 = false, r3 = false;
    ButtonRow2("Save", "Load", true, &r0, &r1);
    if (r0)
        LogPush(ConfigSave(s, s.menu.configName) ? "config saved  %s" : "error saving config", s.menu.configName);
    if (r1)
        LogPush(ConfigLoad(s, s.menu.configName) ? "config loaded  %s" : "error loading config", s.menu.configName);
    ButtonRow2("Delete", "Reset", false, &r2, &r3);
    if (r2)
    {
        ConfigDelete(s.menu.configName);
        LogPush("config deleted  %s", s.menu.configName);
    }
    if (r3)
    {
        SettingsDefaults(s);
        LogPush("settings reset to defaults");
    }
    Separator("stored configs");
    static char names[32][64];
    const int n = ConfigList(names, 32);
    if (n == 0)
        TextSmall("no configs yet", pal.textFaint);
    for (int i = 0; i < n; ++i)
    {
        const ImVec2 p = GetCursorScreenPos();
        const float w = RowWidth();
        PushID(500 + i);
        SetCursorScreenPos(p);
        InvisibleButton("##cfgrow", ImVec2(ImMax(S(10.0f), w - S(54.0f)), S(22.0f)));
        const bool hov = IsItemHovered();
        ImDrawList* dl = GetWindowDrawList();
        const float t = Tween(GetID("##h"), hov ? 1.0f : 0.0f, 16.0f);
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + S(22.0f)), ColAlpha(IM_COL32(255, 255, 255, 255), 0.02f + 0.05f * t), S(5.0f));
        PushFont(font.semibold, FontBody());
        dl->AddText(ImVec2(p.x + S(8.0f), p.y + S(11.0f) - GetFontSize() * 0.5f),
                    ColMix(pal.textDim, pal.text, 0.40f + 0.60f * t), names[i]);
        PopFont();
        SetCursorScreenPos(ImVec2(p.x + w - S(44.0f), p.y + S(2.0f)));
        if (IconButton("load", icons::Icon_Folder, S(18.0f), "Load"))
        {
            ConfigLoad(s, names[i]);
            snprintf(s.menu.configName, sizeof(s.menu.configName), "%s", names[i]);
            LogPush("config loaded  %s", names[i]);
        }
        SetCursorScreenPos(ImVec2(p.x + w - S(22.0f), p.y + S(2.0f)));
        if (IconButton("del", icons::Icon_Trash, S(18.0f), "Delete"))
        {
            ConfigDelete(names[i]);
            LogPush("config deleted  %s", names[i]);
        }
        PopID();
        SetCursorScreenPos(ImVec2(p.x, p.y + S(24.0f)));
    }
    EndGroup(a);
    BeginGroup(b, "about", "About", icons::Icon_Info);
    TextNormal("neverlose.ui  ·  build 1.0.0", pal.text, true);
    TextSmall("Dear ImGui 1.93 (docking)  ·  DirectX 11", pal.textDim);
    TextSmall("MSVC x64  ·  Win32  ·  single window overlay", pal.textDim);
    Separator(nullptr);
    TextSmall("人物绘制接入点", pal.textFaint);
    TextSmall("features/esp.h   DrawPlayerOverlay()", pal.textDim);
    TextSmall("features/esp.cpp OnOverlayTick()", pal.textDim);
    TextSmall("同一个函数同时驱动预览与真实 ESP。", pal.textFaint);
    EndGroup(b);
    BeginGroup(b, "bind_info", "Binds", icons::Icon_Key);
    TextSmall("INSERT   开关菜单", pal.textDim);
    TextSmall("ESC      关闭菜单", pal.textDim);
    TextSmall("CTRL+Q   退出", pal.textDim);
    EndGroup(b);
}
static void DrawTab(Settings& s, int tab, float dt)
{
    IM_UNUSED(dt);
    const ImVec2 start = GetCursorScreenPos();
    const float avail = GetContentRegionAvail().x - S(10.0f);
    const float gap = S(12.0f);
    const bool two = avail > S(560.0f);
    const float colW = two ? (avail - gap) * 0.5f : avail;
    Column c0;
    c0.x = start.x; c0.y = start.y; c0.w = colW; c0.bottom = start.y;
    Column c1;
    c1.x = start.x + (two ? colW + gap : 0.0f); c1.y = start.y; c1.w = colW; c1.bottom = start.y;
    SetCursorScreenPos(ImVec2(c0.x, c0.y));
    Column& r0 = c0;
    Column& r1 = two ? c1 : c0;
    switch (tab)
    {
    case 0: TabRage(s, r0, r1); break;
    case 1: TabLegit(s, r0, r1); break;
    case 2: TabAntiAim(s, r0, r1); break;
    case 3: TabVisuals(s, r0, r1); break;
    case 4: TabMisc(s, r0, r1); break;
    case 5: TabScripts(s, r0, r1); break;
    case 6: TabMemory(s, r0, r1); break;
    default: TabSettings(s, r0, r1); break;
    }
    const float bottom = ImMax(c0.bottom, c1.bottom);
    SetCursorScreenPos(ImVec2(start.x, bottom + S(6.0f)));
    Dummy(ImVec2(1.0f, 1.0f));
}
static void Sidebar(Settings& s, const ImVec2& o, float w, float h)
{
    ImDrawList* dl = GetWindowDrawList();
    const ImVec2 mn = o;
    const ImVec2 mx(o.x + w, o.y + h);
    dl->AddRectFilled(mn, mx, pal.sidebar);
    dl->AddLine(ImVec2(mx.x - S(1.0f), mn.y), ImVec2(mx.x - S(1.0f), mx.y), pal.separator);
    const float pad = S(15.0f);
    float y = o.y + S(16.0f);
    const float av = S(38.0f);
    const ImVec2 ac(o.x + pad + av * 0.5f, y + av * 0.5f);
    dl->AddCircleFilled(ac, av * 0.5f + S(1.5f), ColAlpha(g_accent, 0.14f), 40);
    dl->AddCircle(ac, av * 0.5f, ColAlpha(g_accent, 0.85f), 40, S(1.6f));
    icons::Draw(dl, icons::Icon_User, ac, av * 0.50f, pal.text, S(1.6f));
    PushFont(font.semibold, FontTitle());
    dl->AddText(ImVec2(ac.x + av * 0.5f + S(10.0f), y + S(1.0f)), pal.text, "Administrator");
    PopFont();
    PushFont(font.smallText, FontSmall());
    dl->AddText(ImVec2(ac.x + av * 0.5f + S(10.0f), y + S(22.0f)), g_accent, "premium  ·  lifetime");
    PopFont();
    y += av + S(14.0f);
    dl->AddRectFilled(ImVec2(o.x + pad, y), ImVec2(mx.x - pad, y + S(3.0f)), ColAlpha(pal.text, 0.07f), S(1.5f));
    dl->AddRectFilled(ImVec2(o.x + pad, y), ImVec2(mx.x - pad, y + S(3.0f)), g_accent, S(1.5f));
    PushFont(font.smallText, FontSmall());
    dl->AddText(ImVec2(o.x + pad, y + S(8.0f)), pal.textFaint, "license active  ·  build 1.0.0");
    PopFont();
    y += S(30.0f);
    SetCursorScreenPos(ImVec2(o.x + pad, y));
    InputText("##search", s.menu.search, sizeof(s.menu.search), "Search settings...", w - pad * 2.0f, nullptr);
    SetFilter(s.menu.search);
    y += S(32.0f);
    // 页签数量增加后自适应高度，保证不会顶到底部提示
    float itemH = S(32.0f);
    {
        const float bottomReserve = S(46.0f);
        const float avail = (mx.y - bottomReserve) - y;
        if (itemH * (float)kTabCount > avail)
            itemH = ImMax(S(22.0f), avail / (float)kTabCount - S(2.0f));
    }
    for (int i = 0; i < kTabCount; ++i)
    {
        const ImVec2 ip(o.x + S(8.0f), y + i * (itemH + S(2.0f)));
        const float iw = w - S(16.0f);
        PushID(100 + i);
        SetCursorScreenPos(ip);
        InvisibleButton("##tab", ImVec2(iw, itemH));
        const bool hov = IsItemHovered();
        const bool sel = (s.menu.activeTab == i);
        if (IsItemClicked() && !sel)
        {
            s.menu.activeTab = i;
            LogPush("tab -> %s", kTabs[i]);
        }
        const float t = Tween(GetID("##t"), sel ? 1.0f : (hov ? 0.5f : 0.0f), 16.0f);
        ImU32 bg = sel ? ColMix(pal.widget, g_accent, 0.26f) : pal.widget;
        if (t > 0.01f)
            dl->AddRectFilled(ip, ImVec2(ip.x + iw, ip.y + itemH), ColAlpha(bg, sel ? 1.0f : 0.55f * t), S(7.0f));
        const float barH = itemH * 0.32f * ImClamp(t * 1.6f, 0.0f, 1.0f);
        if (barH > 0.5f)
            dl->AddRectFilled(ImVec2(ip.x, ip.y + itemH * 0.5f - barH), ImVec2(ip.x + S(2.6f), ip.y + itemH * 0.5f + barH), g_accent, S(1.3f));
        const float slide = S(2.0f) * t;
        icons::Draw(dl, kTabIcons[i], ImVec2(ip.x + S(21.0f) + slide, ip.y + itemH * 0.5f), S(14.0f),
                    ColMix(pal.textDim, sel ? g_accent : pal.text, ImClamp(t * 1.3f + (sel ? 0.4f : 0.0f), 0.0f, 1.0f)), S(1.5f));
        PushFont(font.semibold, FontBody());
        dl->AddText(ImVec2(ip.x + S(38.0f) + slide, ip.y + itemH * 0.5f - GetFontSize() * 0.5f),
                    ColMix(pal.textDim, pal.text, ImClamp(t * 1.6f, 0.0f, 1.0f)), kTabs[i]);
        PopFont();
        if (sel)
            icons::Draw(dl, icons::Icon_ChevronRight, ImVec2(ip.x + iw - S(14.0f), ip.y + itemH * 0.5f),
                        S(9.0f), ColAlpha(g_accent, 0.85f), S(1.4f));
        PopID();
    }
    PushFont(font.smallText, FontSmall());
    dl->AddText(ImVec2(o.x + pad, mx.y - S(30.0f)), pal.textFaint, "INSERT   toggle menu");
    dl->AddText(ImVec2(o.x + pad, mx.y - S(17.0f)), pal.textFaint, "neverlose.ui  ·  dx11");
    PopFont();
}
static void Content(Settings& s, const ImVec2& wp, const ImVec2& ws, float dt)
{
    const float sbW = S(208.0f);
    const ImVec2 mn(wp.x + sbW, wp.y);
    const ImVec2 mx(wp.x + ws.x, wp.y + ws.y);
    ImDrawList* dl = GetWindowDrawList();
    const float pad = S(18.0f);
    const int tab = ImClamp(s.menu.activeTab, 0, kTabCount - 1);
    PushFont(font.semibold, FontBig());
    dl->AddText(ImVec2(mn.x + pad, mn.y + pad - S(2.0f)), pal.text, kTabs[tab]);
    PopFont();
    PushFont(font.smallText, FontSmall());
    dl->AddText(ImVec2(mn.x + pad, mn.y + pad + S(24.0f)), pal.textDim, kTabDesc[tab]);
    PopFont();
    SetCursorScreenPos(ImVec2(mx.x - pad - S(24.0f), mn.y + pad + S(2.0f)));
    if (IconButton("close", icons::Icon_Close, S(24.0f), "Close menu (INSERT)", false, false))
    {
        g_closeRequest = true;
        LogPush("menu hidden");
    }
    const float dot = S(14.0f);
    const float dotsW = PresetCount() * (dot + S(7.0f));
    SetCursorScreenPos(ImVec2(mx.x - pad - S(34.0f) - dotsW, mn.y + pad + S(7.0f)));
    AccentDots(s, dot);
    dl->AddLine(ImVec2(mn.x + pad, mn.y + S(62.0f)), ImVec2(mx.x - pad, mn.y + S(62.0f)), pal.separator);
    const float contentTop = mn.y + S(74.0f);
    const float contentH = ImMax(S(80.0f), mx.y - S(30.0f) - contentTop);
    SetCursorScreenPos(ImVec2(mn.x + pad, contentTop));
    PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    BeginChild("##content", ImVec2(mx.x - mn.x - pad * 2.0f, contentH), ImGuiChildFlags_None, ImGuiWindowFlags_None);
    DrawTab(s, tab, dt);
    EndChild();
    PopStyleVar();
    dl->AddLine(ImVec2(mn.x, mx.y - S(29.0f)), ImVec2(mx.x, mx.y - S(29.0f)), pal.separator);
    char info[192];
    snprintf(info, sizeof(info), "%s  ·  %.0f fps  ·  %.2f ms  ·  %.0f vtx",
             kTabs[tab], (double)GetIO().Framerate, 1000.0 / ImMax(1.0, (double)GetIO().Framerate),
             (double)GetIO().MetricsRenderVertices);
    PushFont(font.smallText, FontSmall());
    dl->AddText(ImVec2(mn.x + pad, mx.y - S(20.0f)), pal.textFaint, info);
    if (s.menu.keyHints)
    {
        const char* hint = "INSERT  menu      ESC  close      CTRL+Q  exit";
        const float hw = CalcTextSize(hint).x;
        dl->AddText(ImVec2(mx.x - pad - hw, mx.y - S(20.0f)), pal.textFaint, hint);
    }
    PopFont();
}
void DrawMenu(Settings& s, float dt)
{
    IM_UNUSED(dt);
    ApplyTheme(s.menu.accent, s.menu.accent2);
    ImGuiViewport* vp = GetMainViewport();
    if (!s.menu.hasPos)
    {
        s.menu.size[0] = S(920.0f);
        s.menu.size[1] = S(580.0f);
        s.menu.pos[0] = vp->Pos.x + (vp->Size.x - s.menu.size[0]) * 0.5f;
        s.menu.pos[1] = vp->Pos.y + (vp->Size.y - s.menu.size[1]) * 0.5f;
        s.menu.hasPos = true;
        LogPush("ui ready");
        LogPush("insert toggles the menu, ctrl+q exits");
    }
    s.menu.size[0] = ImClamp(s.menu.size[0], S(780.0f), ImMax(S(780.0f), vp->Size.x));
    s.menu.size[1] = ImClamp(s.menu.size[1], S(470.0f), ImMax(S(470.0f), vp->Size.y));
    s.menu.pos[0] = ImClamp(s.menu.pos[0], vp->Pos.x - S(40.0f), vp->Pos.x + vp->Size.x - S(140.0f));
    s.menu.pos[1] = ImClamp(s.menu.pos[1], vp->Pos.y, vp->Pos.y + vp->Size.y - S(60.0f));
    SetNextWindowPos(ImVec2(s.menu.pos[0], s.menu.pos[1]));
    SetNextWindowSize(ImVec2(s.menu.size[0], s.menu.size[1]));
    PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                                   ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                                   ImGuiWindowFlags_NoSavedSettings;
    if (Begin("##neverlose_ui", nullptr, flags))
    {
        const ImVec2 wp = GetWindowPos();
        const ImVec2 ws = GetWindowSize();
        g_panel.x = wp.x;
        g_panel.y = wp.y;
        g_panel.w = ws.x;
        g_panel.h = ws.y;
        ImDrawList* dl = GetWindowDrawList();
        const float panelRound = g_colorKeyMode ? S(3.0f) : S(10.0f);
        if (!g_colorKeyMode)
        {
            for (int i = 6; i >= 1; --i)
            {
                const float e = (float)i * S(1.6f);
                dl->AddRect(ImVec2(wp.x - e, wp.y - e), ImVec2(wp.x + ws.x + e, wp.y + ws.y + e),
                            ColAlpha(g_accent, 0.030f), S(10.0f) + e, S(1.0f));
            }
        }
        dl->AddRectFilled(wp, ImVec2(wp.x + ws.x, wp.y + ws.y), pal.bg, panelRound);
        dl->AddRectFilled(wp, ImVec2(wp.x + ws.x, wp.y + ws.y * 0.62f), ColAlpha(g_accent, 0.035f), panelRound, ImDrawFlags_RoundCornersTop);
        dl->AddRectFilled(ImVec2(wp.x, wp.y + ws.y * 0.28f), ImVec2(wp.x + ws.x, wp.y + ws.y), ColAlpha(g_accent2, 0.020f), panelRound, ImDrawFlags_RoundCornersBottom);
        dl->AddRectFilled(ImVec2(wp.x + S(2.0f), wp.y + S(1.5f)), ImVec2(wp.x + ws.x - S(2.0f), wp.y + S(3.0f)), ColAlpha(g_accent, 0.55f), S(1.0f));
        dl->AddRect(ImVec2(wp.x + 0.5f, wp.y + 0.5f), ImVec2(wp.x + ws.x - 0.5f, wp.y + ws.y - 0.5f),
                    ColAlpha(g_accent, g_colorKeyMode ? 0.85f : 0.20f), panelRound, S(1.0f));
        Sidebar(s, wp, S(208.0f), ws.y);
        Content(s, wp, ws, dt);
    }
    End();
    PopStyleVar();
    PopStyleColor();
}
} }


