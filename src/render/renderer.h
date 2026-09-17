#pragma once
#include <windows.h>
#include <d3d11.h>
namespace nl {
bool RenderInit(HWND hwnd);
void RenderShutdown();
void RenderNewFrame();
void RenderPresent();
void RenderResize(int width, int height);
bool RenderIsTransparent();
bool RenderUsesColorKey();
const char* RenderModeName();
COLORREF RenderColorKey();
ID3D11Device* RenderDevice();
ID3D11DeviceContext* RenderContext();
}
