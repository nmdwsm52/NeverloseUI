#include "ui/icons.h"
#include "ui/style.h"

#include <cmath>

namespace nl { namespace icons {

namespace {

inline ImVec2 P(const ImVec2& c, float s, float x, float y)
{
    return ImVec2(c.x + (x - 0.5f) * s, c.y + (y - 0.5f) * s);
}

inline void Poly(ImDrawList* dl, const ImVec2& c, float s, ImU32 col, float th, const float* pts, int n, bool closed)
{
    ImVec2 buf[16];
    for (int i = 0; i < n; ++i)
        buf[i] = P(c, s, pts[i * 2 + 0], pts[i * 2 + 1]);
    dl->AddPolyline(buf, n, col, th, closed ? ImDrawFlags_Closed : 0);
}

inline void Line(ImDrawList* dl, const ImVec2& c, float s, ImU32 col, float th, float x0, float y0, float x1, float y1)
{
    dl->AddLine(P(c, s, x0, y0), P(c, s, x1, y1), col, th);
}

inline void Circ(ImDrawList* dl, const ImVec2& c, float s, ImU32 col, float th, float cx, float cy, float r)
{
    dl->AddCircle(P(c, s, cx, cy), r * s, col, 32, th);
}

inline void Disc(ImDrawList* dl, const ImVec2& c, float s, ImU32 col, float cx, float cy, float r)
{
    dl->AddCircleFilled(P(c, s, cx, cy), r * s, col, 24);
}

} // namespace

void Draw(ImDrawList* dl, Icon icon, const ImVec2& c, float s, ImU32 col, float thickness)
{
    if (icon == Icon_None || dl == nullptr)
        return;

    const float th = thickness > 0.0f ? thickness : ImMax(1.2f, s * 0.11f);

    switch (icon)
    {
    case Icon_Target:
    {
        Circ(dl, c, s, col, th, 0.5f, 0.5f, 0.29f);
        Disc(dl, c, s, col, 0.5f, 0.5f, 0.055f);
        for (int i = 0; i < 4; ++i)
        {
            const float a = i * IM_PI * 0.5f;
            const ImVec2 d(cosf(a), sinf(a));
            dl->AddLine(c + d * (s * 0.37f), c + d * (s * 0.5f), col, th);
        }
    } break;

    case Icon_Crosshair:
    {
        Circ(dl, c, s, col, th, 0.5f, 0.5f, 0.25f);
        Disc(dl, c, s, col, 0.5f, 0.5f, 0.045f);
        Line(dl, c, s, col, th, 0.5f, 0.06f, 0.5f, 0.19f);
        Line(dl, c, s, col, th, 0.5f, 0.81f, 0.5f, 0.94f);
        Line(dl, c, s, col, th, 0.06f, 0.5f, 0.19f, 0.5f);
        Line(dl, c, s, col, th, 0.81f, 0.5f, 0.94f, 0.5f);
    } break;

    case Icon_Skull:
    {
        Circ(dl, c, s, col, th, 0.5f, 0.42f, 0.29f);
        Disc(dl, c, s, col, 0.395f, 0.40f, 0.068f);
        Disc(dl, c, s, col, 0.605f, 0.40f, 0.068f);
        Line(dl, c, s, col, th * 0.9f, 0.40f, 0.71f, 0.60f, 0.71f);
        Line(dl, c, s, col, th * 0.9f, 0.44f, 0.85f, 0.44f, 0.71f);
        Line(dl, c, s, col, th * 0.9f, 0.56f, 0.85f, 0.56f, 0.71f);
    } break;

    case Icon_Eye:
    {
        dl->AddEllipse(c, ImVec2(s * 0.46f, s * 0.285f), col, 0.0f, 40, th);
        Disc(dl, c, s, col, 0.5f, 0.5f, 0.115f);
    } break;

    case Icon_Sliders:
    {
        const float ys[3] = { 0.24f, 0.5f, 0.76f };
        const float xs[3] = { 0.36f, 0.64f, 0.44f };
        for (int i = 0; i < 3; ++i)
        {
            Line(dl, c, s, col, th * 0.85f, 0.10f, ys[i], 0.90f, ys[i]);
            Disc(dl, c, s, col, xs[i], ys[i], (i == 1) ? 0.105f : 0.085f);
        }
    } break;

    case Icon_Code:
    {
        const float l[] = { 0.38f, 0.26f, 0.15f, 0.5f, 0.38f, 0.74f };
        const float r[] = { 0.62f, 0.26f, 0.85f, 0.5f, 0.62f, 0.74f };
        Poly(dl, c, s, col, th, l, 3, false);
        Poly(dl, c, s, col, th, r, 3, false);
        Line(dl, c, s, col, th * 0.85f, 0.56f, 0.14f, 0.44f, 0.86f);
    } break;

    case Icon_Gear:
    {
        Circ(dl, c, s, col, th, 0.5f, 0.5f, 0.27f);
        Circ(dl, c, s, col, th * 0.85f, 0.5f, 0.5f, 0.105f);
        for (int i = 0; i < 8; ++i)
        {
            const float a = i * IM_PI * 0.25f;
            const ImVec2 d(cosf(a), sinf(a));
            dl->AddLine(c + d * (s * 0.30f), c + d * (s * 0.46f), col, th * 1.05f);
        }
    } break;

    case Icon_Shield:
    {
        const float pts[] = { 0.5f, 0.07f, 0.87f, 0.22f, 0.87f, 0.52f, 0.5f, 0.94f, 0.13f, 0.52f, 0.13f, 0.22f };
        Poly(dl, c, s, col, th, pts, 6, true);
    } break;

    case Icon_Box:
    {
        const float e = 0.30f;
        Line(dl, c, s, col, th, 0.10f, 0.10f, 0.10f + e, 0.10f);
        Line(dl, c, s, col, th, 0.10f, 0.10f, 0.10f, 0.10f + e);
        Line(dl, c, s, col, th, 0.90f, 0.10f, 0.90f - e, 0.10f);
        Line(dl, c, s, col, th, 0.90f, 0.10f, 0.90f, 0.10f + e);
        Line(dl, c, s, col, th, 0.10f, 0.90f, 0.10f + e, 0.90f);
        Line(dl, c, s, col, th, 0.10f, 0.90f, 0.10f, 0.90f - e);
        Line(dl, c, s, col, th, 0.90f, 0.90f, 0.90f - e, 0.90f);
        Line(dl, c, s, col, th, 0.90f, 0.90f, 0.90f, 0.90f - e);
    } break;

    case Icon_Heart:
    {
        dl->AddCircleFilled(P(c, s, 0.31f, 0.36f), 0.205f * s, col, 24);
        dl->AddCircleFilled(P(c, s, 0.69f, 0.36f), 0.205f * s, col, 24);
        const ImVec2 t1 = P(c, s, 0.105f, 0.42f);
        const ImVec2 t2 = P(c, s, 0.895f, 0.42f);
        const ImVec2 t3 = P(c, s, 0.5f, 0.93f);
        dl->AddTriangleFilled(t1, t2, t3, col);
    } break;

    case Icon_Text:
    {
        Line(dl, c, s, col, th, 0.16f, 0.18f, 0.84f, 0.18f);
        Line(dl, c, s, col, th, 0.5f, 0.18f, 0.5f, 0.86f);
    } break;

    case Icon_Bone:
    {
        Circ(dl, c, s, col, th, 0.5f, 0.15f, 0.12f);
        Line(dl, c, s, col, th * 0.9f, 0.5f, 0.27f, 0.5f, 0.58f);
        Line(dl, c, s, col, th * 0.9f, 0.20f, 0.38f, 0.80f, 0.38f);
        Line(dl, c, s, col, th * 0.9f, 0.5f, 0.58f, 0.28f, 0.90f);
        Line(dl, c, s, col, th * 0.9f, 0.5f, 0.58f, 0.72f, 0.90f);
    } break;

    case Icon_Palette:
    {
        Circ(dl, c, s, col, th, 0.5f, 0.5f, 0.40f);
        Disc(dl, c, s, col, 0.36f, 0.33f, 0.075f);
        Disc(dl, c, s, col, 0.64f, 0.31f, 0.075f);
        Disc(dl, c, s, col, 0.5f, 0.63f, 0.075f);
    } break;

    case Icon_Key:
    {
        Circ(dl, c, s, col, th, 0.28f, 0.29f, 0.17f);
        Line(dl, c, s, col, th, 0.40f, 0.41f, 0.86f, 0.87f);
        Line(dl, c, s, col, th * 0.9f, 0.76f, 0.77f, 0.60f, 0.61f);
        Line(dl, c, s, col, th * 0.9f, 0.62f, 0.63f, 0.72f, 0.53f);
    } break;

    case Icon_User:
    {
        Circ(dl, c, s, col, th, 0.5f, 0.28f, 0.17f);
        dl->PathArcTo(P(c, s, 0.5f, 0.88f), 0.33f * s, IM_PI, IM_PI * 2.0f, 32);
        dl->PathStroke(col, 0, th);
    } break;

    case Icon_Search:
    {
        Circ(dl, c, s, col, th, 0.42f, 0.42f, 0.28f);
        Line(dl, c, s, col, th, 0.63f, 0.63f, 0.88f, 0.88f);
    } break;

    case Icon_ChevronDown:
    {
        const float pts[] = { 0.24f, 0.41f, 0.5f, 0.66f, 0.76f, 0.41f };
        Poly(dl, c, s, col, th, pts, 3, false);
    } break;

    case Icon_ChevronRight:
    {
        const float pts[] = { 0.40f, 0.24f, 0.65f, 0.5f, 0.40f, 0.76f };
        Poly(dl, c, s, col, th, pts, 3, false);
    } break;

    case Icon_Check:
    {
        const float pts[] = { 0.19f, 0.53f, 0.42f, 0.75f, 0.81f, 0.26f };
        Poly(dl, c, s, col, th, pts, 3, false);
    } break;

    case Icon_Close:
    {
        Line(dl, c, s, col, th, 0.24f, 0.24f, 0.76f, 0.76f);
        Line(dl, c, s, col, th, 0.76f, 0.24f, 0.24f, 0.76f);
    } break;

    case Icon_Plus:
    {
        Line(dl, c, s, col, th, 0.5f, 0.18f, 0.5f, 0.82f);
        Line(dl, c, s, col, th, 0.18f, 0.5f, 0.82f, 0.5f);
    } break;

    case Icon_Cross:
    {
        Line(dl, c, s, col, th, 0.5f, 0.14f, 0.5f, 0.86f);
        Line(dl, c, s, col, th, 0.14f, 0.5f, 0.86f, 0.5f);
        Circ(dl, c, s, col, th * 0.8f, 0.5f, 0.5f, 0.36f);
    } break;

    case Icon_Planet:
    case Icon_Earth:
    {
        Circ(dl, c, s, col, th, 0.5f, 0.5f, 0.27f);
        dl->AddEllipse(c, ImVec2(s * 0.46f, s * 0.17f), col, -0.45f, 40, th * 0.9f);
        if (icon == Icon_Earth)
            Line(dl, c, s, col, th * 0.8f, 0.5f, 0.23f, 0.5f, 0.77f);
    } break;

    case Icon_Zap:
    {
        const float pts[] = { 0.58f, 0.07f, 0.29f, 0.53f, 0.47f, 0.53f, 0.42f, 0.95f, 0.71f, 0.46f, 0.52f, 0.46f };
        Poly(dl, c, s, col, th, pts, 6, true);
    } break;

    case Icon_Speed:
    {
        dl->PathArcTo(P(c, s, 0.5f, 0.62f), 0.40f * s, IM_PI * 1.15f, IM_PI * 1.95f, 32);
        dl->PathStroke(col, 0, th);
        Line(dl, c, s, col, th, 0.5f, 0.62f, 0.70f, 0.36f);
        Disc(dl, c, s, col, 0.5f, 0.62f, 0.06f);
    } break;

    case Icon_Skate:
    {
        dl->AddRect(P(c, s, 0.13f, 0.20f), P(c, s, 0.87f, 0.58f), col, 0.10f * s, th);
        Disc(dl, c, s, col, 0.30f, 0.78f, 0.075f);
        Disc(dl, c, s, col, 0.70f, 0.78f, 0.075f);
    } break;

    case Icon_Wand:
    {
        Line(dl, c, s, col, th, 0.18f, 0.82f, 0.60f, 0.40f);
        Line(dl, c, s, col, th * 0.85f, 0.78f, 0.12f, 0.78f, 0.36f);
        Line(dl, c, s, col, th * 0.85f, 0.66f, 0.24f, 0.90f, 0.24f);
        Disc(dl, c, s, col, 0.62f, 0.16f, 0.055f);
    } break;

    case Icon_Power:
    {
        dl->PathArcTo(P(c, s, 0.5f, 0.55f), 0.31f * s, IM_PI * 0.62f, IM_PI * 0.38f, 36);
        dl->PathStroke(col, 0, th);
        Line(dl, c, s, col, th, 0.5f, 0.12f, 0.5f, 0.47f);
    } break;

    case Icon_Info:
    {
        Circ(dl, c, s, col, th, 0.5f, 0.5f, 0.42f);
        Disc(dl, c, s, col, 0.5f, 0.30f, 0.055f);
        Line(dl, c, s, col, th * 0.9f, 0.5f, 0.45f, 0.5f, 0.73f);
    } break;

    case Icon_Dot:
    {
        Disc(dl, c, s, col, 0.5f, 0.5f, 0.17f);
    } break;

    case Icon_Music:
    {
        Disc(dl, c, s, col, 0.30f, 0.74f, 0.13f);
        Line(dl, c, s, col, th, 0.42f, 0.74f, 0.42f, 0.22f);
        Line(dl, c, s, col, th, 0.42f, 0.22f, 0.80f, 0.32f);
        Line(dl, c, s, col, th, 0.80f, 0.32f, 0.80f, 0.66f);
    } break;

    case Icon_Trash:
    {
        Line(dl, c, s, col, th, 0.12f, 0.24f, 0.88f, 0.24f);
        Line(dl, c, s, col, th, 0.38f, 0.24f, 0.38f, 0.12f);
        Line(dl, c, s, col, th, 0.62f, 0.24f, 0.62f, 0.12f);
        Line(dl, c, s, col, th, 0.38f, 0.12f, 0.62f, 0.12f);
        const float body[] = { 0.20f, 0.30f, 0.80f, 0.30f, 0.72f, 0.90f, 0.28f, 0.90f };
        Poly(dl, c, s, col, th, body, 4, true);
    } break;

    case Icon_Save:
    {
        const float outer[] = { 0.12f, 0.12f, 0.74f, 0.12f, 0.88f, 0.26f, 0.88f, 0.88f, 0.12f, 0.88f };
        Poly(dl, c, s, col, th, outer, 5, true);
        dl->AddRect(P(c, s, 0.34f, 0.14f), P(c, s, 0.66f, 0.40f), col, 0.02f * s, th * 0.9f);
        dl->AddRect(P(c, s, 0.30f, 0.58f), P(c, s, 0.70f, 0.86f), col, 0.02f * s, th * 0.9f);
    } break;

    case Icon_Folder:
    {
        const float pts[] = { 0.10f, 0.78f, 0.10f, 0.24f, 0.42f, 0.24f, 0.50f, 0.34f, 0.90f, 0.34f, 0.90f, 0.78f };
        Poly(dl, c, s, col, th, pts, 6, true);
    } break;

    case Icon_Refresh:
    {
        dl->PathArcTo(P(c, s, 0.5f, 0.5f), 0.34f * s, IM_PI * 1.15f, IM_PI * 2.55f, 40);
        dl->PathStroke(col, 0, th);
        dl->AddTriangleFilled(P(c, s, 0.72f, 0.16f), P(c, s, 0.90f, 0.34f), P(c, s, 0.66f, 0.40f), col);
    } break;

    default: break;
    }
}

} } // namespace nl::icons
