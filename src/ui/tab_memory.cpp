#include "ui/tab_memory.h"
#include "ui/style.h"
#include "ui/icons.h"
#include "ui/menu.h"
#include "core/memory.h"
#include "features/offsets.h"
#include "features/game.h"
#include <imgui.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace ImGui;

namespace nl { namespace ui {

namespace {

char        g_paste[4096] = { 0 };
std::string g_report = "把偏移整段粘贴到下面的框里，然后点 [解析并保存]。\n支持 name = 0x1234 / name 0x1234 / client.dll+0x1234。";

bool ContainsCI(const std::string& hay, const std::string& needle)
{
    if (needle.empty())
        return false;
    std::string h = hay, n = needle;
    for (auto& c : h) c = (char)tolower((unsigned char)c);
    for (auto& c : n) c = (char)tolower((unsigned char)c);
    return h.find(n) != std::string::npos;
}

std::string FormatAddress(uintptr_t v)
{
    char buf[48];
    snprintf(buf, sizeof(buf), "0x%llX", (unsigned long long)v);
    return buf;
}
void ButtonRow2(const char* l0, const char* l1, bool accent0, bool* r0, bool* r1)
{
    const ImVec2 p = GetCursorScreenPos();
    const float half = (RowWidth() - S(8.0f)) * 0.5f;
    SetCursorScreenPos(p);
    *r0 = Button(l0, ImVec2(half, S(23.0f)), accent0);
    SetCursorScreenPos(ImVec2(p.x + half + S(8.0f), p.y));
    *r1 = Button(l1, ImVec2(half, S(23.0f)), false);
    SetCursorScreenPos(ImVec2(p.x, p.y + S(23.0f) + S(2.0f)));
}
}

void TabMemory(Settings& s, Column& a, Column& b)
{
    const GameStatus& st = GameStatusGet();
    Memory::RefreshProcesses();
    char line[256];
    static int  g_editSlot = -1;
    static char g_editBuf[64] = { 0 };

    BeginGroup(a, "mem_target", "Target process", icons::Icon_Planet, nullptr);
    InputText("##proc", s.menu.targetProcess, (int)sizeof(s.menu.targetProcess), "cs2.exe", RowWidth(), "Process");
    {
        const std::vector<std::string>& all = Memory::Processes();
        int shown = 0;
        for (const auto& name : all)
        {
            if (shown >= 6)
                break;
            if (!ContainsCI(name, s.menu.targetProcess))
                continue;
            const ImVec2 p = GetCursorScreenPos();
            const float w = RowWidth();
            PushID(700 + shown);
            SetCursorScreenPos(p);
            InvisibleButton("##pick", ImVec2(w, S(19.0f)));
            const bool hov = IsItemHovered();
            ImDrawList* dl = GetWindowDrawList();
            const float t = Tween(GetID("##p"), hov ? 1.0f : 0.0f, 18.0f);
            if (t > 0.01f)
                dl->AddRectFilled(p, ImVec2(p.x + w, p.y + S(19.0f)), ColAlpha(g_accent, 0.08f * t), S(4.0f));
            PushFont(font.semibold, FontSmall());
            dl->AddText(ImVec2(p.x + S(7.0f), p.y + S(9.5f) - GetFontSize() * 0.5f),
                        ColMix(pal.textDim, pal.text, 0.6f + 0.4f * t), name.c_str());
            PopFont();
            if (IsItemClicked())
                snprintf(s.menu.targetProcess, sizeof(s.menu.targetProcess), "%s", name.c_str());
            PopID();
            SetCursorScreenPos(ImVec2(p.x, p.y + S(20.0f)));
            ++shown;
        }
        if (shown == 0)
            TextSmall("没有匹配的进程名（可以直接手输）", pal.textFaint);
    }
    InputText("##module", s.menu.targetModule, (int)sizeof(s.menu.targetModule), "client.dll", RowWidth(), "Module");
    Toggle("Auto attach on start", &s.menu.autoAttach, "启动时自动连接（跟着游戏一起开）");
    bool attach = false, detach = false;
    ButtonRow2("Attach", "Detach", true, &attach, &detach);
    if (attach)
    {
        if (GameAttach(s.menu.targetProcess, s.menu.targetModule))
            LogPush("attached %s", s.menu.targetProcess);
        else
            LogPush("attach failed: %s", GameStatusGet().message.c_str());
    }
    if (detach)
    {
        GameDetach();
        LogPush("detached");
    }
    Separator("status");
    Badge(st.attached ? (st.ready ? "READY" : "ATTACHED") : "OFFLINE",
          st.attached ? (st.ready ? pal.success : pal.warn) : pal.textDim);
    snprintf(line, sizeof(line), "process   %s    pid %lu", st.process.c_str(), st.pid);
    TextSmall(line, pal.textDim);
    snprintf(line, sizeof(line), "module    %s    base %s", st.moduleName.c_str(), FormatAddress(st.moduleBase).c_str());
    TextSmall(line, pal.textDim);
    snprintf(line, sizeof(line), "scanned %d    drawn %d    bones %d    %.2f ms",
             st.entityScanned, st.playersFound, st.boneHits, (double)st.tickMs);
    TextSmall(line, pal.textDim);
    TextSmall(st.message.c_str(), st.ready ? pal.success : pal.warn);
    EndGroup(a);

    BeginGroup(a, "mem_offsets", "Offsets", icons::Icon_Key, nullptr);
    snprintf(line, sizeof(line), "已填 %d / %d 项    必需缺失 %d 项",
             OffsetsFilledCount(), OffsetSlotCount(), OffsetsRequiredMissing());
    TextSmall(line, OffsetsRequiredMissing() == 0 ? pal.success : pal.warn);
    bool apply = false, reload = false, tmpl = false, clear = false;
    bool verify = false, verifyAll = false, bones = false;
    {
        const ImVec2 p = GetCursorScreenPos();
        const float w = RowWidth();
        SetCursorScreenPos(p);
        PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(8.0f), S(6.0f)));
        SetNextItemWidth(w);
        ImGui::InputTextMultiline("##pastebox", g_paste, sizeof(g_paste), ImVec2(w, S(92.0f)));
        PopStyleVar();
        SetCursorScreenPos(ImVec2(p.x, p.y + S(96.0f)));
    }
    ButtonRow2("解析并保存", "重新载入", true, &apply, &reload);
    if (apply)
    {
        const int n = OffsetsApplyPaste(g_paste, &g_report);
        LogPush("offsets parsed  ·  %d slots filled", n);
    }
    if (reload)
    {
        OffsetsLoad();
        g_report = "已从 offsets.ini 重新载入：\n" + OffsetsDumpFilled();
    }
    ButtonRow2("导出模板", "清空偏移", false, &tmpl, &clear);
    if (tmpl)
    {
        snprintf(g_paste, sizeof(g_paste), "%s", OffsetsExportTemplate().c_str());
        g_report = "模板已写入上面的输入框（也可另存为 offsets.ini 手工填）。";
    }
    if (clear)
    {
        OffsetsReset();
        OffsetsSave();
        g_report = "偏移已清空（常量恢复默认值）。";
    }
    ButtonRow2("校验当前偏移", "全项自检", true, &verify, &verifyAll);
    if (verify || verifyAll)
    {
        if (!g_mem.Attached())
        {
            g_report = "还没连接目标进程：先在 Attach 之后再校验。";
        }
        else
        {
            const int bad = GameVerifyOffsets(&g_report);
            LogPush("offset verify: bad=%d", bad);
            if (verifyAll)
                g_report += "\n提示：BAD 项按提示改单个值即可（列表里点那一行可直接改）。\n";
        }
    }
    ButtonRow2("骨骼自动标定", "导出报告", false, &bones, &verifyAll);
    if (bones)
    {
        if (!g_mem.Attached())
        {
            g_report = "还没连接目标进程：Attach 之后再标定骨骼。";
        }
        else
        {
            GameCalibrateBones(&g_report);
            LogPush("bones calibrated");
        }
    }
    bool elist = false;
    bool dummy2 = false;
    ButtonRow2("实体列表标定", "占位", true, &elist, &dummy2);
    if (elist)
    {
        if (!g_mem.Attached())
            g_report = "还没连接目标进程：Attach 之后再标定。";
        else
        {
            GameCalibrateEntityList(&g_report);
            LogPush("entity list calibrated");
        }
    }
    Separator("report");
    {
        const ImVec2 p = GetCursorScreenPos();
        const float w = RowWidth();
        const float h = S(120.0f);
        Dummy(ImVec2(w, h));
        ImDrawList* dl = GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), IM_COL32(8, 8, 11, 215), S(6.0f));
        dl->AddRect(p, ImVec2(p.x + w, p.y + h), ColAlpha(pal.text, 0.06f), S(6.0f), S(1.0f));
        PushFont(font.regular, S(11.0f));
        float y = p.y + S(6.0f);
        size_t start = 0;
        const float lh = S(13.0f);
        while (start <= g_report.size() && y + lh < p.y + h - S(2.0f))
        {
            const size_t nl2 = g_report.find('\n', start);
            const std::string ln = g_report.substr(start, (nl2 == std::string::npos ? g_report.size() : nl2) - start);
            dl->AddText(ImVec2(p.x + S(8.0f), y), pal.textDim, ln.c_str());
            y += lh;
            if (nl2 == std::string::npos)
                break;
            start = nl2 + 1;
        }
        PopFont();
    }
    EndGroup(a);

    BeginGroup(b, "mem_slots", "Offset slots", icons::Icon_Sliders, nullptr);
    const char* lastGroup = nullptr;
    for (int i = 0; i < OffsetSlotCount(); ++i)
    {
        OffsetSlot& slot = OffsetSlotAt(i);
        if (lastGroup == nullptr || strcmp(lastGroup, slot.group) != 0)
        {
            lastGroup = slot.group;
            Separator(slot.group);
        }
        const ImVec2 p = GetCursorScreenPos();
        const float w = RowWidth();
        const float h = S(17.0f);
        SetCursorScreenPos(p);
        InvisibleButton("##slot", ImVec2(w, h));
        const bool hov = IsItemHovered();
        const bool clicked = IsItemClicked();
        ImDrawList* dl = GetWindowDrawList();
        const int state = GameSlotState(i);
        if (hov)
            dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), ColAlpha(g_accent, 0.08f), S(4.0f));
        PushFont(font.regular, S(11.0f));
        ImU32 nameCol = slot.filled ? pal.text : (slot.required ? pal.danger : pal.textFaint);
        if (state == 1)
            nameCol = pal.success;
        else if (state == 2 || state == 3)
            nameCol = pal.danger;
        dl->AddText(ImVec2(p.x + S(6.0f), p.y + h * 0.5f - GetFontSize() * 0.5f), nameCol, slot.name);
        if (g_editSlot != i)
        {
            const std::string val = slot.filled ? FormatAddress(slot.value) : "待填";
            const float vw = CalcTextSize(val.c_str()).x;
            dl->AddText(ImVec2(p.x + w - vw - S(6.0f), p.y + h * 0.5f - GetFontSize() * 0.5f),
                        slot.filled ? (state == 2 ? pal.danger : pal.success) : pal.textFaint, val.c_str());
        }
        PopFont();
        if (hov)
        {
            BeginTooltip();
            TextUnformatted(slot.description);
            EndTooltip();
        }
        if (clicked)
        {
            g_editSlot = i;
            snprintf(g_editBuf, sizeof(g_editBuf), "0x%llX", (unsigned long long)slot.value);
        }
        if (g_editSlot == i)
        {
            SetCursorScreenPos(ImVec2(p.x + w - S(86.0f), p.y - S(1.0f)));
            SetNextItemWidth(S(84.0f));
            PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(4.0f), S(1.0f)));
            if (IsWindowAppearing())
                SetKeyboardFocusHere();
            const bool done = ImGui::InputText("##slotval", g_editBuf, sizeof(g_editBuf),
                                               ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CharsHexadecimal);
            const bool lost = IsItemDeactivated() && !IsItemActive();
            PopStyleVar();
            if (done || lost)
            {
                if (done)
                {
                    const uintptr_t v = (uintptr_t)_strtoui64(g_editBuf, nullptr, 0);
                    slot.value = v;
                    slot.filled = true;
                    OffsetsSave();
                    LogPush("offset %s = 0x%llX", slot.name, (unsigned long long)v);
                }
                g_editSlot = -1;
            }
        }
        SetCursorScreenPos(ImVec2(p.x, p.y + h));
    }
    EndGroup(b);
}
}}
