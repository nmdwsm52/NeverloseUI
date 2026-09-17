#include "features/config.h"
#include "core/paths.h"
#include <windows.h>
#include <string>
#include <vector>
#include <map>
#include <cstdio>
#include <cstring>
#include <cstdlib>
namespace nl {
Settings g_settings;
namespace {
class Ini {
public:
    bool writing = true;
    std::vector<std::pair<std::string, std::string>> items;
    std::map<std::string, std::string> values;
    void B(const char* k, bool& v)
    {
        if (writing) items.emplace_back(k, v ? "1" : "0");
        else { auto it = values.find(k); if (it != values.end()) v = (it->second != "0" && !it->second.empty()); }
    }
    void I(const char* k, int& v)
    {
        if (writing) { char b[32]; snprintf(b, sizeof(b), "%d", v); items.emplace_back(k, b); }
        else { auto it = values.find(k); if (it != values.end()) v = atoi(it->second.c_str()); }
    }
    void F(const char* k, float& v)
    {
        if (writing) { char b[48]; snprintf(b, sizeof(b), "%.4f", v); items.emplace_back(k, b); }
        else { auto it = values.find(k); if (it != values.end()) v = (float)atof(it->second.c_str()); }
    }
    void F4(const char* k, float v[4])
    {
        if (writing)
        {
            char b[128];
            snprintf(b, sizeof(b), "%.4f %.4f %.4f %.4f", v[0], v[1], v[2], v[3]);
            items.emplace_back(k, b);
        }
        else
        {
            auto it = values.find(k);
            if (it != values.end())
                sscanf(it->second.c_str(), "%f %f %f %f", &v[0], &v[1], &v[2], &v[3]);
        }
    }
    void S(const char* k, char* buf, size_t n)
    {
        if (writing) items.emplace_back(k, std::string(buf));
        else { auto it = values.find(k); if (it != values.end()) snprintf(buf, n, "%s", it->second.c_str()); }
    }
};
void Serialize(Settings& s, Ini& f)
{
    f.I("menu.activeTab", s.menu.activeTab);
    f.I("menu.preset", s.menu.preset);
    f.F4("menu.accent", s.menu.accent);
    f.F4("menu.accent2", s.menu.accent2);
    f.F("menu.uiScale", s.menu.uiScale);
    f.B("menu.watermark", s.menu.watermark);
    f.B("menu.blur", s.menu.blur);
    f.B("menu.hints", s.menu.hints);
    f.B("menu.animations", s.menu.animations);
    f.B("menu.keyHints", s.menu.keyHints);
    f.B("menu.autoAttach", s.menu.autoAttach);
    f.F("menu.posX", s.menu.pos[0]);
    f.F("menu.posY", s.menu.pos[1]);
    f.F("menu.sizeW", s.menu.size[0]);
    f.F("menu.sizeH", s.menu.size[1]);
    f.B("menu.hasPos", s.menu.hasPos);
    f.S("menu.configName", s.menu.configName, sizeof(s.menu.configName));
    f.S("menu.targetProcess", s.menu.targetProcess, sizeof(s.menu.targetProcess));
    f.S("menu.targetModule", s.menu.targetModule, sizeof(s.menu.targetModule));
    // rage
    f.B("rage.enable", s.rage.enable);
    f.I("rage.key", s.rage.key);
    f.I("rage.hitbox", s.rage.hitbox);
    f.I("rage.priority", s.rage.priority);
    f.F("rage.fov", s.rage.fov);
    f.F("rage.smooth", s.rage.smooth);
    f.F("rage.hitchance", s.rage.hitchance);
    f.F("rage.mindamage", s.rage.mindamage);
    f.F("rage.mindamageOverride", s.rage.mindamageOverride);
    f.F("rage.backtrack", s.rage.backtrack);
    f.B("rage.autowall", s.rage.autowall);
    f.B("rage.autoScope", s.rage.autoScope);
    f.B("rage.autoStop", s.rage.autoStop);
    f.B("rage.multipoint", s.rage.multipoint);
    f.B("rage.forceSafePoint", s.rage.forceSafePoint);
    f.B("rage.ignoreLimbs", s.rage.ignoreLimbs);
    f.B("rage.ignoreInvisible", s.rage.ignoreInvisible);
    f.B("rage.preferArmor", s.rage.preferArmor);
    f.B("rage.doubleTap", s.rage.doubleTap);
    f.I("rage.doubleTapKey", s.rage.doubleTapKey);
    f.B("rage.hideShots", s.rage.hideShots);
    f.B("rage.aaOnShot", s.rage.aaOnShot);
    f.B("rage.betweenShots", s.rage.betweenShots);
    f.B("rage.autoFire", s.rage.autoFire);
    // legit
    f.B("legit.enable", s.legit.enable);
    f.I("legit.key", s.legit.key);
    f.I("legit.hitbox", s.legit.hitbox);
    f.I("legit.priority", s.legit.priority);
    f.F("legit.fov", s.legit.fov);
    f.F("legit.smooth", s.legit.smooth);
    f.F("legit.hitchance", s.legit.hitchance);
    f.B("legit.triggerbot", s.legit.triggerbot);
    f.F("legit.triggerDelay", s.legit.triggerDelay);
    f.F("legit.triggerHitchance", s.legit.triggerHitchance);
    f.B("legit.backtrack", s.legit.backtrackLegit);
    f.B("legit.autoScope", s.legit.autoScope);
    f.B("legit.autoStop", s.legit.autoStop);
    // anti-aim
    f.B("aa.enable", s.aa.enable);
    f.I("aa.pitch", s.aa.pitch);
    f.I("aa.yawBase", s.aa.yawBase);
    f.I("aa.yawAdd", s.aa.yawAdd);
    f.F("aa.yawJitter", s.aa.yawJitter);
    f.I("aa.jitterMode", s.aa.jitterMode);
    f.F("aa.bodyYaw", s.aa.bodyYaw);
    f.F("aa.desync", s.aa.desync);
    f.I("aa.desyncMode", s.aa.desyncMode);
    f.B("aa.fakeLag", s.aa.fakeLag);
    f.I("aa.fakeLagMode", s.aa.fakeLagMode);
    f.I("aa.fakeLagTicks", s.aa.fakeLagTicks);
    f.B("aa.fakeDuck", s.aa.fakeDuck);
    f.I("aa.fakeDuckKey", s.aa.fakeDuckKey);
    f.B("aa.fakePeek", s.aa.fakePeek);
    f.I("aa.fakePeekKey", s.aa.fakePeekKey);
    f.B("aa.airStuck", s.aa.airStuck);
    f.I("aa.airStuckKey", s.aa.airStuckKey);
    f.B("aa.slowWalk", s.aa.slowWalk);
    f.I("aa.slowWalkKey", s.aa.slowWalkKey);
    // visuals
    f.B("vis.enable", s.vis.enable);
    f.B("vis.box", s.vis.box);
    f.B("vis.boxOutline", s.vis.boxOutline);
    f.B("vis.filled", s.vis.filled);
    f.B("vis.name", s.vis.name);
    f.B("vis.health", s.vis.health);
    f.B("vis.healthText", s.vis.healthText);
    // vis.armor 已移除（护甲条不再绘制）
    f.B("vis.weapon", s.vis.weapon);
    f.B("vis.distance", s.vis.distance);
    f.B("vis.skeleton", s.vis.skeleton);
    f.B("vis.snapline", s.vis.snapline);
    f.B("vis.ammo", s.vis.ammo);
    f.B("vis.flags", s.vis.flags);
    f.B("vis.visibleOnly", s.vis.visibleOnly);
    f.B("vis.teamCheck", s.vis.teamCheck);
    f.I("vis.boxStyle", s.vis.boxStyle);
    f.F("vis.maxDistance", s.vis.maxDistance);
    f.F("vis.fillAlpha", s.vis.fillAlpha);
    f.F4("vis.colBox", s.vis.colBox);
    f.F4("vis.colVisible", s.vis.colVisible);
    f.F4("vis.colFill", s.vis.colFill);
    f.F4("vis.colText", s.vis.colText);
    f.F4("vis.colSkeleton", s.vis.colSkeleton);
    f.B("vis.worldEnable", s.vis.worldEnable);
    f.B("vis.nightMode", s.vis.nightMode);
    f.B("vis.removeScope", s.vis.removeScope);
    f.B("vis.removeSmoke", s.vis.removeSmoke);
    f.B("vis.radarHack", s.vis.radarHack);
    f.F("vis.radarScale", s.vis.radarScale);
    f.B("vis.bulletImpacts", s.vis.bulletImpacts);
    f.F("vis.fovOverride", s.vis.fovOverride);
    f.I("vis.fovKey", s.vis.fovKey);
    f.I("vis.aspectRatio", s.vis.aspectRatio);
    f.B("vis.chams", s.vis.chams);
    f.B("vis.thirdPerson", s.vis.thirdPerson);
    f.I("vis.thirdPersonKey", s.vis.thirdPersonKey);
    f.I("vis.chamsMaterial", s.vis.chamsMaterial);
    f.F4("vis.chamsColor", s.vis.chamsColor);
    f.B("vis.glow", s.vis.glow);
    f.F4("vis.glowColor", s.vis.glowColor);
    f.B("vis.previewAnim", s.vis.previewAnim);
    f.B("vis.bombTimer", s.vis.bombTimer);
    f.I("vis.previewTeam", s.vis.previewTeam);
    // misc
    f.B("misc.bhop", s.misc.bhop);
    f.I("misc.bhopKey", s.misc.bhopKey);
    f.B("misc.autoStrafe", s.misc.autoStrafe);
    f.I("misc.strafeMode", s.misc.strafeMode);
    f.B("misc.edgeJump", s.misc.edgeJump);
    f.I("misc.edgeJumpKey", s.misc.edgeJumpKey);
    f.B("misc.jumpBug", s.misc.jumpBug);
    f.B("misc.autoPeek", s.misc.autoPeek);
    f.I("misc.autoPeekKey", s.misc.autoPeekKey);
    f.B("misc.autoAccept", s.misc.autoAccept);
    f.B("misc.revealRanks", s.misc.revealRanks);
    f.B("misc.muteEnemy", s.misc.muteEnemy);
    f.B("misc.antiAfk", s.misc.antiAfk);
    f.B("misc.noFlash", s.misc.noFlash);
    f.B("misc.autoPistol", s.misc.autoPistol);
    f.B("misc.knifeBot", s.misc.knifeBot);
    f.I("misc.knifeMode", s.misc.knifeMode);
    f.B("misc.clantag", s.misc.clantag);
    f.S("misc.clantagText", s.misc.clantagText, sizeof(s.misc.clantagText));
    f.B("misc.chatSpam", s.misc.chatSpam);
    f.S("misc.chatText", s.misc.chatText, sizeof(s.misc.chatText));
    f.F("misc.chatDelay", s.misc.chatDelay);
}
} // namespace
std::string ConfigDirectory()
{
    const std::string dir = DataDirectory() + "\\nl_configs";
    CreateDirectoryA(dir.c_str(), nullptr);
    return dir;
}
bool ConfigSave(const Settings& s, const char* name)
{
    if (!name || !*name)
        return false;
    Ini f;
    f.writing = true;
    Settings copy = s;
    Serialize(copy, f);
    const std::string path = ConfigDirectory() + "\\" + name + ".ini";
    FILE* fp = nullptr;
    if (fopen_s(&fp, path.c_str(), "wb") != 0 || !fp)
        return false;
    for (auto& kv : f.items)
        fprintf(fp, "%s=%s\n", kv.first.c_str(), kv.second.c_str());
    fclose(fp);
    return true;
}
bool ConfigLoad(Settings& s, const char* name)
{
    if (!name || !*name)
        return false;
    const std::string path = ConfigDirectory() + "\\" + name + ".ini";
    FILE* fp = nullptr;
    if (fopen_s(&fp, path.c_str(), "rb") != 0 || !fp)
        return false;
    Ini f;
    f.writing = false;
    char line[1024];
    while (fgets(line, sizeof(line), fp))
    {
        std::string l(line);
        while (!l.empty() && (l.back() == '\n' || l.back() == '\r'))
            l.pop_back();
        const size_t eq = l.find('=');
        if (eq == std::string::npos || eq == 0)
            continue;
        f.values[l.substr(0, eq)] = l.substr(eq + 1);
    }
    fclose(fp);
    Serialize(s, f);
    return true;
}
bool ConfigDelete(const char* name)
{
    if (!name || !*name)
        return false;
    const std::string path = ConfigDirectory() + "\\" + name + ".ini";
    return DeleteFileA(path.c_str()) != 0;
}
int ConfigList(char names[][64], int maxCount)
{
    int count = 0;
    WIN32_FIND_DATAA fd;
    const std::string pattern = ConfigDirectory() + "\\*.ini";
    HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    do
    {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;
        std::string n = fd.cFileName;
        const size_t dot = n.find_last_of('.');
        if (dot != std::string::npos)
            n = n.substr(0, dot);
        if (n == "last")
            continue;
        snprintf(names[count], 64, "%s", n.c_str());
        if (++count >= maxCount)
            break;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return count;
}
bool ConfigLoadLast(Settings& s) { return ConfigLoad(s, "last"); }
void ConfigSaveLast(const Settings& s) { ConfigSave(s, "last"); }
void SettingsDefaults(Settings& s)
{
    s = Settings();
}
}


