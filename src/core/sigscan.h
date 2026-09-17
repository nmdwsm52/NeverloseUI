#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>
namespace nl {

struct SigByte {
    uint8_t value = 0;
    bool    wildcard = false;
};

// 支持 "48 8B 05 ?? ?? ?? ?? 48 85 C0" / "488B05????????4885C0" 两种写法
std::vector<SigByte> ParseSignature(const char* pattern);
uintptr_t ScanBufferForSignature(const uint8_t* data, size_t size, const std::vector<SigByte>& sig, size_t start = 0);
// 返回首个匹配地址（外部进程模式下按块读取），未命中返回 0
uintptr_t ScanForSignature(const class Memory& mem, uintptr_t base, size_t size, const char* pattern, size_t chunkSize = 0x100000);
}
