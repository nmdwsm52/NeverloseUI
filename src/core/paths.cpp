#include "core/paths.h"
#include <windows.h>
#include <vector>

namespace nl {
namespace {

// 目录是否存在
bool DirExists(const std::string& dir)
{
    if (dir.empty())
        return false;
    const DWORD attr = GetFileAttributesA(dir.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

std::string DirOf(const std::string& file)
{
    const size_t slash = file.find_last_of("\\/");
    return (slash == std::string::npos) ? std::string() : file.substr(0, slash);
}

// 从某个模块句柄/地址取目录；失败返回空串
std::string DirectoryFromModule(HMODULE mod)
{
    char path[MAX_PATH] = { 0 };
    if (GetModuleFileNameA(mod, path, MAX_PATH) == 0)
        return std::string();
    return DirOf(std::string(path));
}

} // namespace

std::string ModuleDirectory()
{
    HMODULE mod = nullptr;
    // 通过本函数地址反查所属模块：EXE 与 LoadLibrary 注入的 DLL 都适用；
    // 手动映射的镜像不在模块链表里，这里会失败
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCSTR>(&ModuleDirectory), &mod) || mod == nullptr)
        return std::string(".");
    const std::string dir = DirectoryFromModule(mod);
    return dir.empty() ? std::string(".") : dir;
}

std::string HostDirectory()
{
    const std::string dir = DirectoryFromModule(nullptr);   // 主 EXE
    return dir.empty() ? std::string(".") : dir;
}

std::string DataDirectory()
{
    // 1) 本模块目录：正常 EXE / LoadLibrary 注入时和以前完全一样
    //    注意 "." 是"查不到模块"的占位值（GetFileAttributes(".") 会成功），不能当真
    const std::string moduleDir = ModuleDirectory();
    if (moduleDir != "." && DirExists(moduleDir))
        return moduleDir;

    // 2) 手动映射：模块不在链表里，退回用户级固定目录（还能顺便避开只读的游戏目录）
    char buf[MAX_PATH] = { 0 };
    if (GetEnvironmentVariableA("LOCALAPPDATA", buf, MAX_PATH) > 0)
    {
        const std::string dir = std::string(buf) + "\\NeverloseUI";
        CreateDirectoryA(dir.c_str(), nullptr);
        if (DirExists(dir))
            return dir;
    }

    // 3) 最后退回主 EXE 目录
    const std::string host = HostDirectory();
    return DirExists(host) ? host : std::string(".");
}

std::string ResolveDataFile(const char* fileName)
{
    std::vector<std::string> candidates;
    candidates.push_back(ModuleDirectory());

    char buf[MAX_PATH] = { 0 };
    if (GetEnvironmentVariableA("LOCALAPPDATA", buf, MAX_PATH) > 0)
        candidates.push_back(std::string(buf) + "\\NeverloseUI");

    candidates.push_back(HostDirectory());

    char cwd[MAX_PATH] = { 0 };
    if (GetCurrentDirectoryA(MAX_PATH, cwd) > 0)
        candidates.push_back(cwd);

    for (const std::string& dir : candidates)
    {
        if (dir.empty() || dir == ".")
            continue;
        const std::string full = dir + "\\" + fileName;
        if (GetFileAttributesA(full.c_str()) != INVALID_FILE_ATTRIBUTES)
            return full;
    }
    // 都没找到：返回"本模块/数据目录"下的路径，方便报错时看出找的是哪
    return DataDirectory() + "\\" + fileName;
}

bool IsLoaderModule()
{
    HMODULE mod = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCSTR>(&IsLoaderModule), &mod) || mod == nullptr)
        return false;
    return !DirectoryFromModule(mod).empty();
}

bool SwitchFlag(const char* name)
{
    if (name == nullptr || *name == 0)
        return false;
    static std::vector<std::string> enabled;
    static bool loaded = false;
    if (!loaded)
    {
        loaded = true;
        const std::string path = DataDirectory() + "\\nl_switch.ini";
        FILE* fp = nullptr;
        if (fopen_s(&fp, path.c_str(), "r") == 0 && fp != nullptr)
        {
            char line[256] = { 0 };
            while (fgets(line, sizeof(line), fp) != nullptr)
            {
                std::string s(line);
                const size_t hash = s.find_first_of("#;");
                if (hash != std::string::npos)
                    s = s.substr(0, hash);
                const size_t eq = s.find('=');
                if (eq == std::string::npos)
                    continue;
                std::string key = s.substr(0, eq);
                std::string val = s.substr(eq + 1);
                auto trim = [](std::string& t)
                {
                    while (!t.empty() && (t.front() == ' ' || t.front() == '\t' || t.front() == '\r' || t.front() == '\n'))
                        t.erase(t.begin());
                    while (!t.empty() && (t.back() == ' ' || t.back() == '\t' || t.back() == '\r' || t.back() == '\n'))
                        t.pop_back();
                };
                trim(key);
                trim(val);
                if (!key.empty() && val != "0" && !val.empty())
                    enabled.push_back(key);
            }
            fclose(fp);
        }
    }
    for (const std::string& s : enabled)
    {
        if (s == name)
            return true;
    }
    return false;
}

int SwitchInt(const char* name, int def)
{
    if (name == nullptr || *name == 0)
        return def;
    static std::vector<std::pair<std::string, std::string>> values;
    static bool loaded = false;
    if (!loaded)
    {
        loaded = true;
        const std::string path = DataDirectory() + "\\nl_switch.ini";
        FILE* fp = nullptr;
        if (fopen_s(&fp, path.c_str(), "r") == 0 && fp != nullptr)
        {
            char line[256] = { 0 };
            while (fgets(line, sizeof(line), fp) != nullptr)
            {
                std::string s(line);
                const size_t hash = s.find_first_of("#;");
                if (hash != std::string::npos)
                    s = s.substr(0, hash);
                const size_t eq = s.find('=');
                if (eq == std::string::npos)
                    continue;
                std::string key = s.substr(0, eq);
                std::string val = s.substr(eq + 1);
                auto trim = [](std::string& t)
                {
                    while (!t.empty() && (t.front() == ' ' || t.front() == '\t' || t.front() == '\r' || t.front() == '\n'))
                        t.erase(t.begin());
                    while (!t.empty() && (t.back() == ' ' || t.back() == '\t' || t.back() == '\r' || t.back() == '\n'))
                        t.pop_back();
                };
                trim(key);
                trim(val);
                if (!key.empty())
                    values.emplace_back(key, val);
            }
            fclose(fp);
        }
    }
    for (const auto& kv : values)
    {
        if (kv.first == name)
            return atoi(kv.second.c_str());
    }
    return def;
}
}
