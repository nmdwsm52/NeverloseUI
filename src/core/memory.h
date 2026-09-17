#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>

// 内存访问层：外部（读取别的进程）与内部（代码自身被注入时直接解引用）两种模式共用同一套接口
namespace nl {

enum class MemMode { External, Internal };

class Memory {
public:
    bool Open(const char* processName);
    void Close();
    bool Attached() const { return m_attached; }
    MemMode Mode() const { return m_mode; }
    DWORD Pid() const { return m_pid; }
    const std::string& ProcessName() const { return m_name; }
    // 内部模式下读取失败次数（指针链不对时的关键诊断数据）
    unsigned long long FailedReads() const { return m_failedReads; }
    void ResetFailedReads() const { m_failedReads = 0; }
    unsigned long long FailedWrites() const { return m_failedWrites; }

    uintptr_t ModuleBase(const char* moduleName) const;
    size_t    ModuleSize(const char* moduleName) const;

    bool ReadRaw(uintptr_t address, void* out, size_t size) const;
    // 写内存（内部模式直接写，外部模式需要写权限的句柄）
    bool WriteRaw(uintptr_t address, const void* data, size_t size) const;
    template <typename T>
    bool Write(uintptr_t address, const T& value) const
    {
        return WriteRaw(address, &value, sizeof(T));
    }
    std::vector<uint8_t> ReadBytes(uintptr_t address, size_t size) const;
    std::string ReadString(uintptr_t address, size_t maxLength = 64) const;

    template <typename T>
    T Read(uintptr_t address) const
    {
        T value{};
        ReadRaw(address, &value, sizeof(T));
        return value;
    }
    template <typename T>
    bool ReadArray(uintptr_t address, T* out, size_t count) const
    {
        return ReadRaw(address, out, sizeof(T) * count);
    }
    // 读取 T 数组式指针链：ReadPtrChain(base, {0x10, 0x20})
    uintptr_t ReadPtrChain(uintptr_t base, const int* offsets, size_t count) const;

    bool IsValid(uintptr_t address) const;
    // 内部模式没有异常保护，扫内存前先用它确认整段可读（VirtualQuery）
    bool IsValidRange(uintptr_t address, size_t size) const;

    static void RefreshProcesses();
    static const std::vector<std::string>& Processes();
    static bool ProcessExists(const char* processName);
    static DWORD FindProcessId(const char* processName);

private:
    HANDLE      m_handle = nullptr;
    DWORD       m_pid = 0;
    bool        m_attached = false;
    MemMode     m_mode = MemMode::External;
    std::string m_name;
    mutable std::string m_lastModule;
    mutable uintptr_t   m_lastModuleBase = 0;
    mutable size_t      m_lastModuleSize = 0;
    mutable unsigned long long m_failedReads = 0;
    mutable unsigned long long m_failedWrites = 0;
    // 内部模式的可读性页表缓存：
    //   1) 连续读同一段内存时不再反复 VirtualQuery；
    //   2) 页粒度（4KB），命中率远高于"记住最后一个 region"；
    //   3) 不可读的页也缓存，避免每次都去踩一次异常。
    //   实测（被内核驱动手动映射、VAD 被融合过的进程里）VirtualQuery 单次约 30us，
    //   一帧上千次检查会把帧时间打到 100ms 以上，这就是注入版掉帧的根因。
    static constexpr int kPageCacheSlots = 1024;   // 必须是 2 的幂
    struct PageCacheSlot {
        uintptr_t page = 0;      // 页号（地址 >> 12），valid 为 true 时才有意义
        bool      valid = false;
        bool      readable = false;
    };
    mutable PageCacheSlot m_pageCache[kPageCacheSlots];
};

extern Memory g_mem;
}
