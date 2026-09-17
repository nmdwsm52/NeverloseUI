#pragma once
#include <imgui.h>

namespace nl { namespace icons {

enum Icon {
    Icon_None = 0,
    Icon_Target, Icon_Crosshair, Icon_Skull, Icon_Eye, Icon_Sliders, Icon_Code, Icon_Gear,
    Icon_Shield, Icon_Box, Icon_Heart, Icon_Text, Icon_Bone, Icon_Palette, Icon_Key, Icon_User,
    Icon_Search, Icon_ChevronDown, Icon_ChevronRight, Icon_Check, Icon_Close, Icon_Plus,
    Icon_Planet, Icon_Zap, Icon_Skate, Icon_Wand, Icon_Power, Icon_Info, Icon_Dot, Icon_Speed,
    Icon_Cross, Icon_Earth, Icon_Music, Icon_Trash, Icon_Save, Icon_Folder, Icon_Refresh
};

// 以 c 为中心、s 为边长绘制矢量图标（不依赖任何图标字体）
void Draw(ImDrawList* dl, Icon icon, const ImVec2& c, float s, ImU32 col, float thickness = 0.0f);

} }
