#pragma once
#include <imgui.h>
#include "ui/icons.h"

// Neverlose 风格自绘控件集
namespace nl { namespace ui {

// 单栏布局游标
struct Column {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float bottom = 0.0f;
};

// ---------- 布局 ----------
void   BeginGroup(Column& col, const char* id, const char* title, icons::Icon icon, bool* headerToggle = nullptr);
void   EndGroup(Column& col);
void   JumpTo(Column& col, float y);
float  RowWidth();
float  RowH(bool hint);
float  TextWidth(const char* text, float size = 0.0f, bool semibold = false);
float  SmallTextWidth(const char* text);
float  GroupPad();
void   SetFilter(const char* text);
void   Spacer(float h);

// ---------- 控件 ----------
bool   Toggle(const char* label, bool* v, const char* hint = nullptr);
bool   Checkbox(const char* label, bool* v, float width = 0.0f, const char* hint = nullptr);
bool   SliderFloat(const char* label, float* v, float mn, float mx, const char* fmt = "%.1f", const char* hint = nullptr, float step = 0.0f);
bool   SliderInt(const char* label, int* v, int mn, int mx, const char* hint = nullptr);
bool   Combo(const char* label, int* idx, const char* const* items, int count, const char* hint = nullptr);
bool   Keybind(const char* label, int* key, const char* hint = nullptr);
bool   ColorEdit(const char* label, float col[4], const char* hint = nullptr);
bool   Button(const char* label, const ImVec2& size = ImVec2(0, 0), bool accent = false, icons::Icon icon = icons::Icon_None);
bool   IconButton(const char* id, icons::Icon icon, float box = 24.0f, const char* tip = nullptr, bool accent = false, bool active = false);
bool   InputText(const char* id, char* buf, int bufSize, const char* placeholder = nullptr, float width = 0.0f, const char* label = nullptr);
void   LabelRow(const char* label, const char* hint = nullptr);
void   Separator(const char* label = nullptr);
bool   TabBar(const char* id, const char* const* items, int count, int* current, float width = 0.0f);
void   Badge(const char* text, ImU32 col, bool centered = false);
void   TextSmall(const char* text, ImU32 col, bool centered = false);
void   TextNormal(const char* text, ImU32 col, bool semibold = false);

} }
