#pragma once
#include "features/config.h"
namespace nl { namespace ui {
struct PanelRect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};
void DrawMenu(Settings& s, float dt);
PanelRect GetMenuPanelRect();
bool MenuCloseRequested();
bool ConsumeRestyleRequest();
void LogPush(const char* fmt, ...);
}}
