#pragma once
#include <windows.h>
#include <cstdint>
#include <vector>

// 虚函数表钩子：直接改写 vtable 条目（可逆），支持同一实例挂多个索引
// 不修改被调用函数的机器码，卸载时把原始指针写回即可，可反复挂/卸
namespace nl {

class VmtHook {
public:
    VmtHook() = default;
    ~VmtHook() { RemoveAll(); }
    VmtHook(const VmtHook&) = delete;
    VmtHook& operator=(const VmtHook&) = delete;

    // instance: 对象指针；index: 虚表下标；hook: 我们的函数；originalOut: 原始函数（可选）
    bool  Install(void* instance, int index, void* hook, void** originalOut = nullptr);
    bool  Remove(int index);
    void  RemoveAll();

    bool  IsHooked(int index) const;
    void* Original(int index) const;
    void* Target() const { return m_vtable; }
    int   Count() const { return (int)m_entries.size(); }

private:
    struct Entry {
        int   index = 0;
        void* hook = nullptr;      // 我们写进去的函数（卸载时要确认虚表项还是它）
        void* original = nullptr;
    };
    void*  m_vtable = nullptr;
    void*  m_instance = nullptr;
    std::vector<Entry> m_entries;
};
}
