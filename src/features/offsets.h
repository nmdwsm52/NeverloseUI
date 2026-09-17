#pragma once
#include <cstdint>
#include <string>

namespace nl {

enum class SlotKind {
    ModuleOffset,   // 模块基址 + 该偏移，例如 dwEntityList
    NetVar,         // 类字段偏移，例如 m_iHealth
    Constant        // 结构性常量，例如实体步长、骨骼数量
};

struct OffsetSlot {
    const char* name = "";
    const char* group = "";
    const char* description = "";
    SlotKind    kind = SlotKind::ModuleOffset;
    uintptr_t   value = 0;
    bool        filled = false;      // 是否已经填过（手工粘贴或特征码命中）
    bool        required = true;     // 必需项：缺了对应功能直接报错
    const char* signature = nullptr; // 可选特征码，留空表示只能手工提供
    int         sigDisp = 0;         // 特征码命中后还需要加的字节数（指针字段 / RIP 相对）
    uintptr_t   defaultValue = 0;    // 常量默认值
};

int         OffsetSlotCount();
OffsetSlot& OffsetSlotAt(int index);
OffsetSlot* FindOffsetSlot(const char* name);

void        OffsetsReset();
bool        OffsetsLoad();
bool        OffsetsSave();
int         OffsetsFilledCount();
int         OffsetsRequiredMissing();

// 解析外部粘贴的偏移文本，返回成功填写的槽位数量
int         OffsetsApplyPaste(const char* text, std::string* report);
// 生成待填模板（含中文说明注释），用于交给用户逐项补全
std::string OffsetsExportTemplate();
std::string OffsetsDumpFilled();
// 用特征码解析（外部/内部进程都可用）
int         OffsetsResolveBySignature(const class Memory& mem, uintptr_t moduleBase, size_t moduleSize, std::string* report);

const char* SlotKindName(SlotKind kind);
}
