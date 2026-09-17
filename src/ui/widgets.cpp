#include "ui/widgets.h"
#include <windows.h>
#include "features/bind.h"
#include "ui/style.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <cstdlib>
using namespace ImGui;
namespace nl { namespace ui {
namespace {
float g_rowWidth = 0.0f;
char  g_filter[96] = { 0 };
bool  g_filterOn = false;
struct GroupCtx {
    ImVec2 pos;
    float  width;
    float  headerH;
    float  pad;
    bool*  headerToggle;
};
ImVector<GroupCtx> g_groupStack;
ImVector<float>    g_widthStack;
struct SliderEdit {
    ImGuiID id = 0;
    char    buf[32] = { 0 };
    bool    fresh = false;
};
SliderEdit g_sliderEdit;
ImGuiID g_keybindListening = 0;
inline float Pad()      { return S(12.0f); }
inline float HeaderH()  { return S(34.0f); }
inline float Radius()   { return S(8.0f); }
inline float Gap()      { return S(12.0f); }
bool ContainsCI(const char* hay, const char* needle)
{
    if (!needle || !*needle) return true;
    if (!hay) return false;
    const size_t nl = strlen(needle);
    for (const char* p = hay; *p; ++p)
    {
        size_t i = 0;
        while (i < nl && p[i] && tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i]))
            ++i;
        if (i == nl)
            return true;
    }
    return false;
}
bool RowVisible(const char* label)
{
    if (!g_filterOn || g_filter[0] == 0)
        return true;
    return ContainsCI(label, g_filter);
}
struct Row {
    ImVec2 pos;
    float  width = 0.0f;
    float  height = 0.0f;
    float  ctrlX = 0.0f;
    float  ctrlW = 0.0f;
    bool   hovered = false;
    bool   clicked = false;
    bool   rightClicked = false;   // 右键：给 keybind 之类的控件唤菜单用
};
Row BeginRow(const char* label, float ctrlW, bool withHint)
{
    Row r;
    r.pos     = GetCursorScreenPos();
    r.width   = RowWidth();
    r.height  = RowH(withHint);
    r.ctrlW   = ctrlW > 0.0f ? ctrlW : r.width;
    r.ctrlX   = r.pos.x + r.width - r.ctrlW;
    PushID(label);
    SetCursorScreenPos(r.pos);
    InvisibleButton("##row", ImVec2(r.width, r.height));
    r.hovered = IsItemHovered();
    r.clicked = IsItemClicked();
    r.rightClicked = r.hovered && IsMouseClicked(ImGuiMouseButton_Right);
    return r;
}
void EndRow(const Row& r)
{
    PopID();
    SetCursorScreenPos(ImVec2(r.pos.x, r.pos.y + r.height + S(1.0f)));
}
void RowLabel(const Row& r, const char* label, const char* hint, bool active, bool useColor = false, ImU32 col = 0)
{
    ImDrawList* dl = GetWindowDrawList();
    PushFont(font.semibold, FontBody());
    const float fh = GetFontSize();
    const float ly = hint ? r.pos.y : r.pos.y + (r.height - fh) * 0.5f;
    const ImU32 c = useColor ? col : ColMix(pal.textDim, pal.text, (active || r.hovered) ? 1.0f : 0.58f);
    dl->AddText(ImVec2(r.pos.x, ly), c, label);
    PopFont();
    if (hint)
    {
        PushFont(font.smallText, FontSmall());
        dl->AddText(ImVec2(r.pos.x, r.pos.y + S(14.0f)), pal.textFaint, hint);
        PopFont();
    }
}
void ChevronIcon(ImDrawList* dl, const ImVec2& c, float s, ImU32 col, float th, bool up)
{
    const float sign = up ? -1.0f : 1.0f;
    const ImVec2 pts[3] = {
        ImVec2(c.x - s * 0.5f, c.y - sign * s * 0.22f),
        ImVec2(c.x,            c.y + sign * s * 0.30f),
        ImVec2(c.x + s * 0.5f, c.y - sign * s * 0.22f),
    };
    dl->AddPolyline(pts, 3, col, th, 0);
}
void CheckMark(ImDrawList* dl, const ImVec2& c, float s, ImU32 col, float th)
{
    const ImVec2 pts[3] = {
        ImVec2(c.x - s * 0.42f, c.y + s * 0.02f),
        ImVec2(c.x - s * 0.10f, c.y + s * 0.34f),
        ImVec2(c.x + s * 0.44f, c.y - s * 0.36f),
    };
    dl->AddPolyline(pts, 3, col, th, 0);
}
} // namespace
// ------------------------------------------------------------------ 布局
float GroupPad()  { return Pad(); }
float RowWidth()  { return g_rowWidth > 0.0f ? g_rowWidth : GetContentRegionAvail().x; }
float RowH(bool hint) { return hint ? S(34.0f) : S(24.0f); }
float TextWidth(const char* text, float size, bool semibold)
{
    if (!text) return 0.0f;
    PushFont(semibold ? font.semibold : font.regular, size > 0.0f ? size : FontBody());
    const float w = CalcTextSize(text).x;
    PopFont();
    return w;
}
float SmallTextWidth(const char* text)
{
    if (!text) return 0.0f;
    PushFont(font.smallText, FontSmall());
    const float w = CalcTextSize(text).x;
    PopFont();
    return w;
}
void SetFilter(const char* text)
{
    snprintf(g_filter, sizeof(g_filter), "%s", text ? text : "");
    g_filterOn = (g_filter[0] != 0);
}
void Spacer(float h)
{
    const ImVec2 p = GetCursorScreenPos();
    SetCursorScreenPos(ImVec2(p.x, p.y + h));
}
void JumpTo(Column& col, float y) { col.y = y; }
void BeginGroup(Column& col, const char* id, const char* title, icons::Icon icon, bool* headerToggle)
{
    GroupCtx ctx;
    ctx.pos          = ImVec2(col.x, col.y);
    ctx.width        = col.w;
    ctx.headerH      = HeaderH();
    ctx.pad          = Pad();
    ctx.headerToggle = headerToggle;
    g_groupStack.push_back(ctx);
    g_widthStack.push_back(g_rowWidth);
    PushID(id);
    ImDrawList* dl = GetWindowDrawList();
    dl->ChannelsSplit(2);
    dl->ChannelsSetCurrent(1);
    const ImVec2 p = ctx.pos;
    const float iconS = S(14.0f);
    icons::Draw(dl, icon, ImVec2(p.x + ctx.pad + iconS * 0.5f, p.y + ctx.headerH * 0.5f), iconS, g_accent, S(1.5f));
    PushFont(font.semibold, FontTitle());
    dl->AddText(ImVec2(p.x + ctx.pad + iconS + S(9.0f), p.y + (ctx.headerH - GetFontSize()) * 0.5f - S(0.5f)), pal.text, title);
    PopFont();
    if (headerToggle != nullptr)
    {
        const float tw = S(30.0f), th = S(16.0f);
        ImVec2 mn(p.x + ctx.width - ctx.pad - tw, p.y + (ctx.headerH - th) * 0.5f);
        ImVec2 mx(mn.x + tw, mn.y + th);
        SetCursorScreenPos(mn);
        InvisibleButton("##hdr_toggle", ImVec2(tw, th));
        const bool hov = IsItemHovered();
        if (IsItemClicked())
            *headerToggle = !*headerToggle;
        const float t  = Tween(GetID("##hdr_t"), *headerToggle ? 1.0f : 0.0f, 18.0f);
        const float hv = Tween(GetID("##hdr_h"), hov ? 1.0f : 0.0f, 14.0f);
        ImU32 bg = ColMix(pal.widget, g_accent, t);
        bg = ColMix(bg, ColLighten(bg, 0.12f), hv * 0.7f);
        if (t > 0.02f)
            dl->AddRectFilled(mn - ImVec2(S(1.5f), S(1.5f)), mx + ImVec2(S(1.5f), S(1.5f)), ColAlpha(g_accent, 0.12f * t), th * 0.5f + S(1.5f));
        dl->AddRectFilled(mn, mx, bg, th * 0.5f);
        const float kr = th * 0.5f - S(2.0f);
        const float kx = ImLerp(mn.x + th * 0.5f, mx.x - th * 0.5f, t);
        dl->AddCircleFilled(ImVec2(kx, (mn.y + mx.y) * 0.5f), kr, ColMix(ColAlpha(pal.text, 0.8f), IM_COL32(255, 255, 255, 255), t));
    }
    g_rowWidth = ctx.width - ctx.pad * 2.0f;
    SetCursorScreenPos(ImVec2(p.x + ctx.pad, p.y + ctx.headerH + S(7.0f)));
}
void EndGroup(Column& col)
{
    if (g_groupStack.empty())
        return;
    const GroupCtx ctx = g_groupStack.back();
    g_groupStack.pop_back();
    if (!g_widthStack.empty())
    {
        g_rowWidth = g_widthStack.back();
        g_widthStack.pop_back();
    }
    const float bottom = GetCursorScreenPos().y;
    float height = bottom - ctx.pos.y + ctx.pad - S(1.0f);
    height = ImMax(height, ctx.headerH + S(44.0f));
    ImDrawList* dl = GetWindowDrawList();
    dl->ChannelsSetCurrent(0);
    const ImVec2 mx(ctx.pos.x + ctx.width, ctx.pos.y + height);
    dl->AddRectFilled(ctx.pos, mx, pal.group, Radius());
    dl->AddRectFilled(ctx.pos, ImVec2(mx.x, ctx.pos.y + ctx.headerH), pal.groupHeader, Radius(), ImDrawFlags_RoundCornersTop);
    dl->AddRect(ctx.pos, mx, pal.groupBorder, Radius(), 1.0f);
    dl->AddLine(ImVec2(ctx.pos.x + ctx.pad * 0.5f, ctx.pos.y + ctx.headerH),
                ImVec2(mx.x - ctx.pad * 0.5f, ctx.pos.y + ctx.headerH), pal.separator);
    dl->AddRectFilledMultiColor(ImVec2(ctx.pos.x + ctx.pad, ctx.pos.y + ctx.headerH + S(1.0f)),
                                ImVec2(mx.x - ctx.pad, ctx.pos.y + ctx.headerH + S(3.0f)),
                                ColAlpha(g_accent, 0.16f), ColAlpha(g_accent, 0.0f),
                                ColAlpha(g_accent, 0.0f), ColAlpha(g_accent, 0.16f));
    dl->ChannelsMerge();
    PopID();
    col.y = ctx.pos.y + height + Gap();
    col.bottom = ImMax(col.bottom, col.y);
    SetCursorScreenPos(ImVec2(col.x, col.y));
}
// ------------------------------------------------------------------ 控件
bool Toggle(const char* label, bool* v, const char* hint)
{
    if (!RowVisible(label))
        return false;
    Row r = BeginRow(label, S(34.0f), hint != nullptr);
    bool changed = false;
    if (r.clicked)
    {
        *v = !*v;
        changed = true;
    }
    ImDrawList* dl = GetWindowDrawList();
    const float t   = Tween(GetID("##t"), *v ? 1.0f : 0.0f, 18.0f);
    const float hov = Tween(GetID("##h"), r.hovered ? 1.0f : 0.0f, 14.0f);
    const float tw = S(34.0f), th = S(18.0f);
    ImVec2 mn(r.pos.x + r.width - tw, r.pos.y + (r.height - th) * 0.5f);
    ImVec2 mx(mn.x + tw, mn.y + th);
    if (t > 0.02f)
        dl->AddRectFilled(mn - ImVec2(S(1.5f), S(1.5f)), mx + ImVec2(S(1.5f), S(1.5f)),
                          ColAlpha(g_accent, 0.13f * t), th * 0.5f + S(1.5f));
    ImU32 bg = ColMix(pal.widget, g_accent, t);
    bg = ColMix(bg, ColLighten(bg, 0.12f), hov * 0.7f);
    dl->AddRectFilled(mn, mx, bg, th * 0.5f);
    dl->AddRect(mn, mx, ColAlpha(g_accent, 0.45f * t + 0.08f * hov), th * 0.5f, S(1.0f));
    const float kr = th * 0.5f - S(2.5f);
    const float kx = ImLerp(mn.x + th * 0.5f, mx.x - th * 0.5f, t);
    const ImVec2 kc(kx, (mn.y + mx.y) * 0.5f);
    dl->AddCircleFilled(kc, kr, ColMix(ColAlpha(pal.text, 0.72f), IM_COL32(255, 255, 255, 255), t));
    if (t > 0.55f)
        CheckMark(dl, kc + ImVec2(0, S(0.3f)), kr * 1.30f, ColAlpha(g_accent, (t - 0.55f) / 0.45f), S(1.4f));
    RowLabel(r, label, hint, *v);
    EndRow(r);
    return changed;
}
bool Checkbox(const char* label, bool* v, float width, const char* hint)
{
    if (!RowVisible(label))
        return false;
    const float box = S(15.0f);
    Row r;
    r.pos    = GetCursorScreenPos();
    r.width  = width > 0.0f ? width : RowWidth();
    r.height = RowH(hint != nullptr);
    PushID(label);
    SetCursorScreenPos(r.pos);
    InvisibleButton("##cb", ImVec2(r.width, r.height));
    r.hovered = IsItemHovered();
    const bool clicked = IsItemClicked();
    if (clicked)
        *v = !*v;
    ImDrawList* dl = GetWindowDrawList();
    const float t   = Tween(GetID("##t"), *v ? 1.0f : 0.0f, 18.0f);
    const float totalH = hint ? S(30.0f) : r.height;
    ImVec2 mn(r.pos.x, r.pos.y + (totalH - box) * 0.5f);
    ImVec2 mx(mn.x + box, mn.y + box);
    dl->AddRectFilled(mn, mx, ColMix(pal.widget, g_accent, t), S(4.0f));
    dl->AddRect(mn, mx, ColMix(pal.groupBorder, ColAlpha(g_accent, 0.7f), t), S(4.0f), S(1.0f));
    if (t > 0.02f)
        CheckMark(dl, ImVec2((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f), box * 1.05f,
                  ColAlpha(IM_COL32(255, 255, 255, 255), ImClamp(t * 1.2f, 0.0f, 1.0f)), S(1.6f));
    PushFont(font.semibold, FontBody());
    const float fh = GetFontSize();
    const ImU32 tc = ColMix(pal.textDim, pal.text, (*v || r.hovered) ? 1.0f : 0.60f);
    dl->AddText(ImVec2(mx.x + S(8.0f), r.pos.y + (totalH - fh) * 0.5f), tc, label);
    PopFont();
    if (hint)
    {
        PushFont(font.smallText, FontSmall());
        dl->AddText(ImVec2(mx.x + S(8.0f), r.pos.y + S(15.0f)), pal.textFaint, hint);
        PopFont();
    }
    PopID();
    SetCursorScreenPos(ImVec2(r.pos.x, r.pos.y + r.height + S(1.0f)));
    return clicked;
}
static bool SliderImpl(const char* label, float* vf, int* vi, float mn, float mx,
                       const char* fmt, const char* hint, bool isInt)
{
    if (!RowVisible(label))
        return false;
    const bool withHint = hint != nullptr;
    Row r = BeginRow(label, 0.0f, withHint);
    bool changed = false;
    const bool active = IsItemActive();
    const bool hovered = r.hovered;
    const float cur = isInt ? (float)*vi : *vf;
    float t = (mx > mn) ? ImClamp((cur - mn) / (mx - mn), 0.0f, 1.0f) : 0.0f;
    const ImGuiID editId = GetID("##edit");
    if (hovered && IsMouseDoubleClicked(ImGuiMouseButton_Left))
    {
        g_sliderEdit.id = editId;
        g_sliderEdit.fresh = true;
    }
    const bool editing = (g_sliderEdit.id == editId);
    if (active && !editing)
    {
        const float mouseT = (GetIO().MousePos.x - r.pos.x - S(2.0f)) / ImMax(1.0f, r.width - S(4.0f));
        float nv = mn + ImClamp(mouseT, 0.0f, 1.0f) * (mx - mn);
        if (isInt)
            nv = floorf(nv + 0.5f);
        nv = ImClamp(nv, mn, mx);
        if (nv != cur)
        {
            if (isInt) *vi = (int)nv; else *vf = nv;
            changed = true;
        }
    }
    if (active && !editing)
        SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    ImDrawList* dl = GetWindowDrawList();
    const float hov = Tween(GetID("##h"), (hovered || active) ? 1.0f : 0.0f, 14.0f);
    const float act = Tween(GetID("##a"), active ? 1.0f : 0.0f, 18.0f);
    const float val = isInt ? (float)*vi : *vf;
    if (mx > mn)
        t = ImClamp((val - mn) / (mx - mn), 0.0f, 1.0f);
    char vbuf[64];
    if (isInt)
        snprintf(vbuf, sizeof(vbuf), "%d", *vi);
    else
        snprintf(vbuf, sizeof(vbuf), fmt, *vf);
    PushFont(font.semibold, FontBody());
    const float vw = CalcTextSize(vbuf).x;
    const float vh = GetFontSize();
    PopFont();
    const ImVec2 vpos(r.pos.x + r.width - vw, r.pos.y);
    if (hov > 0.01f || act > 0.01f)
        dl->AddRectFilled(ImVec2(vpos.x - S(7.0f), vpos.y - S(2.0f)),
                          ImVec2(vpos.x + vw + S(7.0f), vpos.y + vh + S(2.0f)),
                          ColAlpha(g_accent, 0.10f * ImMax(hov, act)), S(4.0f));
    const float trackH = S(5.0f);
    const float trackY = r.pos.y + r.height - trackH - S(2.0f);
    const ImVec2 tmn(r.pos.x + S(1.0f), trackY);
    const ImVec2 tmx(r.pos.x + r.width - S(1.0f), trackY + trackH);
    const float fillX = ImLerp(tmn.x, tmx.x, t);
    dl->AddRectFilled(tmn, tmx, pal.track, trackH * 0.5f);
    if (t > 0.002f)
    {
        dl->PushClipRect(ImVec2(tmn.x - S(1.0f), tmn.y - S(3.0f)), ImVec2(fillX, tmx.y + S(3.0f)), true);
        dl->AddRectFilled(tmn, tmx, ColMix(g_accent, g_accent2, 0.35f), trackH * 0.5f);
        dl->PopClipRect();
    }
    if (hov > 0.01f)
        dl->AddRectFilled(tmn, ImVec2(ImMax(fillX, tmn.x + S(2.0f)), tmx.y), ColAlpha(IM_COL32(255, 255, 255, 255), 0.10f * hov), trackH * 0.5f);
    const ImVec2 kc(fillX, trackY + trackH * 0.5f);
    const float kr = S(3.2f) + S(1.2f) * hov + S(0.8f) * act;
    if (t > 0.002f || hov > 0.01f)
    {
        dl->AddCircleFilled(kc, kr + S(1.4f), ColAlpha(g_accent, 0.20f * ImMax(hov, act)), 20);
        dl->AddCircleFilled(kc, kr, IM_COL32(255, 255, 255, 255), 20);
    }
    RowLabel(r, label, hint, changed || active, (hov > 0.5f || act > 0.5f), ColMix(pal.textDim, pal.text, ImMax(hov, act)));
    if (!editing)
    {
        PushFont(font.semibold, FontBody());
        dl->AddText(vpos, ColMix(pal.textDim, pal.text, ImMax(hov * 0.85f, act)), vbuf);
        PopFont();
    }
    if (editing)
    {
        SetCursorScreenPos(ImVec2(vpos.x - S(26.0f), r.pos.y - S(3.0f)));
        SetNextItemWidth(S(76.0f));
        if (g_sliderEdit.fresh)
        {
            if (isInt) snprintf(g_sliderEdit.buf, sizeof(g_sliderEdit.buf), "%d", *vi);
            else       snprintf(g_sliderEdit.buf, sizeof(g_sliderEdit.buf), "%.3f", *vf);
            SetKeyboardFocusHere();
            g_sliderEdit.fresh = false;
        }
        PushStyleColor(ImGuiCol_FrameBg, pal.widgetActive);
        PushStyleColor(ImGuiCol_Border, g_accent);
        PushStyleVar(ImGuiStyleVar_FrameBorderSize, S(1.0f));
        const bool done = ImGui::InputText("##valedit", g_sliderEdit.buf, IM_ARRAYSIZE(g_sliderEdit.buf),
                                    ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        const bool cancel = IsKeyPressed(ImGuiKey_Escape, false);
        const bool lost = IsItemDeactivated();
        PopStyleVar();
        PopStyleColor(2);
        if (done || cancel || lost)
        {
            if (!cancel)
            {
                float nv = (float)atof(g_sliderEdit.buf);
                nv = ImClamp(nv, mn, mx);
                if (isInt) *vi = (int)(nv + 0.5f); else *vf = nv;
                changed = true;
            }
            g_sliderEdit.id = 0;
        }
    }
    EndRow(r);
    return changed;
}
bool SliderFloat(const char* label, float* v, float mn, float mx, const char* fmt, const char* hint, float step)
{
    const bool ch = SliderImpl(label, v, nullptr, mn, mx, fmt ? fmt : "%.1f", hint, false);
    if (ch && step > 0.0f)
        *v = ImClamp(roundf(*v / step) * step, mn, mx);
    return ch;
}
bool SliderInt(const char* label, int* v, int mn, int mx, const char* hint)
{
    return SliderImpl(label, nullptr, v, (float)mn, (float)mx, "%d", hint, true);
}
bool Combo(const char* label, int* idx, const char* const* items, int count, const char* hint)
{
    if (!RowVisible(label))
        return false;
    Row r = BeginRow(label, S(128.0f), hint != nullptr);
    bool changed = false;
    ImDrawList* dl = GetWindowDrawList();
    const float h = S(22.0f);
    const ImVec2 mn(r.ctrlX, r.pos.y + (r.height - h) * 0.5f);
    const ImVec2 mx(mn.x + r.ctrlW, mn.y + h);
    const bool open = IsPopupOpen("##combo", ImGuiPopupFlags_None);
    const float hov = Tween(GetID("##h"), r.hovered ? 1.0f : 0.0f, 14.0f);
    const float op  = Tween(GetID("##o"), open ? 1.0f : 0.0f, 16.0f);
    ImU32 bg = ColMix(pal.widget, pal.widgetHover, ImMax(hov * 0.8f, op * 0.5f));
    dl->AddRectFilled(mn, mx, bg, S(6.0f));
    dl->AddRect(mn, mx, ColAlpha(g_accent, 0.35f * op + 0.10f * hov), S(6.0f), S(1.0f));
    const char* text = (idx != nullptr && *idx >= 0 && *idx < count) ? items[*idx] : "";
    PushFont(font.semibold, FontBody());
    dl->AddText(ImVec2(mn.x + S(9.0f), (mn.y + mx.y) * 0.5f - GetFontSize() * 0.5f),
                ColMix(pal.textDim, pal.text, 0.4f + 0.6f * ImMax(hov, op)), text);
    PopFont();
    ChevronIcon(dl, ImVec2(mx.x - S(11.0f), (mn.y + mx.y) * 0.5f), S(9.0f),
                ColMix(pal.textDim, g_accent, ImMax(hov, op)), S(1.4f), open);
    if (r.clicked)
        OpenPopup("##combo", ImGuiPopupFlags_None);
    if (BeginPopup("##combo", ImGuiWindowFlags_NoMove))
    {
        ImDrawList* pdl = GetWindowDrawList();
        const float w = ImMax(S(152.0f), r.ctrlW);
        PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(6.0f), S(6.0f)));
        PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, S(2.0f)));
        SetNextWindowSize(ImVec2(w + S(12.0f), 0.0f));
        for (int i = 0; i < count; ++i)
        {
            PushID(i);
            const ImVec2 p = GetCursorScreenPos();
            InvisibleButton("##it", ImVec2(w, S(22.0f)));
            const bool ih = IsItemHovered();
            const float it = Tween(GetID("##i"), ih ? 1.0f : 0.0f, 20.0f);
            const bool selected = (idx != nullptr && *idx == i);
            if (it > 0.01f || selected)
                pdl->AddRectFilled(p, ImVec2(p.x + w, p.y + S(22.0f)), ColAlpha(g_accent, 0.10f + 0.12f * it), S(5.0f));
            if (selected)
                pdl->AddRectFilled(ImVec2(p.x + S(2.0f), p.y + S(5.0f)), ImVec2(p.x + S(4.5f), p.y + S(17.0f)), g_accent, S(1.2f));
            PushFont(font.semibold, FontBody());
            pdl->AddText(ImVec2(p.x + S(13.0f) + S(3.0f) * it, p.y + S(11.0f) - GetFontSize() * 0.5f),
                         selected ? g_accent : ColMix(pal.textDim, pal.text, 0.5f + 0.5f * it), items[i]);
            PopFont();
            if (IsItemClicked())
            {
                *idx = i;
                changed = true;
                CloseCurrentPopup();
            }
            PopID();
        }
        PopStyleVar(2);
        EndPopup();
    }
    EndRow(r);
    return changed;
}
bool Keybind(const char* label, int* key, const char* hint)
{
    if (!RowVisible(label))
        return false;
    Row r = BeginRow(label, S(74.0f), hint != nullptr);
    const ImGuiID id = GetID("##kb");
    bool listening = (g_keybindListening == id);
    bool changed = false;

    // 左键：开始/结束监听；右键：弹出模式菜单（Hold / Toggle / Always）
    if (r.clicked)
    {
        g_keybindListening = listening ? 0 : id;
        listening = !listening;
    }
    if (r.rightClicked)
        OpenPopup("##kbmode");

    // 监听时抓 Windows 虚拟键码（不是 ImGui 的按键枚举 —— 运行时用的是 VK，
    // 之前这里存 ImGuiKey，导致 UI 里绑的键运行时对不上，鼠标侧键也被直接跳过）
    if (listening)
    {
        bool got = false;
        int newVk = 0;
        // 鼠标键（含侧键 Mouse4/Mouse5）优先，ImGui 不一定给它们发按键事件
        static const int kMouseVks[] = { VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2 };
        for (int i = 0; i < 5; ++i)
        {
            if (GetAsyncKeyState(kMouseVks[i]) & 0x8000)
            {
                // 左/右键是"点击控件用"的键，避免一按就把自己绑上，跳过它们
                if (kMouseVks[i] == VK_LBUTTON || kMouseVks[i] == VK_RBUTTON)
                    continue;
                newVk = kMouseVks[i];
                got = true;
                break;
            }
        }
        if (!got)
        {
            for (int vk = 8; vk < 256; ++vk)
            {
                if (vk == VK_ESCAPE)
                    continue;     // Esc 特殊处理在后面
                if (GetAsyncKeyState(vk) & 0x8000)
                {
                    newVk = vk;
                    got = true;
                    break;
                }
            }
        }
        if (got)
        {
            *key = BindMake(newVk, BindModeOf(*key));
            g_keybindListening = 0;
            listening = false;
            changed = true;
        }
        else if (GetAsyncKeyState(VK_ESCAPE) & 0x8000)
        {
            *key = BindMake(0, BindModeOf(*key));   // Esc = 清空按键
            g_keybindListening = 0;
            listening = false;
            changed = true;
        }
    }

    // 模式菜单：右键唤出
    if (BeginPopup("##kbmode"))
    {
        const int curMode = BindModeOf(*key);
        const char* modes[3] = { "Hold (按住)", "Toggle (切换)", "Always (常开)" };
        for (int m = 0; m < 3; ++m)
        {
            const bool sel = (curMode == m);
            if (Selectable(modes[m], sel))
            {
                *key = BindMake(BindVk(*key), m);
                changed = true;
                CloseCurrentPopup();
            }
        }
        EndPopup();
    }

    ImDrawList* dl = GetWindowDrawList();
    const float h = S(21.0f);
    const ImVec2 mn(r.ctrlX, r.pos.y + (r.height - h) * 0.5f);
    const ImVec2 mx(mn.x + r.ctrlW, mn.y + h);
    const float pulse = listening ? (0.5f + 0.5f * sinf((float)GetTime() * 6.0f)) : 0.0f;
    const float hov = Tween(GetID("##h"), r.hovered ? 1.0f : 0.0f, 14.0f);
    dl->AddRectFilled(mn, mx, ColMix(pal.widget, pal.widgetHover, ImMax(hov * 0.7f, pulse * 0.4f)), S(5.0f));
    dl->AddRect(mn, mx, ColAlpha(g_accent, listening ? (0.45f + 0.4f * pulse) : 0.10f * hov), S(5.0f), S(1.0f));

    // 显示：按键名 + 模式后缀（T = toggle，A = always）
    const int vk = BindVk(*key);
    const int mode = BindModeOf(*key);
    char text[48] = { 0 };
    if (listening)
        snprintf(text, sizeof(text), "...");
    else
    {
        const char* suffix = (mode == BindMode_Toggle) ? " T" : (mode == BindMode_Always ? " A" : "");
        snprintf(text, sizeof(text), "%s%s", BindVkName(vk), suffix);
    }
    PushFont(font.semibold, FontBody());
    const float tw = CalcTextSize(text).x;
    dl->AddText(ImVec2((mn.x + mx.x) * 0.5f - tw * 0.5f, (mn.y + mx.y) * 0.5f - GetFontSize() * 0.5f),
                listening ? g_accent : ColMix(pal.textDim, pal.text, (vk != 0 || mode == BindMode_Always ? 0.90f : 0.45f) + 0.10f * hov),
                text);
    PopFont();
    RowLabel(r, label, hint, listening);
    EndRow(r);
    return changed;
}
bool ColorEdit(const char* label, float col[4], const char* hint)
{
    if (!RowVisible(label))
        return false;
    Row r = BeginRow(label, S(56.0f), hint != nullptr);
    bool changed = false;
    ImDrawList* dl = GetWindowDrawList();
    const float sw = S(34.0f), sh = S(19.0f);
    const ImVec2 mn(r.ctrlX + (r.ctrlW - sw) * 0.5f, r.pos.y + (r.height - sh) * 0.5f);
    const ImVec2 mx(mn.x + sw, mn.y + sh);
    const float half = sw * 0.5f;
    dl->AddRectFilled(mn, mx, IM_COL32(34, 34, 42, 255), S(5.0f));
    dl->AddRectFilled(mn, ImVec2(mn.x + half, mn.y + sh * 0.5f), IM_COL32(58, 58, 68, 255));
    dl->AddRectFilled(ImVec2(mn.x + half, mn.y + sh * 0.5f), mx, IM_COL32(58, 58, 68, 255));
    const ImU32 cA = Col(col[0], col[1], col[2], col[3]);
    dl->AddRectFilledMultiColor(mn, mx, ColAlpha(cA, 0.0f), cA, cA, ColAlpha(cA, 0.0f));
    dl->AddRect(mn, mx, ColAlpha(pal.text, 0.20f), S(5.0f), S(1.0f));
    const ImGuiID cpId = GetID("##cp");
    static ImGuiID g_cpActive = 0;
    static char    g_cpHex[16] = { 0 };
    if (IsItemClicked())
        OpenPopup("##cp", ImGuiPopupFlags_None);
    if (BeginPopup("##cp", ImGuiWindowFlags_NoMove))
    {
        ImDrawList* pdl = GetWindowDrawList();
        PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(10.0f), S(10.0f)));
        PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(S(6.0f), S(6.0f)));
        SetNextWindowSize(ImVec2(S(250.0f), 0.0f));
        if (g_cpActive != cpId)
        {
            g_cpActive = cpId;
            snprintf(g_cpHex, sizeof(g_cpHex), "%02X%02X%02X%02X",
                     (int)(col[0] * 255.0f + 0.5f), (int)(col[1] * 255.0f + 0.5f),
                     (int)(col[2] * 255.0f + 0.5f), (int)(col[3] * 255.0f + 0.5f));
        }
        if (ColorPicker4("##picker", col, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_PickerHueBar |
                                         ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoLabel |
                                         ImGuiColorEditFlags_AlphaPreviewHalf))
            changed = true;
        static const float kSwatches[8][3] = {
            { 1.00f, 1.00f, 1.00f }, { 0.93f, 0.93f, 0.96f }, { 0.65f, 0.48f, 1.00f }, { 0.37f, 0.55f, 1.00f },
            { 0.34f, 0.89f, 0.61f }, { 1.00f, 0.72f, 0.30f }, { 1.00f, 0.30f, 0.43f }, { 0.16f, 0.16f, 0.20f },
        };
        for (int i = 0; i < 8; ++i)
        {
            PushID(1000 + i);
            const ImVec2 p = GetCursorScreenPos();
            InvisibleButton("##sw", ImVec2(S(24.0f), S(18.0f)));
            const bool hov = IsItemHovered();
            pdl->AddRectFilled(p, ImVec2(p.x + S(24.0f), p.y + S(18.0f)), Col(kSwatches[i][0], kSwatches[i][1], kSwatches[i][2], 1.0f), S(4.0f));
            pdl->AddRect(p, ImVec2(p.x + S(24.0f), p.y + S(18.0f)),
                         hov ? ColAlpha(g_accent, 0.9f) : ColAlpha(pal.text, 0.15f), S(4.0f), S(1.0f));
            if (IsItemClicked())
            {
                col[0] = kSwatches[i][0];
                col[1] = kSwatches[i][1];
                col[2] = kSwatches[i][2];
                changed = true;
            }
            PopID();
            if (i != 7)
                SameLine();
        }
        SetNextItemWidth(S(150.0f));
        PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(8.0f), S(5.0f)));
        if (ImGui::InputText("##hex", g_cpHex, IM_ARRAYSIZE(g_cpHex),
                      ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_EnterReturnsTrue))
        {
            unsigned int rr = 0, gg = 0, bb = 0, aa = 255;
            const int n = sscanf(g_cpHex, "%2x%2x%2x%2x", &rr, &gg, &bb, &aa);
            if (n >= 3)
            {
                col[0] = rr / 255.0f;
                col[1] = gg / 255.0f;
                col[2] = bb / 255.0f;
                if (n >= 4)
                    col[3] = aa / 255.0f;
                changed = true;
            }
        }
        PopStyleVar();
        PopStyleVar(2);
        EndPopup();
    }
    else if (g_cpActive == cpId)
    {
        g_cpActive = 0;
    }
    RowLabel(r, label, hint, changed);
    EndRow(r);
    return changed;
}
bool Button(const char* label, const ImVec2& size, bool accent, icons::Icon icon)
{
    ImVec2 sz = size;
    if (sz.x <= 0.0f) sz.x = RowWidth();
    if (sz.y <= 0.0f) sz.y = S(24.0f);
    const ImVec2 p = GetCursorScreenPos();
    PushID(label);
    SetCursorScreenPos(p);
    InvisibleButton("##btn", sz);
    const bool hov = IsItemHovered();
    const bool act = IsItemActive();
    const bool clicked = IsItemClicked();
    ImDrawList* dl = GetWindowDrawList();
    const float t = Tween(GetID("##h"), hov ? 1.0f : 0.0f, 18.0f);
    const float a = Tween(GetID("##a"), act ? 1.0f : 0.0f, 22.0f);
    const float rad = S(6.0f);
    ImU32 bg;
    if (accent)
    {
        bg = ColMix(g_accent, g_accent2, t * 0.85f);
        bg = ColMix(bg, IM_COL32(255, 255, 255, 255), a * 0.10f);
    }
    else
    {
        bg = ColMix(pal.widget, pal.widgetHover, t);
        bg = ColMix(bg, pal.widgetActive, a * 0.5f);
    }
    const ImVec2 off(0.0f, a * S(1.0f));
    dl->AddRectFilled(p + off, p + sz + off, bg, rad);
    if (!accent)
        dl->AddRect(p + off, p + sz + off, ColAlpha(g_accent, 0.28f * t), rad, S(1.0f));
    else
        dl->AddRectFilledMultiColor(p + off, p + sz + off,
                                    ColAlpha(IM_COL32(255, 255, 255, 255), 0.10f),
                                    ColAlpha(IM_COL32(255, 255, 255, 255), 0.02f),
                                    ColAlpha(IM_COL32(0, 0, 0, 255), 0.06f),
                                    ColAlpha(IM_COL32(0, 0, 0, 255), 0.02f));
    PushFont(font.semibold, FontBody());
    const float tw = CalcTextSize(label).x;
    const float fh = GetFontSize();
    const float iconW = (icon != icons::Icon_None) ? (S(13.0f) + S(7.0f)) : 0.0f;
    const float totalW = tw + iconW;
    const ImVec2 textPos((p.x + sz.x * 0.5f) - totalW * 0.5f, (p.y + sz.y * 0.5f) - fh * 0.5f + a * S(1.0f));
    if (icon != icons::Icon_None)
        icons::Draw(dl, icon, ImVec2(textPos.x + S(6.5f), textPos.y + fh * 0.5f), S(13.0f),
                    accent ? IM_COL32(255, 255, 255, 255) : ColMix(g_accent, pal.text, 0.35f), S(1.4f));
    dl->AddText(ImVec2(textPos.x + iconW, textPos.y), accent ? IM_COL32(255, 255, 255, 255) : pal.text, label);
    PopFont();
    PopID();
    return clicked;
}
bool IconButton(const char* id, icons::Icon icon, float box, const char* tip, bool accent, bool active)
{
    const float sz = box > 0.0f ? box : S(24.0f);
    const ImVec2 p = GetCursorScreenPos();
    PushID(id);
    SetCursorScreenPos(p);
    InvisibleButton("##ibtn", ImVec2(sz, sz));
    const bool hov = IsItemHovered();
    const bool clicked = IsItemClicked();
    ImDrawList* dl = GetWindowDrawList();
    const float t = Tween(GetID("##h"), hov ? 1.0f : 0.0f, 18.0f);
    const float ac = Tween(GetID("##a2"), active ? 1.0f : 0.0f, 16.0f);
    const ImVec2 c(p.x + sz * 0.5f, p.y + sz * 0.5f);
    dl->AddRectFilled(p, p + ImVec2(sz, sz), ColMix(pal.widget, pal.widgetHover, ImMax(t * 0.9f, ac)), S(6.0f));
    if (accent || active)
        dl->AddRect(p, p + ImVec2(sz, sz), ColAlpha(g_accent, 0.35f * ImMax(t, ac)), S(6.0f), S(1.0f));
    icons::Draw(dl, icon, c, sz * 0.52f, ColMix(pal.textDim, pal.text, ImMax(t, ac)), S(1.5f));
    if (tip && hov)
    {
        BeginTooltip();
        TextUnformatted(tip);
        EndTooltip();
    }
    PopID();
    return clicked;
}
bool InputText(const char* id, char* buf, int bufSize, const char* placeholder, float width, const char* label)
{
    const ImVec2 p = GetCursorScreenPos();
    const float w = width > 0.0f ? width : RowWidth();
    const float h = S(24.0f);
    float inputW = w;
    if (label != nullptr)
    {
        ImDrawList* dl = GetWindowDrawList();
        PushFont(font.semibold, FontBody());
        dl->AddText(ImVec2(p.x, p.y + (h - GetFontSize()) * 0.5f), pal.text, label);
        PopFont();
        inputW = w * 0.5f;
    }
    SetCursorScreenPos(ImVec2(p.x + w - inputW, p.y));
    SetNextItemWidth(inputW);
    PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(9.0f), S(5.0f)));
    const bool changed = InputTextWithHint(id, placeholder ? placeholder : "", buf, bufSize);
    PopStyleVar();
    SetCursorScreenPos(ImVec2(p.x, p.y + h + S(1.0f)));
    return changed;
}
void LabelRow(const char* label, const char* hint)
{
    if (!RowVisible(label))
        return;
    ImDrawList* dl = GetWindowDrawList();
    const ImVec2 p = GetCursorScreenPos();
    const float h = hint ? S(30.0f) : S(20.0f);
    PushFont(font.semibold, FontBody());
    dl->AddText(ImVec2(p.x, p.y), pal.text, label);
    PopFont();
    if (hint)
    {
        PushFont(font.smallText, FontSmall());
        dl->AddText(ImVec2(p.x, p.y + S(15.0f)), pal.textFaint, hint);
        PopFont();
    }
    SetCursorScreenPos(ImVec2(p.x, p.y + h));
}
void Separator(const char* label)
{
    ImDrawList* dl = GetWindowDrawList();
    const ImVec2 p = GetCursorScreenPos();
    const float w = RowWidth();
    const float h = S(20.0f);
    if (label)
    {
        PushFont(font.smallText, FontSmall());
        const float lw = CalcTextSize(label).x;
        dl->AddText(ImVec2(p.x, p.y + h * 0.5f - GetFontSize() * 0.5f), pal.textFaint, label);
        PopFont();
        const float x0 = p.x + lw + S(8.0f);
        dl->AddLine(ImVec2(x0, p.y + h * 0.5f), ImVec2(p.x + w, p.y + h * 0.5f), pal.separator);
    }
    else
    {
        dl->AddLine(ImVec2(p.x, p.y + h * 0.5f), ImVec2(p.x + w, p.y + h * 0.5f), pal.separator);
    }
    SetCursorScreenPos(ImVec2(p.x, p.y + h));
}
bool TabBar(const char* id, const char* const* items, int count, int* current, float width)
{
    if (count <= 0)
        return false;
    PushID(id);
    const ImVec2 p = GetCursorScreenPos();
    const float w = width > 0.0f ? width : RowWidth();
    const float h = S(26.0f);
    const float segW = w / (float)count;
    SetCursorScreenPos(p);
    InvisibleButton("##tabs", ImVec2(w, h));
    const bool hov = IsItemHovered();
    int hoverIdx = -1;
    if (hov)
        hoverIdx = ImClamp((int)((GetIO().MousePos.x - p.x) / segW), 0, count - 1);
    bool changed = false;
    if (hov && IsMouseClicked(ImGuiMouseButton_Left) && hoverIdx != *current)
    {
        *current = hoverIdx;
        changed = true;
    }
    ImDrawList* dl = GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), pal.widget, h * 0.5f);
    const float sel = Tween(GetID("##sel"), (float)*current, 14.0f, 0.0f);
    const ImVec2 imin(p.x + sel * segW + S(2.0f), p.y + S(2.0f));
    const ImVec2 imax(imin.x + segW - S(4.0f), p.y + h - S(2.0f));
    dl->AddRectFilled(imin, imax, ColMix(pal.widgetHover, g_accent, 0.78f), (h - S(4.0f)) * 0.5f);
    for (int i = 0; i < count; ++i)
    {
        PushFont(font.semibold, FontBody());
        const float tw = CalcTextSize(items[i]).x;
        const float fh = GetFontSize();
        const float x = p.x + segW * (i + 0.5f) - tw * 0.5f;
        const float y = p.y + h * 0.5f - fh * 0.5f;
        const bool selected = (i == *current);
        const ImU32 col = selected ? IM_COL32(255, 255, 255, 255)
                                   : ColMix(pal.textDim, pal.text, (i == hoverIdx) ? 0.85f : 0.45f);
        dl->AddText(ImVec2(x, y), col, items[i]);
        PopFont();
    }
    PopID();
    SetCursorScreenPos(ImVec2(p.x, p.y + h + S(6.0f)));
    return changed;
}
void Badge(const char* text, ImU32 col, bool centered)
{
    ImDrawList* dl = GetWindowDrawList();
    const ImVec2 p = GetCursorScreenPos();
    PushFont(font.smallText, FontSmall());
    const float tw = CalcTextSize(text).x;
    const float fh = GetFontSize();
    const float w = tw + S(14.0f);
    const float h = S(17.0f);
    const float x = centered ? p.x + (RowWidth() - w) * 0.5f : p.x;
    dl->AddRectFilled(ImVec2(x, p.y), ImVec2(x + w, p.y + h), ColAlpha(col, 0.16f), h * 0.5f);
    dl->AddRect(ImVec2(x, p.y), ImVec2(x + w, p.y + h), ColAlpha(col, 0.35f), h * 0.5f, S(1.0f));
    dl->AddText(ImVec2(x + w * 0.5f - tw * 0.5f, p.y + h * 0.5f - fh * 0.5f), col, text);
    PopFont();
    SetCursorScreenPos(ImVec2(p.x, p.y + h));
}
void TextSmall(const char* text, ImU32 col, bool centered)
{
    ImDrawList* dl = GetWindowDrawList();
    const ImVec2 p = GetCursorScreenPos();
    PushFont(font.smallText, FontSmall());
    const float tw = CalcTextSize(text).x;
    const float fh = GetFontSize();
    const float x = centered ? p.x + (RowWidth() - tw) * 0.5f : p.x;
    dl->AddText(ImVec2(x, p.y), col, text);
    PopFont();
    SetCursorScreenPos(ImVec2(p.x, p.y + fh + S(2.0f)));
}
void TextNormal(const char* text, ImU32 col, bool semibold)
{
    ImDrawList* dl = GetWindowDrawList();
    const ImVec2 p = GetCursorScreenPos();
    PushFont(semibold ? font.semibold : font.regular, FontBody());
    const float fh = GetFontSize();
    dl->AddText(p, col, text);
    PopFont();
    SetCursorScreenPos(ImVec2(p.x, p.y + fh + S(3.0f)));
}
} }




