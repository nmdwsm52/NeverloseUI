#pragma once
#include <imgui.h>
#include <imgui_internal.h>

// Neverlose 风格主题 / 字体 / 缩放
namespace nl {

struct Palette {
    ImU32 bg           = IM_COL32(10, 10, 14, 255);
    ImU32 bgTop        = IM_COL32(24, 20, 40, 255);
    ImU32 sidebar      = IM_COL32(13, 13, 18, 255);
    ImU32 group        = IM_COL32(18, 18, 24, 240);
    ImU32 groupHeader  = IM_COL32(255, 255, 255, 6);
    ImU32 groupBorder  = IM_COL32(255, 255, 255, 12);
    ImU32 widget       = IM_COL32(30, 30, 38, 255);
    ImU32 widgetHover  = IM_COL32(38, 38, 48, 255);
    ImU32 widgetActive = IM_COL32(45, 45, 57, 255);
    ImU32 text         = IM_COL32(234, 234, 241, 255);
    ImU32 textDim      = IM_COL32(132, 132, 150, 255);
    ImU32 textFaint    = IM_COL32(88, 88, 104, 255);
    ImU32 separator    = IM_COL32(255, 255, 255, 12);
    ImU32 popup        = IM_COL32(16, 16, 22, 253);
    ImU32 track        = IM_COL32(255, 255, 255, 16);
    ImU32 danger       = IM_COL32(255, 98, 122, 255);
    ImU32 success      = IM_COL32(122, 231, 168, 255);
    ImU32 warn         = IM_COL32(255, 196, 108, 255);
};

extern Palette pal;
extern float   g_scale;      // DPI * 用户缩放
extern bool    g_colorKeyMode;   // 渲染器使用颜色键透明（此时避免外发光等半透明叠加）
extern ImU32   g_accent;
extern ImU32   g_accent2;

struct Fonts {
    ImFont* regular  = nullptr;
    ImFont* semibold = nullptr;
    ImFont* smallText = nullptr;
    ImFont* tinyText  = nullptr;
};
extern Fonts font;

// 布局常量（已乘缩放）
inline float S(float v) { return v * g_scale; }

float FontBody();
float FontSmall();
float FontTitle();
float FontBig();

void SetAccent(const float c[4], const float c2[4]);
void ApplyTheme(const float accent[4], const float accent2[4]);   // 从配置色推导 palette 中的强调色
void SetupStyle(float scale);              // ImGuiStyle + 字体，可在运行时重载
const char* PresetName(int idx);
ImU32 PresetAccent(int idx, float alpha = 1.0f);
ImU32 PresetAccent2(int idx, float alpha = 1.0f);
int   PresetCount();

// 颜色工具
ImU32 Col(float r, float g, float b, float a = 1.0f);
ImU32 ColA(const float c[4], float mul = 1.0f);
ImU32 ColAlpha(ImU32 c, float a);
ImU32 ColAlphaMul(ImU32 c, float m);
ImU32 ColMix(ImU32 a, ImU32 b, float t);
ImU32 ColLighten(ImU32 c, float f);
ImVec4 ToVec4(ImU32 c);

// 动画
float Tween(ImGuiID id, float target, float speed = 16.0f, float init = 0.0f);
float EaseOutCubic(float t);
float EaseOutQuint(float t);
float EaseInOutSine(float t);

} // namespace nl
