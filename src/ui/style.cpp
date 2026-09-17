#include "ui/style.h"

#include <cmath>
#include <filesystem>

namespace nl {

Palette pal;
float   g_scale   = 1.0f;
bool    g_colorKeyMode = false;
ImU32   g_accent  = IM_COL32(166, 122, 255, 255);
ImU32   g_accent2 = IM_COL32(95, 139, 255, 255);
Fonts   font;

namespace {

struct PresetDef { const char* name; float a[3]; float b[3]; };

const PresetDef kPresets[] = {
    { "Nebula",  { 0.651f, 0.478f, 1.000f }, { 0.373f, 0.545f, 1.000f } },
    { "Violet",  { 0.486f, 0.361f, 1.000f }, { 0.620f, 0.310f, 0.960f } },
    { "Azure",   { 0.345f, 0.769f, 1.000f }, { 0.110f, 0.522f, 1.000f } },
    { "Blossom", { 1.000f, 0.431f, 0.616f }, { 1.000f, 0.667f, 0.471f } },
    { "Mint",    { 0.341f, 0.890f, 0.608f }, { 0.235f, 0.745f, 1.000f } },
    { "Amber",   { 1.000f, 0.718f, 0.302f }, { 1.000f, 0.431f, 0.471f } },
    { "Crimson", { 1.000f, 0.302f, 0.427f }, { 1.000f, 0.549f, 0.353f } },
    { "Mono",    { 0.910f, 0.910f, 0.945f }, { 0.600f, 0.600f, 0.660f } },
};

const char* kFontCandidates[2][3] = {
    { "C:\\Windows\\Fonts\\segoeui.ttf",  "C:\\Windows\\Fonts\\tahoma.ttf",   "C:\\Windows\\Fonts\\verdana.ttf"  },
    { "C:\\Windows\\Fonts\\seguisb.ttf",  "C:\\Windows\\Fonts\\segoeuib.ttf",  "C:\\Windows\\Fonts\\tahomabd.ttf" },
};
// 中文回退字体：微软雅黑 / 等线 / 黑体 / 宋体
const char* kCjkCandidates[6] = {
    "C:\\Windows\\Fonts\\msyh.ttc", "C:\\Windows\\Fonts\\Deng.ttf", "C:\\Windows\\Fonts\\simhei.ttf",
    "C:\\Windows\\Fonts\\msyhbd.ttc", "C:\\Windows\\Fonts\\msyhl.ttc", "C:\\Windows\\Fonts\\simsun.ttc",
};

const char* FindFontFile(int which)
{
    for (int i = 0; i < 3; ++i)
    {
        std::error_code ec;
        if (std::filesystem::exists(kFontCandidates[which][i], ec))
            return kFontCandidates[which][i];
    }
    return nullptr;
}
const char* FindCjkFont()
{
    for (int i = 0; i < 6; ++i)
    {
        std::error_code ec;
        if (std::filesystem::exists(kCjkCandidates[i], ec))
            return kCjkCandidates[i];
    }
    return nullptr;
}

} // namespace

float FontBody()  { return 13.5f * g_scale; }
float FontSmall() { return 11.0f * g_scale; }
float FontTitle() { return 15.0f * g_scale; }
float FontBig()   { return 19.5f * g_scale; }

int   PresetCount() { return (int)(sizeof(kPresets) / sizeof(kPresets[0])); }
const char* PresetName(int idx) { return kPresets[ImClamp(idx, 0, PresetCount() - 1)].name; }

ImU32 PresetAccent(int idx, float alpha)
{
    const PresetDef& p = kPresets[ImClamp(idx, 0, PresetCount() - 1)];
    return Col(p.a[0], p.a[1], p.a[2], alpha);
}

ImU32 PresetAccent2(int idx, float alpha)
{
    const PresetDef& p = kPresets[ImClamp(idx, 0, PresetCount() - 1)];
    return Col(p.b[0], p.b[1], p.b[2], alpha);
}

ImU32 Col(float r, float g, float b, float a)
{
    return IM_COL32((int)(r * 255.0f + 0.5f), (int)(g * 255.0f + 0.5f), (int)(b * 255.0f + 0.5f), (int)(a * 255.0f + 0.5f));
}

ImU32 ColA(const float c[4], float mul)
{
    return Col(c[0], c[1], c[2], c[3] * mul);
}

ImU32 ColAlpha(ImU32 c, float a)
{
    const int alpha = ImClamp((int)(a * 255.0f + 0.5f), 0, 255);
    return (c & 0x00FFFFFF) | ((ImU32)alpha << 24);
}

ImU32 ColAlphaMul(ImU32 c, float m)
{
    const int a = (int)(((c >> IM_COL32_A_SHIFT) & 0xFF) * m);
    return (c & 0x00FFFFFF) | ((ImU32)ImClamp(a, 0, 255) << 24);
}

ImU32 ColMix(ImU32 a, ImU32 b, float t)
{
    t = ImClamp(t, 0.0f, 1.0f);
    const int a0 = (a >> 0) & 0xFF, a1 = (a >> 8) & 0xFF, a2 = (a >> 16) & 0xFF, a3 = (a >> 24) & 0xFF;
    const int b0 = (b >> 0) & 0xFF, b1 = (b >> 8) & 0xFF, b2 = (b >> 16) & 0xFF, b3 = (b >> 24) & 0xFF;
    return IM_COL32(
        (int)(a0 + (b0 - a0) * t), (int)(a1 + (b1 - a1) * t),
        (int)(a2 + (b2 - a2) * t), (int)(a3 + (b3 - a3) * t));
}

ImU32 ColLighten(ImU32 c, float f)
{
    return ColMix(c, IM_COL32(255, 255, 255, (c >> 24) & 0xFF), f);
}

ImVec4 ToVec4(ImU32 c)
{
    return ImGui::ColorConvertU32ToFloat4(c);
}

void SetAccent(const float c[4], const float c2[4])
{
    g_accent  = ColA(c);
    g_accent2 = ColA(c2);
    g_accent  = ColAlpha(g_accent, 1.0f);
    g_accent2 = ColAlpha(g_accent2, 1.0f);
}

void ApplyTheme(const float accent[4], const float accent2[4])
{
    SetAccent(accent, accent2);
    // 用强调色给窗口顶部一点极淡的染色
    pal.bgTop = Col(13.0f + accent[0] * 14.0f, 12.0f + accent[1] * 12.0f, 18.0f + accent[2] * 18.0f, 255.0f);
}

float Tween(ImGuiID id, float target, float speed, float init)
{
    ImGuiStorage* storage = ImGui::GetStateStorage();
    float v = storage->GetFloat(id, init);
    const float dt = ImClamp(ImGui::GetIO().DeltaTime, 0.0f, 1.0f / 20.0f);
    const float k = 1.0f - expf(-speed * dt);
    v += (target - v) * k;
    if (fabsf(target - v) < 0.0005f)
        v = target;
    storage->SetFloat(id, v);
    return v;
}

float EaseOutCubic(float t)  { t = ImClamp(t, 0.0f, 1.0f); const float u = 1.0f - t; return 1.0f - u * u * u; }
float EaseOutQuint(float t)  { t = ImClamp(t, 0.0f, 1.0f); const float u = 1.0f - t; return 1.0f - u * u * u * u * u; }
float EaseInOutSine(float t) { t = ImClamp(t, 0.0f, 1.0f); return -(cosf(IM_PI * t) - 1.0f) * 0.5f; }

void SetupStyle(float scale)
{
    g_scale = scale;

    ImGuiIO& io = ImGui::GetIO();

    // ---- 字体 ----
    io.Fonts->Clear();
    ImFontConfig cfg;
    cfg.OversampleH = 2;
    cfg.OversampleV = 1;
    cfg.PixelSnapH  = false;

    const char* regularFile  = FindFontFile(0);
    const char* semiboldFile = FindFontFile(1);
    const char* cjkFile      = FindCjkFont();
    const ImWchar* cjkRanges = cjkFile ? io.Fonts->GetGlyphRangesChineseSimplifiedCommon() : nullptr;
    // 把中文字体作为回退合并进当前字体：拉丁字符用 Segoe UI，中文自动落到雅黑，避免出现方块乱码
    auto mergeCjk = [&]()
    {
        if (cjkFile == nullptr)
            return;
        ImFontConfig mc;
        mc.MergeMode   = true;
        mc.OversampleH = 2;
        mc.OversampleV = 1;
        mc.PixelSnapH  = false;
        io.Fonts->AddFontFromFileTTF(cjkFile, 16.0f * scale, &mc, cjkRanges);
    };

    if (regularFile)
        font.regular = io.Fonts->AddFontFromFileTTF(regularFile, 16.0f * scale, &cfg);
    if (font.regular)
        mergeCjk();
    if (semiboldFile)
        font.semibold = io.Fonts->AddFontFromFileTTF(semiboldFile, 16.0f * scale, &cfg);
    if (font.semibold && font.semibold != font.regular)
        mergeCjk();

    if (!font.regular)
    {
        font.regular = io.Fonts->AddFontDefault();
        mergeCjk();
    }
    if (!font.semibold)
        font.semibold = font.regular;
    font.smallText = font.regular;
    font.tinyText  = font.regular;
    io.FontDefault = font.regular;

    // ---- 样式 ----
    ImGuiStyle& st = ImGui::GetStyle();
    st = ImGuiStyle();

    st.WindowPadding      = ImVec2(0, 0);
    st.FramePadding       = ImVec2(8, 4);
    st.ItemSpacing        = ImVec2(8, 4);
    st.ItemInnerSpacing   = ImVec2(6, 4);
    st.CellPadding        = ImVec2(4, 3);
    st.ScrollbarSize      = 6.0f;
    st.GrabMinSize        = 10.0f;
    st.WindowBorderSize   = 0.0f;
    st.ChildBorderSize    = 0.0f;
    st.PopupBorderSize    = 1.0f;
    st.FrameBorderSize    = 0.0f;
    st.WindowRounding     = 0.0f;
    st.ChildRounding      = 0.0f;
    st.FrameRounding      = 6.0f;
    st.PopupRounding      = 8.0f;
    st.ScrollbarRounding  = 8.0f;
    st.GrabRounding       = 6.0f;
    st.TabRounding        = 6.0f;
    st.WindowTitleAlign   = ImVec2(0.0f, 0.5f);
    st.WindowMenuButtonPosition = ImGuiDir_None;
    st.SeparatorTextBorderSize = 1.0f;
    st.AntiAliasedLines   = true;
    st.AntiAliasedLinesUseTex = true;
    st.AntiAliasedFill    = true;
    st.CircleTessellationMaxError = 0.16f;

    ImVec4* c = st.Colors;
    c[ImGuiCol_Text]                  = ToVec4(pal.text);
    c[ImGuiCol_TextDisabled]          = ToVec4(pal.textDim);
    c[ImGuiCol_WindowBg]              = ToVec4(pal.bg);
    c[ImGuiCol_ChildBg]               = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg]               = ToVec4(pal.popup);
    c[ImGuiCol_Border]                = ToVec4(pal.groupBorder);
    c[ImGuiCol_BorderShadow]          = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg]               = ToVec4(pal.widget);
    c[ImGuiCol_FrameBgHovered]        = ToVec4(pal.widgetHover);
    c[ImGuiCol_FrameBgActive]         = ToVec4(pal.widgetActive);
    c[ImGuiCol_TitleBg]               = ToVec4(pal.sidebar);
    c[ImGuiCol_TitleBgActive]         = ToVec4(pal.sidebar);
    c[ImGuiCol_TitleBgCollapsed]      = ToVec4(pal.sidebar);
    c[ImGuiCol_MenuBarBg]             = ToVec4(pal.sidebar);
    c[ImGuiCol_ScrollbarBg]           = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab]         = ToVec4(ColAlpha(pal.text, 0.14f));
    c[ImGuiCol_ScrollbarGrabHovered]  = ToVec4(ColAlpha(pal.text, 0.24f));
    c[ImGuiCol_ScrollbarGrabActive]   = ToVec4(g_accent);
    c[ImGuiCol_CheckMark]             = ToVec4(IM_COL32(255, 255, 255, 255));
    c[ImGuiCol_SliderGrab]            = ToVec4(g_accent);
    c[ImGuiCol_SliderGrabActive]      = ToVec4(g_accent2);
    c[ImGuiCol_Button]                = ToVec4(pal.widget);
    c[ImGuiCol_ButtonHovered]         = ToVec4(pal.widgetHover);
    c[ImGuiCol_ButtonActive]          = ToVec4(g_accent);
    c[ImGuiCol_Header]                = ToVec4(ColAlpha(g_accent, 0.22f));
    c[ImGuiCol_HeaderHovered]         = ToVec4(ColAlpha(g_accent, 0.30f));
    c[ImGuiCol_HeaderActive]          = ToVec4(ColAlpha(g_accent, 0.42f));
    c[ImGuiCol_Separator]             = ToVec4(pal.separator);
    c[ImGuiCol_SeparatorHovered]      = ToVec4(ColAlpha(g_accent, 0.50f));
    c[ImGuiCol_SeparatorActive]       = ToVec4(g_accent);
    c[ImGuiCol_ResizeGrip]            = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ResizeGripHovered]     = ToVec4(ColAlpha(g_accent, 0.35f));
    c[ImGuiCol_ResizeGripActive]      = ToVec4(g_accent);
    c[ImGuiCol_PlotLines]             = ToVec4(g_accent);
    c[ImGuiCol_PlotLinesHovered]      = ToVec4(g_accent2);
    c[ImGuiCol_PlotHistogram]         = ToVec4(g_accent);
    c[ImGuiCol_PlotHistogramHovered]  = ToVec4(g_accent2);
    c[ImGuiCol_TextSelectedBg]        = ToVec4(ColAlpha(g_accent, 0.35f));
    c[ImGuiCol_DragDropTarget]        = ToVec4(g_accent);
    c[ImGuiCol_NavHighlight]          = ToVec4(ColAlpha(g_accent, 0.60f));
    c[ImGuiCol_NavWindowingHighlight] = ToVec4(ColAlpha(g_accent, 0.70f));
    c[ImGuiCol_NavWindowingDimBg]     = ImVec4(0, 0, 0, 0.35f);
    c[ImGuiCol_ModalWindowDimBg]      = ImVec4(0, 0, 0, 0.45f);

    st.ScaleAllSizes(scale);
}

} // namespace nl
