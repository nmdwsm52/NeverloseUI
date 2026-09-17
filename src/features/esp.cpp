#include "features/esp.h"
#include "features/game.h"
#include "core/paths.h"
#include "ui/style.h"
#include "ui/icons.h"
#include <cmath>
#include <cstdio>
namespace nl {
namespace {
BombInfo g_bomb;
}
const BombInfo& BombGet() { return g_bomb; }
BombInfo& BombMutable() { return g_bomb; }

namespace {
void TextShadow(ImDrawList* dl, ImFont* f, float size, const ImVec2& pos, ImU32 col, const char* txt)
{
    if (f) dl->AddText(f, size, ImVec2(pos.x + 1.0f, pos.y + 1.0f), IM_COL32(0, 0, 0, 170), txt);
    else   dl->AddText(ImVec2(pos.x + 1.0f, pos.y + 1.0f), IM_COL32(0, 0, 0, 170), txt);
    if (f) dl->AddText(f, size, pos, col, txt);
    else   dl->AddText(pos, col, txt);
}
ImVec2 Measure(ImFont* f, float size, const char* txt)
{
    if (!f) return ImGui::CalcTextSize(txt);
    ImGui::PushFont(f, size);
    const ImVec2 s = ImGui::CalcTextSize(txt);
    ImGui::PopFont();
    return s;
}
void TextCentered(ImDrawList* dl, ImFont* f, float size, float centerX, float y, ImU32 col, const char* txt)
{
    const ImVec2 s = Measure(f, size, txt);
    TextShadow(dl, f, size, ImVec2(centerX - s.x * 0.5f, y), col, txt);
}
ImU32 HealthColor(float hp)
{
    const float t = ImClamp(hp / 100.0f, 0.0f, 1.0f);
    const ImU32 low = IM_COL32(235, 76, 92, 255);
    const ImU32 mid = IM_COL32(245, 196, 84, 255);
    const ImU32 top = IM_COL32(96, 226, 138, 255);
    return (t < 0.5f) ? ColMix(low, mid, t * 2.0f) : ColMix(mid, top, (t - 0.5f) * 2.0f);
}
void FillQuad(ImDrawList* dl, const ImVec2& a, const ImVec2& b, const ImVec2& c, const ImVec2& d, ImU32 col)
{
    const ImVec2 pts[4] = { a, b, c, d };
    dl->AddConvexPolyFilled(pts, 4, col);
}
} // namespace
void DrawPlayerOverlay(ImDrawList* dl, const PlayerView& p, const VisualConfig& v, float alpha)
{
    if (dl == nullptr)
        return;
    if (v.visibleOnly && !p.visible)
        return;
    // 排查开关（nl_switch.ini 里写 dual_box=1）：用"上一帧的视矩阵"把同一个人的
    // 头顶/脚底重新投影一次，画一个红色方框。转身时看哪个框贴着人物，就知道
    // 是我们读到的矩阵偏新（红框准）还是偏旧（白框准）。
    if (nl::SwitchFlag("dual_box") && p.hasWorld)
    {
        ImVec2 h2, f2;
        const float* prev = nl::GamePreviousViewMatrix();
        if (nl::WorldToScreen(prev, p.headWorld, &h2) && nl::WorldToScreen(prev, p.feetWorld, &f2))
        {
            const float w2 = ImMax(f2.y - h2.y, 1.0f) * 0.46f;
            dl->AddRect(ImVec2(h2.x - w2 * 0.5f, h2.y), ImVec2(h2.x + w2 * 0.5f, f2.y),
                        IM_COL32(255, 64, 64, (int)(220 * alpha)), 0.0f, 0, 2.0f);
        }
    }
    const float height = ImMax(p.feet.y - p.head.y, 1.0f);
    if (height < 6.0f)
        return;
    const float width = height * 0.46f;
    const ImVec2 bmin(p.head.x - width * 0.5f, p.head.y);
    const ImVec2 bmax(p.head.x + width * 0.5f, p.feet.y);
    const ImU32 boxCol  = ColA(v.colBox, alpha * (p.visible ? 1.0f : 0.62f));
    const ImU32 visCol  = ColA(v.colVisible, alpha);
    const ImU32 fillCol = ColA(v.colFill, alpha * (v.fillAlpha / 100.0f) * (p.visible ? 1.0f : 0.5f));
    const ImU32 textCol = ColA(v.colText, alpha);
    const ImU32 boneCol = ColA(v.colSkeleton, alpha * (p.visible ? 1.0f : 0.6f));
    const float th = ImMax(1.0f, S(1.4f));
    const float shadow = ImMax(1.0f, S(1.0f));
    if (v.filled)
        dl->AddRectFilled(bmin, bmax, fillCol);
    if (v.snapline)
    {
        ImGuiViewport* vp = ImGui::GetMainViewport();
        const ImVec2 screenC(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y);
        dl->AddLine(ImVec2(screenC.x, screenC.y - S(2.0f)), ImVec2((bmin.x + bmax.x) * 0.5f, bmax.y),
                    p.visible ? visCol : boxCol, th);
    }
    if (v.box)
    {
        const ImU32 edge = p.visible ? visCol : boxCol;
        if (v.boxStyle == 1)
        {
            const float len = height * 0.22f;
            dl->AddLine(ImVec2(bmin.x, bmin.y), ImVec2(bmin.x + len, bmin.y), edge, th + 1.0f);
            dl->AddLine(ImVec2(bmin.x, bmin.y), ImVec2(bmin.x, bmin.y + len), edge, th + 1.0f);
            dl->AddLine(ImVec2(bmax.x, bmin.y), ImVec2(bmax.x - len, bmin.y), edge, th + 1.0f);
            dl->AddLine(ImVec2(bmax.x, bmin.y), ImVec2(bmax.x, bmin.y + len), edge, th + 1.0f);
            dl->AddLine(ImVec2(bmin.x, bmax.y), ImVec2(bmin.x + len, bmax.y), edge, th + 1.0f);
            dl->AddLine(ImVec2(bmin.x, bmax.y), ImVec2(bmin.x, bmax.y - len), edge, th + 1.0f);
            dl->AddLine(ImVec2(bmax.x, bmax.y), ImVec2(bmax.x - len, bmax.y), edge, th + 1.0f);
            dl->AddLine(ImVec2(bmax.x, bmax.y), ImVec2(bmax.x, bmax.y - len), edge, th + 1.0f);
        }
        else if (v.boxStyle == 2)
        {
            if (v.boxOutline)
                dl->AddRect(bmin, bmax, IM_COL32(0, 0, 0, (int)(150 * alpha)), S(4.0f), th + 1.0f);
            dl->AddRect(bmin, bmax, edge, S(4.0f), th);
        }
        else
        {
            if (v.boxOutline)
                dl->AddRect(ImVec2(bmin.x - shadow, bmin.y - shadow), ImVec2(bmax.x + shadow, bmax.y + shadow),
                            IM_COL32(0, 0, 0, (int)(150 * alpha)), 0.0f, th + 1.2f);
            dl->AddRect(bmin, bmax, edge, 0.0f, th);
        }
    }
    if (v.health)
    {
        const float barW = ImMax(2.0f, S(3.0f));
        const ImVec2 hm(bmin.x - barW - S(3.0f), bmin.y);
        const ImVec2 hx(hm.x + barW, bmax.y);
        const float hp = ImClamp(p.health, 0.0f, 100.0f) / 100.0f;
        dl->AddRectFilled(ImVec2(hm.x - S(1.0f), hm.y - S(1.0f)), ImVec2(hx.x + S(1.0f), hx.y + S(1.0f)), IM_COL32(0, 0, 0, (int)(150 * alpha)));
        dl->AddRectFilled(ImVec2(hm.x, hx.y - (hx.y - hm.y) * hp), hx, HealthColor(p.health));
        if (v.healthText)
        {
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", (int)(p.health + 0.5f));
            const ImVec2 s = Measure(nl::font.smallText, FontSmall(), buf);
            TextShadow(dl, nl::font.smallText, FontSmall(), ImVec2(hx.x + S(3.0f), (hm.y + hx.y) * 0.5f - s.y * 0.5f), textCol, buf);
        }
    }
    if (v.name)
        TextCentered(dl, nl::font.semibold, FontBody(), p.head.x, bmin.y - S(15.0f), textCol, p.name);
    // 护甲条已按需求移除（护甲值不再画条形图，头盔/护甲信息只在 flags 里体现）
    float infoY = bmax.y + S(3.0f);
    if (v.weapon && p.weapon && *p.weapon)
    {
        char wbuf[64];
        if (p.ammoClip >= 0)
            snprintf(wbuf, sizeof(wbuf), "%s · %d", p.weapon, p.ammoClip);
        else
            snprintf(wbuf, sizeof(wbuf), "%s", p.weapon);
        TextCentered(dl, nl::font.smallText, FontSmall(), p.head.x, infoY, textCol, wbuf);
        infoY += Measure(nl::font.smallText, FontSmall(), wbuf).y + S(1.0f);
    }
    if (v.distance)
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d m", (int)(p.distance + 0.5f));
        TextCentered(dl, nl::font.smallText, FontSmall(), p.head.x, infoY, textCol, buf);
        infoY += Measure(nl::font.smallText, FontSmall(), buf).y + S(1.0f);
    }
    if (v.ammo)
    {
        const float hp = ImClamp(p.ammo / 60.0f, 0.0f, 1.0f);
        const ImVec2 am(bmin.x, bmax.y + S(2.0f));
        const ImVec2 ax(bmax.x, am.y + ImMax(2.0f, S(3.0f)));
        dl->AddRectFilled(am, ax, IM_COL32(0, 0, 0, (int)(140 * alpha)));
        dl->AddRectFilled(am, ImVec2(am.x + (ax.x - am.x) * hp, ax.y), ColAlpha(g_accent, 0.9f * alpha));
    }
    if (v.flags)
    {
        float fy = bmin.y;
        if (p.helmet)
        {
            // 护甲条去掉之后，头盔信息用一个小标记保留（护甲值本身不再显示）
            TextShadow(dl, nl::font.smallText, FontSmall(), ImVec2(bmax.x + S(4.0f), fy), IM_COL32(120, 180, 255, (int)(255 * alpha)), "HK");
            fy += Measure(nl::font.smallText, FontSmall(), "HK").y + S(1.0f);
        }
        if (p.defuser)
        {
            TextShadow(dl, nl::font.smallText, FontSmall(), ImVec2(bmax.x + S(4.0f), fy), IM_COL32(120, 200, 255, (int)(255 * alpha)), "KIT");
            fy += Measure(nl::font.smallText, FontSmall(), "KIT").y + S(1.0f);
        }
        if (p.scoped)
        {
            TextShadow(dl, nl::font.smallText, FontSmall(), ImVec2(bmax.x + S(4.0f), fy), IM_COL32(255, 210, 120, (int)(255 * alpha)), "ZOOM");
            fy += Measure(nl::font.smallText, FontSmall(), "ZOOM").y + S(1.0f);
        }
        if (p.reloading)
        {
            TextShadow(dl, nl::font.smallText, FontSmall(), ImVec2(bmax.x + S(4.0f), fy), IM_COL32(255, 160, 90, (int)(255 * alpha)), "RELOAD");
            fy += Measure(nl::font.smallText, FontSmall(), "RELOAD").y + S(1.0f);
        }
        if (p.flashed > 0.5f)
        {
            TextShadow(dl, nl::font.smallText, FontSmall(), ImVec2(bmax.x + S(4.0f), fy), IM_COL32(255, 255, 255, (int)(255 * alpha)), "FLASH");
            fy += Measure(nl::font.smallText, FontSmall(), "FLASH").y + S(1.0f);
        }
        if (p.money >= 0)
        {
            char mbuf[24];
            snprintf(mbuf, sizeof(mbuf), "$%d", p.money);
            TextShadow(dl, nl::font.smallText, FontSmall(), ImVec2(bmax.x + S(4.0f), fy), IM_COL32(150, 235, 160, (int)(255 * alpha)), mbuf);
        }
    }
    if (v.skeleton)
    {
        ImVec2 b[Bone_Count];
        bool  ok[Bone_Count];
        if (p.hasBones)
        {
            for (int i = 0; i < Bone_Count; ++i)
            {
                b[i] = p.bones[i];
                ok[i] = p.bonesValid[i];
            }
        }
        else
        {
            // 没有真实骨骼（模型读不到）时的盒子骨架：位置是按身体比例摆的
            const float cx = p.head.x;
            b[Bone_Head]      = p.head;
            b[Bone_Neck]      = ImVec2(cx, bmin.y + height * 0.16f);
            b[Bone_Chest]     = ImVec2(cx, bmin.y + height * 0.28f);
            b[Bone_Pelvis]    = ImVec2(cx, bmin.y + height * 0.52f);
            b[Bone_ShoulderL] = ImVec2(cx - width * 0.42f, bmin.y + height * 0.20f);
            b[Bone_ShoulderR] = ImVec2(cx + width * 0.42f, bmin.y + height * 0.20f);
            b[Bone_ElbowL]    = ImVec2(cx - width * 0.55f, bmin.y + height * 0.36f);
            b[Bone_ElbowR]    = ImVec2(cx + width * 0.55f, bmin.y + height * 0.36f);
            b[Bone_HandL]     = ImVec2(cx - width * 0.58f, bmin.y + height * 0.52f);
            b[Bone_HandR]     = ImVec2(cx + width * 0.58f, bmin.y + height * 0.52f);
            b[Bone_KneeL]     = ImVec2(cx - width * 0.20f, bmin.y + height * 0.76f);
            b[Bone_KneeR]     = ImVec2(cx + width * 0.20f, bmin.y + height * 0.76f);
            b[Bone_FootL]     = ImVec2(cx - width * 0.22f, bmax.y);
            b[Bone_FootR]     = ImVec2(cx + width * 0.22f, bmax.y);
            for (int i = 0; i < Bone_Count; ++i)
                ok[i] = true;
        }
        // 屏幕空间体检：关节跑到玩家框外太多就丢掉。
        // 骨骼索引/投影一旦出错，画出来就是"一条线飞到屏幕角落"，这里是最后一道闸。
        const float marginX = width * 0.9f;
        const float marginY = height * 0.35f;
        for (int i = 0; i < Bone_Count; ++i)
        {
            if (!ok[i])
                continue;
            if (b[i].x < bmin.x - marginX || b[i].x > bmax.x + marginX ||
                b[i].y < bmin.y - marginY || b[i].y > bmax.y + marginY)
                ok[i] = false;
        }
        const float maxSeg = height * 1.25f;   // 单根骨头不可能比整个人还长
        auto seg = [&](int a, int c)
        {
            if (!ok[a] || !ok[c])
                return;
            const float dx = b[c].x - b[a].x;
            const float dy = b[c].y - b[a].y;
            if (dx * dx + dy * dy > maxSeg * maxSeg)
                return;
            dl->AddLine(b[a], b[c], IM_COL32(0, 0, 0, (int)(120 * alpha)), th + 1.2f);
            dl->AddLine(b[a], b[c], boneCol, th);
        };
        // 中间关节缺失时直连（例如肘/膝没推出来）
        auto chain = [&](int a, int mid, int c)
        {
            if (ok[a] && ok[mid] && ok[c])
            {
                seg(a, mid);
                seg(mid, c);
            }
            else
            {
                seg(a, c);
            }
        };
        seg(Bone_Head, Bone_Neck);
        seg(Bone_Neck, Bone_Chest);
        seg(Bone_Chest, Bone_Pelvis);
        // 肩没推出来时用胸口当手臂根，手臂照样有
        const int armRootL = ok[Bone_ShoulderL] ? Bone_ShoulderL : Bone_Chest;
        const int armRootR = ok[Bone_ShoulderR] ? Bone_ShoulderR : Bone_Chest;
        if (ok[Bone_ShoulderL])
            seg(Bone_Chest, Bone_ShoulderL);
        if (ok[Bone_ShoulderR])
            seg(Bone_Chest, Bone_ShoulderR);
        chain(armRootL, Bone_ElbowL, Bone_HandL);
        chain(armRootR, Bone_ElbowR, Bone_HandR);
        chain(Bone_Pelvis, Bone_KneeL, Bone_FootL);
        chain(Bone_Pelvis, Bone_KneeR, Bone_FootR);
        if (ok[Bone_Head])
            dl->AddCircle(b[Bone_Head], ImMax(1.5f, height * 0.028f), boneCol, 16, th);
        for (int i = 0; i < Bone_Count; ++i)
        {
            if (i == Bone_Head || !ok[i])
                continue;
            dl->AddCircleFilled(b[i], ImMax(1.5f, th * 1.1f), boneCol, 8);
        }
    }
}
void DrawPlayerPreview(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, const VisualConfig& v, float time, float alpha)
{
    const ImVec2 size(mx.x - mn.x, mx.y - mn.y);
    if (size.x < S(40.0f) || size.y < S(40.0f))
        return;
    dl->AddRectFilled(mn, mx, IM_COL32(13, 13, 18, 255), S(6.0f));
    dl->AddRectFilledMultiColor(mn, ImVec2(mx.x, mn.y + size.y * 0.62f),
                                IM_COL32(28, 25, 42, 255), IM_COL32(21, 21, 31, 255),
                                IM_COL32(13, 13, 18, 255), IM_COL32(13, 13, 18, 255));
    const float groundY = mn.y + size.y * 0.86f;
    dl->AddLine(ImVec2(mn.x, groundY), ImVec2(mx.x, groundY), IM_COL32(255, 255, 255, 16), S(1.0f));
    for (int i = 1; i < 7; ++i)
    {
        const float x = mn.x + size.x * (i / 7.0f);
        dl->AddLine(ImVec2(x, groundY), ImVec2(mn.x + size.x * 0.5f + (x - mn.x - size.x * 0.5f) * 2.4f, mn.y + size.y),
                    IM_COL32(255, 255, 255, 8), S(1.0f));
    }
    dl->AddRect(mn, mx, ColAlpha(pal.text, 0.07f), S(6.0f), S(1.0f));
    const float walk = sinf(time * 0.75f);
    const float bob  = fabsf(sinf(time * 1.5f)) * S(4.0f);
    const float cx   = mn.x + size.x * 0.5f + walk * size.x * 0.18f;
    const float h    = size.y * 0.56f;
    const ImVec2 feet(cx, groundY - bob);
    const ImVec2 head(cx + walk * S(1.5f), feet.y - h);
    const float width = h * 0.46f;
    const bool ct = (v.previewTeam == 0);
    const ImU32 bodyCol  = ct ? IM_COL32(58, 96, 148, 255) : IM_COL32(146, 104, 60, 255);
    const ImU32 bodyDark = ct ? IM_COL32(38, 64, 102, 255)  : IM_COL32(104, 72, 40, 255);
    const ImU32 skin     = IM_COL32(198, 160, 126, 255);
    const float hipY = head.y + h * 0.54f;
    const float shoulderY = head.y + h * 0.17f;
    const float headR = h * 0.085f;
    const float swing = sinf(time * 1.5f) * h * 0.05f;
    dl->AddEllipseFilled(ImVec2(cx, groundY + S(1.0f)), ImVec2(width * 0.7f, S(4.0f)), IM_COL32(0, 0, 0, 90), 0.0f, 24);
    FillQuad(dl, ImVec2(cx - width * 0.30f, hipY), ImVec2(cx - width * 0.02f, hipY),
             ImVec2(cx - width * 0.04f, feet.y), ImVec2(cx - width * 0.30f, feet.y), bodyDark);
    FillQuad(dl, ImVec2(cx + width * 0.02f, hipY), ImVec2(cx + width * 0.30f, hipY),
             ImVec2(cx + width * 0.30f, feet.y), ImVec2(cx + width * 0.04f, feet.y), bodyDark);
    FillQuad(dl, ImVec2(cx - width * 0.42f, shoulderY), ImVec2(cx + width * 0.42f, shoulderY),
             ImVec2(cx + width * 0.32f, hipY), ImVec2(cx - width * 0.32f, hipY), bodyCol);
    FillQuad(dl, ImVec2(cx - width * 0.42f, shoulderY), ImVec2(cx - width * 0.22f, shoulderY),
             ImVec2(cx - width * 0.26f, shoulderY + h * 0.34f + swing), ImVec2(cx - width * 0.46f, shoulderY + h * 0.34f - swing), bodyCol);
    FillQuad(dl, ImVec2(cx + width * 0.22f, shoulderY), ImVec2(cx + width * 0.42f, shoulderY),
             ImVec2(cx + width * 0.46f, shoulderY + h * 0.34f - swing), ImVec2(cx + width * 0.26f, shoulderY + h * 0.34f + swing), bodyCol);
    dl->AddCircleFilled(ImVec2(head.x, head.y + headR), headR, skin, 24);
    dl->AddCircleFilled(ImVec2(head.x, head.y + headR * 0.82f), headR, bodyDark, 24);
    PlayerView p;
    p.head = head;
    p.feet = feet;
    p.hasBones = true;
    p.bones[Bone_Head]   = ImVec2(head.x, head.y + headR);
    p.bones[Bone_Neck]   = ImVec2(cx, shoulderY);
    p.bones[Bone_Chest]  = ImVec2(cx, shoulderY + h * 0.14f);
    p.bones[Bone_Pelvis] = ImVec2(cx, hipY);
    p.bones[Bone_ShoulderL] = ImVec2(cx - width * 0.40f, shoulderY + h * 0.02f);
    p.bones[Bone_ShoulderR] = ImVec2(cx + width * 0.40f, shoulderY + h * 0.02f);
    p.bones[Bone_ElbowL] = ImVec2(cx - width * 0.44f, shoulderY + h * 0.18f - swing * 0.5f);
    p.bones[Bone_ElbowR] = ImVec2(cx + width * 0.44f, shoulderY + h * 0.18f + swing * 0.5f);
    p.bones[Bone_HandL]  = ImVec2(cx - width * 0.36f, shoulderY + h * 0.34f - swing);
    p.bones[Bone_HandR]  = ImVec2(cx + width * 0.36f, shoulderY + h * 0.34f + swing);
    p.bones[Bone_KneeL]  = ImVec2(cx - width * 0.22f, (hipY + feet.y) * 0.5f + h * 0.02f - swing * 0.35f);
    p.bones[Bone_KneeR]  = ImVec2(cx + width * 0.22f, (hipY + feet.y) * 0.5f + h * 0.02f + swing * 0.35f);
    p.bones[Bone_FootL]  = ImVec2(cx - width * 0.16f, feet.y);
    p.bones[Bone_FootR]  = ImVec2(cx + width * 0.16f, feet.y);
    for (int i = 0; i < Bone_Count; ++i)
        p.bonesValid[i] = true;
    p.health   = 35.0f + 65.0f * (0.5f + 0.5f * sinf(time * 0.35f));
    p.visible  = fmodf(time, 6.0f) < 3.4f;
    p.distance = 320.0f + 180.0f * (0.5f + 0.5f * sinf(time * 0.21f));
    p.team     = v.previewTeam;
    p.name     = ct ? "Ryu" : "Kaito";
    p.weapon   = ct ? "M4A4" : "AK-47";
    p.ammo     = 12.0f + 48.0f * (0.5f + 0.5f * sinf(time * 0.6f));
    p.defuser  = ct;
    p.scoped   = (!ct) && (fmodf(time, 5.0f) > 3.5f);
    DrawPlayerOverlay(dl, p, v, alpha);
    const char* label = "PREVIEW";
    const ImVec2 s = Measure(nl::font.smallText, FontSmall(), label);
    dl->AddRectFilled(ImVec2(mn.x + S(6.0f), mn.y + S(6.0f)),
                      ImVec2(mn.x + S(12.0f) + s.x, mn.y + S(6.0f) + s.y + S(5.0f)), IM_COL32(0, 0, 0, 130), S(4.0f));
    TextShadow(dl, nl::font.smallText, FontSmall(), ImVec2(mn.x + S(9.0f), mn.y + S(8.0f)), ColAlpha(g_accent, 0.95f), label);
}
// 顶部炸弹计时面板：站点 + 倒计时 + 拆包进度（用 C4 实体的模拟时间当"当前游戏时间"）
void DrawBombPanel(bool enabled, float time)
{
    if (!enabled)
        return;
    const BombInfo& b = BombGet();
    if (!b.valid || !b.ticking)
        return;
    float now = b.blowTime - b.timerLength;   // 兜底：不知道当前时间时按"刚安放"算
    {
        // 用本地 pawn 的 m_flSimulationTime 作为服务器时间近似（实体时间会随服务器推进）
        static float lastSim = 0.0f;
        static float lastReal = 0.0f;
        const float sim = GameLocalSimulationTime();
        if (sim > 0.0f)
            now = sim;
        else if (lastSim > 0.0f && lastReal > 0.0f)
            now = lastSim + (time - lastReal);
        lastSim = now;
        lastReal = time;
    }
    float remain = b.blowTime - now;
    if (!isfinite(remain))
        return;
    remain = ImClamp(remain, 0.0f, ImMax(b.timerLength, 1.0f));
    const float frac = ImClamp(remain / ImMax(b.timerLength, 1.0f), 0.0f, 1.0f);

    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + S(18.0f)), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.0f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                                   ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
    const float w = S(210.0f);
    const float h = S(52.0f);
    ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    if (ImGui::Begin("##nl_bomb", nullptr, flags))
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 mn = ImGui::GetWindowPos();
        const ImVec2 mx(mn.x + w, mn.y + h);
        const bool urgent = remain <= 10.0f;
        const ImU32 accent = urgent ? IM_COL32(255, 88, 88, 255) : IM_COL32(255, 176, 84, 255);
        const float pulse = urgent ? (0.6f + 0.4f * sinf(time * 8.0f)) : 1.0f;
        dl->AddRectFilled(mn, mx, IM_COL32(12, 12, 17, g_colorKeyMode ? 255 : 232), S(7.0f));
        dl->AddRect(mn, mx, ColAlpha(accent, 0.75f * pulse), S(7.0f), S(1.2f));
        // 站点标记
        char siteText[8];
        if (b.site == 0)
            snprintf(siteText, sizeof(siteText), "A");
        else if (b.site == 1)
            snprintf(siteText, sizeof(siteText), "B");
        else
            snprintf(siteText, sizeof(siteText), "?");
        ImGui::PushFont(font.semibold, FontBig());
        dl->AddText(ImVec2(mn.x + S(12.0f), mn.y + S(7.0f)), ColAlpha(accent, pulse), siteText);
        ImGui::PopFont();
        char buf[48];
        snprintf(buf, sizeof(buf), "%.1f", (double)remain);
        ImGui::PushFont(font.semibold, FontBig());
        const ImVec2 ts = ImGui::CalcTextSize(buf);
        dl->AddText(ImVec2(mx.x - S(12.0f) - ts.x, mn.y + S(7.0f)), IM_COL32(240, 240, 245, 255), buf);
        ImGui::PopFont();
        // 进度条
        const ImVec2 bm(mn.x + S(12.0f), mx.y - S(14.0f));
        const ImVec2 bx(mx.x - S(12.0f), mx.y - S(9.0f));
        dl->AddRectFilled(bm, bx, IM_COL32(255, 255, 255, 26), S(2.5f));
        dl->AddRectFilled(bm, ImVec2(bm.x + (bx.x - bm.x) * frac, bx.y), ColAlpha(accent, 0.95f), S(2.5f));
        // 拆包进度
        if (b.beingDefused)
        {
            char dib[64];
            const float left = ImMax(0.0f, b.defuseEndTime - now);
            snprintf(dib, sizeof(dib), "拆包中 %s  %.1fs", b.defuserName[0] ? b.defuserName : "?", (double)left);
            ImGui::PushFont(font.smallText, FontSmall());
            dl->AddText(ImVec2(mn.x + S(12.0f), mn.y + S(33.0f)), IM_COL32(140, 220, 255, 255), dib);
            ImGui::PopFont();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

void DrawWatermark(const Settings& s, float time)
{
    if (!s.menu.watermark)
        return;
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x - S(16.0f), vp->Pos.y + S(14.0f)), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.0f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                                   ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
    char buf[160];
    snprintf(buf, sizeof(buf), "neverlose.ui  |  Administrator  |  %.0f fps", ImGui::GetIO().Framerate);
    ImGui::PushFont(nl::font.semibold, FontBody());
    const ImVec2 ts = ImGui::CalcTextSize(buf);
    ImGui::PopFont();
    const float w = ts.x + S(40.0f);
    const float h = ts.y + S(12.0f);
    ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    if (ImGui::Begin("##nl_watermark", nullptr, flags))
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 wp = ImGui::GetWindowPos();
        const ImVec2 mn(wp);
        const ImVec2 mx(wp.x + w, wp.y + h);
        dl->AddRectFilled(mn, mx, IM_COL32(12, 12, 17, g_colorKeyMode ? 255 : 226), S(6.0f));
        dl->AddRect(mn, mx, ColAlpha(g_accent, g_colorKeyMode ? 0.75f : 0.35f), S(6.0f), S(1.0f));
        dl->AddRectFilled(ImVec2(mn.x + S(8.0f), mn.y + S(6.0f)), ImVec2(mn.x + S(11.0f), mx.y - S(6.0f)), g_accent, S(1.5f));
        const float pulse = 0.55f + 0.45f * sinf(time * 3.0f);
        dl->AddCircleFilled(ImVec2(mn.x + S(20.0f), mn.y + h * 0.5f), S(3.0f), ColAlpha(g_accent, pulse), 16);
        ImGui::PushFont(nl::font.semibold, FontBody());
        dl->AddText(ImVec2(mn.x + S(28.0f), mn.y + (h - ts.y) * 0.5f), pal.text, buf);
        ImGui::PopFont();
    }
    ImGui::End();
    ImGui::PopStyleVar();
}
void OnOverlayTick(const Settings& s, float time)
{
    // TODO(后续 人物绘制):
    //   1. 在 Present / CreateMove 钩子中读取实体列表与 view matrix；
    //   2. WorldToScreen(entity->origin) 得到 head/feet 并填充 PlayerView；
    //   3. 骨骼: 由 bone matrix 取 Bone_Head..Bone_RFoot 投影坐标, hasBones = true；
    //   4. 直接复用本文件的 DrawPlayerOverlay()，与预览面板完全同一套绘制逻辑。
    DrawWatermark(s, time);
    DrawBombPanel(s.vis.bombTimer, time);
}
} // namespace nl
