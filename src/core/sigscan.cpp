#include "core/sigscan.h"
#include "core/memory.h"
#include <cstdio>
#include <cstring>

namespace nl {

namespace {
int HexValue(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
}

std::vector<SigByte> ParseSignature(const char* pattern)
{
    std::vector<SigByte> out;
    if (pattern == nullptr)
        return out;
    std::string compact;
    compact.reserve(strlen(pattern));
    for (const char* p = pattern; *p; ++p)
    {
        const char c = *p;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            continue;
        if (c == '?' || c == '*')
        {
            compact.push_back('?');
            continue;
        }
        if (HexValue(c) >= 0)
            compact.push_back(c);
    }
    for (size_t i = 0; i < compact.size();)
    {
        if (compact[i] == '?')
        {
            // "?" 或 "??" 都算一个通配字节
            SigByte b;
            b.wildcard = true;
            out.push_back(b);
            i += (i + 1 < compact.size() && compact[i + 1] == '?') ? 2 : 1;
            continue;
        }
        if (i + 1 >= compact.size())
            break;
        const int hi = HexValue(compact[i]);
        const int lo = HexValue(compact[i + 1]);
        if (hi < 0 || lo < 0)
            break;
        SigByte b;
        b.value = (uint8_t)((hi << 4) | lo);
        out.push_back(b);
        i += 2;
    }
    return out;
}

uintptr_t ScanBufferForSignature(const uint8_t* data, size_t size, const std::vector<SigByte>& sig, size_t start)
{
    if (data == nullptr || sig.empty() || size < sig.size())
        return 0;
    const size_t last = size - sig.size();
    for (size_t i = start; i <= last; ++i)
    {
        size_t j = 0;
        for (; j < sig.size(); ++j)
        {
            if (!sig[j].wildcard && data[i + j] != sig[j].value)
                break;
        }
        if (j == sig.size())
            return (uintptr_t)(data + i);
    }
    return 0;
}

uintptr_t ScanForSignature(const Memory& mem, uintptr_t base, size_t size, const char* pattern, size_t chunkSize)
{
    const std::vector<SigByte> sig = ParseSignature(pattern);
    if (sig.empty() || base == 0 || size == 0)
        return 0;
    if (chunkSize < sig.size() * 4)
        chunkSize = sig.size() * 4;
    std::vector<uint8_t> buf(chunkSize + sig.size());
    for (uintptr_t offset = 0; offset + sig.size() < size; offset += chunkSize)
    {
        const size_t remain = size - offset;
        const size_t want = (chunkSize + sig.size() < remain) ? (chunkSize + sig.size()) : remain;
        if (!mem.ReadRaw(base + offset, buf.data(), want))
            continue;
        const uintptr_t hit = ScanBufferForSignature(buf.data(), want, sig);
        if (hit != 0)
            return base + offset + (hit - (uintptr_t)buf.data());
    }
    return 0;
}
}
