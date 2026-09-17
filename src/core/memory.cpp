#include "core/memory.h"
#include "core/log.h"
#include <tlhelp32.h>
#include <psapi.h>
#include <algorithm>
#include <cstring>

namespace nl {

Memory g_mem;

namespace {
std::vector<std::string> g_processes;

// 内部模式的读取必须抗崩溃：偏移链里拿到垃圾指针时不能把游戏带崩
// 单独放一个没有 C++ 析构对象的函数里，才能安全使用 __try/__except
bool SafeMemcpy(void* dst, uintptr_t src, size_t size)
{
    __try
    {
        memcpy(dst, reinterpret_cast<const void*>(src), size);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// 写内存同样要抗崩溃（内部模式直接解引用）
bool SafeWriteMemcpy(uintptr_t dst, const void* src, size_t size)
{
    __try
    {
        memcpy(reinterpret_cast<void*>(dst), src, size);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

std::string ToLower(const std::string& s)
{
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(), [](unsigned char c) { return (char)tolower(c); });
    return r;
}
}

void Memory::RefreshProcesses()
{
    g_processes.clear();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe))
    {
        do
        {
            char name[MAX_PATH] = { 0 };
            WideCharToMultiByte(CP_UTF8, 0, pe.szExeFile, -1, name, MAX_PATH - 1, nullptr, nullptr);
            g_processes.emplace_back(name);
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    std::sort(g_processes.begin(), g_processes.end());
    g_processes.erase(std::unique(g_processes.begin(), g_processes.end()), g_processes.end());
}

const std::vector<std::string>& Memory::Processes()
{
    if (g_processes.empty())
        RefreshProcesses();
    return g_processes;
}

DWORD Memory::FindProcessId(const char* processName)
{
    if (processName == nullptr || *processName == 0)
        return 0;
    const std::string want = ToLower(processName);
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
            char name[MAX_PATH] = { 0 };
            WideCharToMultiByte(CP_UTF8, 0, pe.szExeFile, -1, name, MAX_PATH - 1, nullptr, nullptr);
            if (ToLower(name) == want)
            {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

bool Memory::ProcessExists(const char* processName)
{
    return FindProcessId(processName) != 0;
}

bool Memory::Open(const char* processName)
{
    Close();
    // 内部模式：目标就是我们自己
    if (processName != nullptr && _stricmp(processName, "self") == 0)
    {
        m_mode = MemMode::Internal;
        m_pid = GetCurrentProcessId();
        m_name = "self";
        m_attached = true;
        return true;
    }
    const DWORD pid = FindProcessId(processName);
    if (pid == 0)
    {
        FileLog("[mem] process not found: %s", processName ? processName : "(null)");
        return false;
    }
    HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (h == nullptr)
    {
        // 提权后重试
        h = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    }
    if (h == nullptr)
    {
        FileLog("[mem] OpenProcess failed pid=%lu err=%lu", (unsigned long)pid, (unsigned long)GetLastError());
        return false;
    }
    m_handle = h;
    m_pid = pid;
    m_mode = MemMode::External;
    m_name = processName ? processName : "";
    m_attached = true;
    FileLog("[mem] attached %s pid=%lu", m_name.c_str(), (unsigned long)pid);
    return true;
}

void Memory::Close()
{
    if (m_handle)
    {
        CloseHandle(m_handle);
        m_handle = nullptr;
    }
    m_attached = false;
    m_pid = 0;
    m_lastModule.clear();
    m_lastModuleBase = 0;
    m_lastModuleSize = 0;
    for (int i = 0; i < kPageCacheSlots; ++i)
        m_pageCache[i].valid = false;
}

size_t Memory::ModuleSize(const char* moduleName) const
{
    if (m_mode == MemMode::Internal)
    {
        HMODULE mod = GetModuleHandleA(moduleName);
        if (mod == nullptr)
            return 0;
        MODULEINFO info{};
        if (GetModuleInformation(GetCurrentProcess(), mod, &info, sizeof(info)))
            return info.SizeOfImage;
        return 0;
    }
    ModuleBase(moduleName);
    return m_lastModuleSize;
}

uintptr_t Memory::ModuleBase(const char* moduleName) const
{
    const std::string want = ToLower(moduleName ? moduleName : "");
    if (m_lastModule == want && m_lastModuleBase != 0)
        return m_lastModuleBase;
    if (m_mode == MemMode::Internal)
    {
        HMODULE mod = GetModuleHandleA(moduleName);
        m_lastModule = want;
        m_lastModuleBase = (uintptr_t)mod;
        return m_lastModuleBase;
    }
    if (!m_attached)
        return 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, m_pid);
    if (snap == INVALID_HANDLE_VALUE)
        return 0;
    MODULEENTRY32W me{};
    me.dwSize = sizeof(me);
    uintptr_t base = 0;
    if (Module32FirstW(snap, &me))
    {
        do
        {
            char name[MAX_PATH] = { 0 };
            WideCharToMultiByte(CP_UTF8, 0, me.szModule, -1, name, MAX_PATH - 1, nullptr, nullptr);
            if (ToLower(name) == want)
            {
                base = (uintptr_t)me.modBaseAddr;
                m_lastModuleSize = me.modBaseSize;
                break;
            }
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
    m_lastModule = want;
    m_lastModuleBase = base;
    return base;
}

bool Memory::ReadRaw(uintptr_t address, void* out, size_t size) const
{
    if (!m_attached || address == 0 || out == nullptr || size == 0)
        return false;
    if (m_mode == MemMode::Internal)
    {
        const bool ok = SafeMemcpy(out, address, size);
        if (!ok)
        {
            ++m_failedReads;
            // 这一页读失败：把页缓存标成不可读，后面的检查就不用再去踩异常
            const uintptr_t pageNo = address >> 12;
            PageCacheSlot& slot = m_pageCache[pageNo % (uintptr_t)kPageCacheSlots];
            slot.valid = true;
            slot.page = pageNo;
            slot.readable = false;
        }
        return ok;
    }
    SIZE_T read = 0;
    const bool ok = ReadProcessMemory(m_handle, (LPCVOID)address, out, size, &read) && read == size;
    if (!ok)
        ++m_failedReads;
    return ok;
}

// 写内存：内部模式直接写（SEH 兜底），外部模式走 WriteProcessMemory。
// 注意外部模式的 OpenProcess 只申请了读权限，写入会失败——需要写的功能请用注入版。
bool Memory::WriteRaw(uintptr_t address, const void* data, size_t size) const
{
    if (!m_attached || address == 0 || data == nullptr || size == 0)
        return false;
    if (m_mode == MemMode::Internal)
    {
        const bool ok = SafeWriteMemcpy(address, data, size);
        if (!ok)
            ++m_failedWrites;
        return ok;
    }
    SIZE_T written = 0;
    const bool ok = WriteProcessMemory(m_handle, (LPVOID)address, data, size, &written) && written == size;
    if (!ok)
        ++m_failedWrites;
    return ok;
}

std::vector<uint8_t> Memory::ReadBytes(uintptr_t address, size_t size) const
{
    std::vector<uint8_t> buf(size, 0);
    if (!ReadRaw(address, buf.data(), size))
        buf.clear();
    return buf;
}

std::string Memory::ReadString(uintptr_t address, size_t maxLength) const
{
    if (address == 0)
        return std::string();
    char buf[256] = { 0 };
    const size_t n = maxLength < sizeof(buf) - 1 ? maxLength : sizeof(buf) - 1;
    if (!ReadRaw(address, buf, n))
        return std::string();
    buf[n] = 0;
    return std::string(buf);
}

uintptr_t Memory::ReadPtrChain(uintptr_t base, const int* offsets, size_t count) const
{
    uintptr_t addr = base;
    for (size_t i = 0; i < count; ++i)
    {
        if (addr == 0)
            return 0;
        addr = Read<uintptr_t>(addr + offsets[i]);
    }
    return addr;
}

bool Memory::IsValid(uintptr_t address) const
{
    if (!m_attached || address < 0x10000 || address > 0x00007FFFFFFFFFFFULL)
        return false;
    uint8_t probe = 0;
    return ReadRaw(address, &probe, 1);
}

bool Memory::IsValidRange(uintptr_t address, size_t size) const
{
    if (!m_attached || address < 0x10000 || address > 0x00007FFFFFFFFFFFULL || size == 0)
        return false;
    if (m_mode == MemMode::External)
        return IsValid(address);
    // 内部模式：**不调用 VirtualQuery**，改成"按页探针 + 页级缓存"。
    //   实测：在被内核驱动手动映射、VAD 被融合过的进程里，VirtualQuery 单次要 5~30us，
    //   一帧上千次检查直接把帧时间打到 100ms 以上（5fps）。
    //   探针读失败由 SafeMemcpy 的 SEH 兜住，一页只探一次，之后走缓存。
    const uintptr_t lastPage = (address + size - 1) & ~(uintptr_t)0xFFF;
    for (uintptr_t page = address & ~(uintptr_t)0xFFF; page <= lastPage; page += 0x1000)
    {
        const uintptr_t pageNo = page >> 12;
        PageCacheSlot& slot = m_pageCache[pageNo % (uintptr_t)kPageCacheSlots];
        if (!slot.valid || slot.page != pageNo)
        {
            uint8_t probe = 0;
            const bool ok = SafeMemcpy(&probe, page, 1);
            slot.valid = true;
            slot.page = pageNo;
            slot.readable = ok;
        }
        if (!slot.readable)
            return false;
    }
    return true;
}
}
