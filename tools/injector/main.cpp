// -----------------------------------------------------------------------------
// NLInjector —— 标准 LoadLibrary 注入器（CreateRemoteThread 方式）
//   用法: NLInjector.exe <进程名|PID> [dll路径]
//   不写内核驱动、不改游戏文件，注入后热键由 DLL 自己处理：
//     INSERT 菜单   F6 安全卸载 DLL（进程不受影响）   F7 由宿主程序 FreeLibrary
// -----------------------------------------------------------------------------
#include <windows.h>
#include <tlhelp32.h>
#include <cstdio>
#include <cstring>
#include <string>

namespace {
DWORD FindPid(const char* name)
{
    if (name == nullptr || *name == 0)
        return 0;
    char* end = nullptr;
    const unsigned long asNumber = strtoul(name, &end, 10);
    if (end != name && *end == 0)
        return (DWORD)asNumber;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return 0;
    DWORD pid = 0;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe))
    {
        do
        {
            char exe[MAX_PATH] = { 0 };
            WideCharToMultiByte(CP_UTF8, 0, pe.szExeFile, -1, exe, MAX_PATH - 1, nullptr, nullptr);
            if (_stricmp(exe, name) == 0)
            {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

bool EnableDebugPrivilege()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
        return false;
    TOKEN_PRIVILEGES tp = {};
    tp.PrivilegeCount = 1;
    LookupPrivilegeValueW(nullptr, L"SeDebugPrivilege", &tp.Privileges[0].Luid);
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    const bool ok = AdjustTokenPrivileges(token, FALSE, &tp, sizeof(tp), nullptr, nullptr) != FALSE;
    CloseHandle(token);
    return ok;
}
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        printf("usage: NLInjector.exe <process name|pid> [dll path]\n");
        return 1;
    }
    const char* target = argv[1];
    const char* dllArg = (argc > 2) ? argv[2] : "NeverloseUI.dll";

    char dllPath[MAX_PATH] = { 0 };
    if (GetFullPathNameA(dllArg, MAX_PATH, dllPath, nullptr) == 0)
    {
        printf("[x] dll 路径无效\n");
        return 1;
    }
    if (GetFileAttributesA(dllPath) == INVALID_FILE_ATTRIBUTES)
    {
        printf("[x] 找不到 %s\n", dllPath);
        return 1;
    }

    EnableDebugPrivilege();
    const DWORD pid = FindPid(target);
    if (pid == 0)
    {
        printf("[x] 找不到进程 %s\n", target);
        return 1;
    }
    HANDLE proc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (proc == nullptr)
    {
        printf("[x] OpenProcess 失败 (err=%lu)，试试管理员运行\n", (unsigned long)GetLastError());
        return 1;
    }
    const size_t bytes = strlen(dllPath) + 1;
    void* remote = VirtualAllocEx(proc, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (remote == nullptr)
    {
        printf("[x] VirtualAllocEx 失败 (err=%lu)\n", (unsigned long)GetLastError());
        CloseHandle(proc);
        return 1;
    }
    if (!WriteProcessMemory(proc, remote, dllPath, bytes, nullptr))
    {
        printf("[x] WriteProcessMemory 失败 (err=%lu)\n", (unsigned long)GetLastError());
        VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
        CloseHandle(proc);
        return 1;
    }
    const HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    const FARPROC loadLibrary = GetProcAddress(kernel32, "LoadLibraryA");
    HANDLE thread = CreateRemoteThread(proc, nullptr, 0, (LPTHREAD_START_ROUTINE)loadLibrary, remote, 0, nullptr);
    if (thread == nullptr)
    {
        printf("[x] CreateRemoteThread 失败 (err=%lu)\n", (unsigned long)GetLastError());
        VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
        CloseHandle(proc);
        return 1;
    }
    WaitForSingleObject(thread, 8000);
    DWORD exitCode = 0;
    GetExitCodeThread(thread, &exitCode);
    CloseHandle(thread);
    VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
    CloseHandle(proc);

    if (exitCode == 0)
    {
        printf("[x] LoadLibrary 在目标进程内返回 0（DLL 可能被架构/依赖问题拒绝）\n");
        return 1;
    }
    printf("[ok] 已注入 pid=%lu  dll=%s\n", (unsigned long)pid, dllPath);
    printf("     INSERT 菜单 / F6 安全卸载 / ESC 关菜单\n");
    return 0;
}
