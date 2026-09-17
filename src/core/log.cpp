#include "core/log.h"
#include "core/paths.h"
#include <cstdio>
#include <cstdarg>
#include <string>
namespace nl {
namespace {
void Write(const char* mode, const char* fmt, va_list ap)
{
    // 手动映射（内核驱动注入）时模块不在链表里，DataDirectory() 会退回 %LOCALAPPDATA%\NeverloseUI
    const std::string path = DataDirectory() + "\\nl_boot.log";
    FILE* fp = nullptr;
    if (fopen_s(&fp, path.c_str(), mode) != 0 || fp == nullptr)
        return;
    vfprintf(fp, fmt, ap);
    fputc('\n', fp);
    fclose(fp);
}
}
void FileLog(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    Write("a", fmt, ap);
    va_end(ap);
}
void FileLogReset()
{
    const std::string path = DataDirectory() + "\\nl_boot.log";
    FILE* fp = nullptr;
    if (fopen_s(&fp, path.c_str(), "w") == 0 && fp != nullptr)
        fclose(fp);
}
}
