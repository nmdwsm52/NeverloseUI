#pragma once
// 简易启动日志：写入 exe 同目录的 nl_boot.log，便于排查初始化问题
namespace nl {
void FileLog(const char* fmt, ...);
void FileLogReset();
}
