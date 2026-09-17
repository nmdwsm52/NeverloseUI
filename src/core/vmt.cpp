#include "core/vmt.h"
#include "core/log.h"
#include <cstring>

namespace nl {

namespace {
bool Protect(void* address, size_t size, DWORD protect)
{
    DWORD old = 0;
    return VirtualProtect(address, size, protect, &old) != FALSE;
}
}

bool VmtHook::Install(void* instance, int index, void* hook, void** originalOut)
{
    if (instance == nullptr || hook == nullptr || index < 0)
        return false;
    void** vtable = *reinterpret_cast<void***>(instance);
    if (vtable == nullptr)
        return false;
    if (m_vtable == nullptr)
    {
        m_vtable = vtable;
        m_instance = instance;
    }
    else if (m_vtable != vtable)
    {
        FileLog("[vmt] 实例的虚表与已挂钩的不一致，拒绝挂载");
        return false;
    }

    void* original = vtable[index];
    if (original == hook)
        return true;   // 已经是我们自己的函数
    if (originalOut != nullptr)
        *originalOut = original;

    if (!Protect(&vtable[index], sizeof(void*), PAGE_EXECUTE_READWRITE))
    {
        FileLog("[vmt] VirtualProtect 失败 index=%d", index);
        return false;
    }
    vtable[index] = hook;
    Protect(&vtable[index], sizeof(void*), PAGE_EXECUTE_READ);

    for (auto& e : m_entries)
    {
        if (e.index == index)
        {
            e.hook = hook;
            e.original = original;
            return true;
        }
    }
    Entry entry;
    entry.index = index;
    entry.hook = hook;
    entry.original = original;
    m_entries.push_back(entry);
    return true;
}

// 只有虚表项还是我们自己写进去的那个函数时才还原：
// 如果期间又有别的实例在外面叠了一层钩子，直接写回会把它摘掉，
// 甚至留下指向"已经清理过的模块"的野指针（实测能把游戏带崩）。
bool VmtHook::Remove(int index)
{
    if (m_vtable == nullptr)
        return false;
    void** vtable = reinterpret_cast<void**>(m_vtable);
    for (size_t i = 0; i < m_entries.size(); ++i)
    {
        if (m_entries[i].index != index)
            continue;
        if (vtable[index] != m_entries[i].hook)
        {
            FileLog("[vmt] index=%d 的虚表项已不是本实例（被别的钩子叠了一层），跳过还原", index);
        }
        else if (Protect(&vtable[index], sizeof(void*), PAGE_EXECUTE_READWRITE))
        {
            vtable[index] = m_entries[i].original;
            Protect(&vtable[index], sizeof(void*), PAGE_EXECUTE_READ);
        }
        m_entries.erase(m_entries.begin() + (ptrdiff_t)i);
        return true;
    }
    return false;
}

void VmtHook::RemoveAll()
{
    if (m_vtable == nullptr)
        return;
    void** vtable = reinterpret_cast<void**>(m_vtable);
    for (auto& e : m_entries)
    {
        if (vtable[e.index] != e.hook)
        {
            FileLog("[vmt] index=%d 的虚表项已不是本实例，跳过还原（避免摘掉别人的钩子/留下野指针）", e.index);
            continue;
        }
        if (Protect(&vtable[e.index], sizeof(void*), PAGE_EXECUTE_READWRITE))
        {
            vtable[e.index] = e.original;
            Protect(&vtable[e.index], sizeof(void*), PAGE_EXECUTE_READ);
        }
    }
    m_entries.clear();
    m_vtable = nullptr;
    m_instance = nullptr;
}

bool VmtHook::IsHooked(int index) const
{
    for (const auto& e : m_entries)
        if (e.index == index)
            return true;
    return false;
}

void* VmtHook::Original(int index) const
{
    for (const auto& e : m_entries)
        if (e.index == index)
            return e.original;
    return nullptr;
}
}
