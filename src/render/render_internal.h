#pragma once
#include <windows.h>
#include <d3d11.h>

// 注入模式渲染：直接用游戏自己的设备/交换链，在 Present 前把界面画进后缓冲
namespace nl {
struct Settings;

bool  RenderInternalInit(IDXGISwapChain* swapChain, HWND hwnd);
void  RenderInternalShutdown();
void  RenderInternalOnResize(IDXGISwapChain* swapChain);
void  RenderInternalFrame(Settings& s, bool menuVisible, float dt);
bool  RenderInternalWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
bool  RenderInternalReady();
ID3D11Device*        RenderInternalDevice();
ID3D11DeviceContext* RenderInternalContext();
IDXGISwapChain*      RenderInternalSwapChain();

// 性能遥测：Present 钩子每帧把"整钩子耗时 / 帧间隔 / 游戏自己 Present 的耗时"喂进来，
// 每 120 帧在日志里汇总一行 [perf]（NL_PERF=0 可关掉）
void  RenderInternalPerfNoteHook(double hookMs, double frameDtMs, double gamePresentMs,
                                 const void* swapChain, unsigned long threadId);
}
