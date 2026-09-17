#include <windows.h>
#include <windowsx.h>
#include <imgui.h>
#include <imgui_impl_win32.h>
#include "render/renderer.h"
#include "ui/style.h"
#include "ui/menu.h"
#include "features/config.h"
#include "features/esp.h"
#include "features/aimbot.h"
#include "features/game.h"
#include "features/offsets.h"
#include "core/sigscan.h"
#include "core/memory.h"
#include "core/paths.h"
#include "core/log.h"
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
namespace {
#define BootLog nl::FileLog
HWND  g_hwnd = nullptr;
bool  g_running = true;
bool  g_menuVisible = true;
bool  g_dragging = false;
bool  g_resizing = false;
int   g_resizeEdge = 0;
POINT g_dragOrigin{ 0, 0 };
float g_dragPos[2] = { 0.0f, 0.0f };
POINT g_resizeOrigin{ 0, 0 };
float g_resizeSize[2] = { 0.0f, 0.0f };
float g_dpiScale = 1.0f;
// 前台窗口抢焦点：SetForegroundWindow 对后台进程会被系统忽略，这里做一次 attach 兜底
void ForceForeground(HWND hwnd)
{
    const HWND fg = GetForegroundWindow();
    const DWORD fgThread = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
    const DWORD myThread = GetCurrentThreadId();
    if (fgThread != 0 && fgThread != myThread)
        AttachThreadInput(fgThread, myThread, TRUE);
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);
    BringWindowToTop(hwnd);
    if (fgThread != 0 && fgThread != myThread)
        AttachThreadInput(fgThread, myThread, FALSE);
}
void SetMenuVisible(bool visible)
{
    g_menuVisible = visible;
    LONG_PTR ex = GetWindowLongPtrW(g_hwnd, GWL_EXSTYLE);
    if (visible)
    {
        ex &= ~(LONG_PTR)WS_EX_TRANSPARENT;
        ex &= ~(LONG_PTR)WS_EX_NOACTIVATE;
        SetWindowLongPtrW(g_hwnd, GWL_EXSTYLE, ex);
        SetWindowPos(g_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        ForceForeground(g_hwnd);
        nl::ui::LogPush("menu shown");
    }
    else
    {
        ex |= WS_EX_TRANSPARENT | WS_EX_NOACTIVATE;
        SetWindowLongPtrW(g_hwnd, GWL_EXSTYLE, ex);
        nl::ui::LogPush("menu hidden  ·  overlay stays active");
    }
    ImGui::GetIO().MouseDrawCursor = visible;
}
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_NCHITTEST:
    {
        if (!g_menuVisible)
            return HTTRANSPARENT;
        POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hWnd, &pt);
        const nl::ui::PanelRect r = nl::ui::GetMenuPanelRect();
        const float b = nl::S(6.0f);
        if (r.w > 0.0f && pt.x >= r.x - b && pt.x <= r.x + r.w + b && pt.y >= r.y - b && pt.y <= r.y + r.h + b)
            return HTCLIENT;
        return HTTRANSPARENT;
    }
    case WM_LBUTTONDOWN:
    {
        if (!g_menuVisible)
            break;
        const POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        const nl::ui::PanelRect r = nl::ui::GetMenuPanelRect();
        // 首帧布局完成前面板矩形为空，此时不要进入拖拽/缩放，避免把面板位置算坏
        if (r.w < nl::S(200.0f) || r.h < nl::S(120.0f))
        {
            break;
        }
        const float b = nl::S(6.0f);
        int edge = 0;
        if (pt.x <= r.x + b) edge |= 1;
        if (pt.x >= r.x + r.w - b) edge |= 2;
        if (pt.y <= r.y + b) edge |= 4;
        if (pt.y >= r.y + r.h - b) edge |= 8;
        if (edge != 0)
        {
            g_resizing = true;
            g_resizeEdge = edge;
            g_resizeOrigin = pt;
            g_resizeSize[0] = r.w;
            g_resizeSize[1] = r.h;
            nl::g_settings.menu.pos[0] = r.x;
            nl::g_settings.menu.pos[1] = r.y;
            SetCapture(hWnd);
            return 0;
        }
        if (pt.y >= r.y && pt.y <= r.y + nl::S(13.0f) && pt.x >= r.x && pt.x <= r.x + r.w)
        {
            g_dragging = true;
            g_dragOrigin = pt;
            g_dragPos[0] = r.x;
            g_dragPos[1] = r.y;
            SetCapture(hWnd);
            return 0;
        }
    } break;
    case WM_MOUSEMOVE:
    {
        if (g_dragging)
        {
            const POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            nl::g_settings.menu.pos[0] = g_dragPos[0] + (float)(pt.x - g_dragOrigin.x);
            nl::g_settings.menu.pos[1] = g_dragPos[1] + (float)(pt.y - g_dragOrigin.y);
            return 0;
        }
        if (g_resizing)
        {
            const POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            const float dx = (float)(pt.x - g_resizeOrigin.x);
            const float dy = (float)(pt.y - g_resizeOrigin.y);
            float w = g_resizeSize[0];
            float h = g_resizeSize[1];
            float x = nl::g_settings.menu.pos[0];
            float y = nl::g_settings.menu.pos[1];
            if (g_resizeEdge & 2) w = g_resizeSize[0] + dx;
            if (g_resizeEdge & 1) { w = g_resizeSize[0] - dx; x = nl::g_settings.menu.pos[0] + dx; }
            if (g_resizeEdge & 8) h = g_resizeSize[1] + dy;
            if (g_resizeEdge & 4) { h = g_resizeSize[1] - dy; y = nl::g_settings.menu.pos[1] + dy; }
            nl::g_settings.menu.pos[0] = x;
            nl::g_settings.menu.pos[1] = y;
            nl::g_settings.menu.size[0] = w;
            nl::g_settings.menu.size[1] = h;
            return 0;
        }
    } break;
    case WM_LBUTTONUP:
        if (g_dragging || g_resizing)
        {
            g_dragging = false;
            g_resizing = false;
            ReleaseCapture();
            nl::ConfigSaveLast(nl::g_settings);
            return 0;
        }
        break;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE && g_menuVisible)
        {
            SetMenuVisible(false);
            return 0;
        }
        break;
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED)
            nl::RenderResize(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0) == SC_KEYMENU)
            return 0;
        break;
    case WM_SETCURSOR:
        // 鼠标在面板内时隐藏系统光标，改由 ImGui 绘制（面板外恢复系统光标）
        if (g_menuVisible && LOWORD(lParam) == HTCLIENT)
        {
            POINT pt{};
            GetCursorPos(&pt);
            ScreenToClient(hWnd, &pt);
            const nl::ui::PanelRect r = nl::ui::GetMenuPanelRect();
            if (r.w > 0.0f && pt.x >= r.x && pt.x <= r.x + r.w && pt.y >= r.y && pt.y <= r.y + r.h)
            {
                SetCursor(nullptr);
                return TRUE;
            }
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    if (g_menuVisible && ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return 1;
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}
}
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int)
{
    ImGui_ImplWin32_EnableDpiAwareness();
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"NeverloseUIWindow";
    RegisterClassExW(&wc);
    const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    g_hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_NOREDIRECTIONBITMAP,
                             wc.lpszClassName, L"neverlose.ui", WS_POPUP,
                             vx, vy, vw, vh, nullptr, nullptr, hInstance, nullptr);
    if (g_hwnd == nullptr)
        return 1;
    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);
    if (!nl::RenderInit(g_hwnd))
    {
        MessageBoxW(nullptr, L"DirectX 11 初始化失败。", L"neverlose.ui", MB_ICONERROR | MB_OK);
        return 1;
    }
    g_dpiScale = (float)GetDpiForWindow(g_hwnd) / 96.0f;
    nl::ConfigLoadLast(nl::g_settings);
    {
        const bool loaded = nl::OffsetsLoad();
        BootLog("offsets filled=%d/%d requiredMissing=%d (file=%s)",
                nl::OffsetsFilledCount(), nl::OffsetSlotCount(), nl::OffsetsRequiredMissing(),
                loaded ? "loaded" : "default");
    }
    nl::SetupStyle(g_dpiScale * nl::g_settings.menu.uiScale);
    nl::g_colorKeyMode = nl::RenderUsesColorKey();
    // 特征码解析自检（写进启动日志，便于确认扫描器可用）
    {
        const unsigned char dummy[10] = { 0x48, 0x8B, 0x05, 0x11, 0x22, 0x33, 0x44, 0x48, 0x85, 0xC0 };
        const std::vector<nl::SigByte> sig = nl::ParseSignature("48 8B 05 ?? ?? ?? ?? 48 85 C0");
        const uintptr_t hit = nl::ScanBufferForSignature(dummy, sizeof(dummy), sig);
        BootLog("sigscan selftest: bytes=%d hitOffset=%lld (expect 0)", (int)sig.size(),
                (long long)(hit == 0 ? -1 : (long long)(hit - (uintptr_t)dummy)));
        // 启动时导出待填模板，方便按清单收集偏移
    {
            FILE* fp = nullptr;
            if (fopen_s(&fp, (nl::ModuleDirectory() + "\\offsets_template.ini").c_str(), "wb") == 0 && fp != nullptr)
            {
                const std::string tpl = nl::OffsetsExportTemplate();
                fwrite(tpl.data(), 1, tpl.size(), fp);
                fclose(fp);
            }
        }
    }
    nl::GameAttachOnStartup(nl::g_settings);
    if (nl::g_mem.Attached())
    {
        nl::ui::LogPush("auto attached %s", nl::g_settings.menu.targetProcess);
        const int bad = nl::GameVerifyAndLog();
        nl::ui::LogPush("offset verify: bad=%d", bad);
    }
    {
        RECT wr = {};
        GetWindowRect(g_hwnd, &wr);
        RECT cr = {};
        GetClientRect(g_hwnd, &cr);
        BootLog("--- neverlose.ui boot ---");
        BootLog("dpi=%.2f scale=%.2f", (double)g_dpiScale, (double)nl::g_scale);
        BootLog("window rect=%ld,%ld %ldx%ld  client=%ldx%ld", wr.left, wr.top, wr.right - wr.left, wr.bottom - wr.top, cr.right, cr.bottom);
        BootLog("screen=%dx%d virtual=%dx%d", GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
                GetSystemMetrics(SM_CXVIRTUALSCREEN), GetSystemMetrics(SM_CYVIRTUALSCREEN));
        BootLog("renderer=%s", nl::RenderModeName());
    }
    nl::ui::LogPush("neverlose.ui build 1.0.0");
    nl::ui::LogPush("renderer: %s", nl::RenderModeName());
    SetMenuVisible(true);
    while (g_running)
    {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
                g_running = false;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!g_running)
            break;
        if (GetAsyncKeyState(VK_INSERT) & 1)
            SetMenuVisible(!g_menuVisible);
        if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState('Q') & 1))
            g_running = false;
        if (nl::ui::ConsumeRestyleRequest())
            nl::SetupStyle(g_dpiScale * nl::g_settings.menu.uiScale);
        // 软件光标只在“鼠标位于面板内”时绘制，避免出现双光标
        if (g_menuVisible)
        {
            POINT cur{};
            GetCursorPos(&cur);
            ScreenToClient(g_hwnd, &cur);
            const nl::ui::PanelRect r = nl::ui::GetMenuPanelRect();
            ImGui::GetIO().MouseDrawCursor = (r.w > 0.0f && cur.x >= r.x && cur.x <= r.x + r.w &&
                                              cur.y >= r.y && cur.y <= r.y + r.h);
        }
        nl::RenderNewFrame();
        nl::GameTick(nl::g_settings);
        if (g_menuVisible)
            nl::ui::DrawMenu(nl::g_settings, ImGui::GetIO().DeltaTime);
        nl::GameDrawEsp(nl::g_settings, ImGui::GetBackgroundDrawList());
        nl::OnOverlayTick(nl::g_settings, (float)ImGui::GetTime());
        nl::RenderPresent();
        if (nl::ui::MenuCloseRequested())
            SetMenuVisible(false);
        // 每 120 帧把内存读取状态写进日志，便于排查「连上了但画不出来」
        {
            static int frames = 0;
            if (++frames >= 120)
            {
                frames = 0;
                const nl::GameStatus& gs = nl::GameStatusGet();
                if (gs.attached)
                {
                    nl::FileLog("[game] scanned=%d drawn=%d bones=%d %.2fms  %s",
                                gs.entityScanned, gs.playersFound, gs.boneHits, (double)gs.tickMs, gs.message.c_str());
                    const std::vector<nl::PlayerView>& list = nl::GamePlayerList();
                    const nl::BombInfo& bomb = nl::BombGet();
                    nl::FileLog("[tick] simTime=%.1f bomb=%d remain=%.1f site=%d defusing=%d",
                                (double)nl::GameLocalSimulationTime(), bomb.valid ? 1 : 0,
                                (double)(bomb.valid ? ImMax(0.0f, bomb.blowTime - nl::GameLocalSimulationTime()) : 0.0f),
                                bomb.site, bomb.beingDefused ? 1 : 0);
                    for (size_t i = 0; i < list.size() && i < 3; ++i)
                    {
                        nl::FileLog("[game]   p%d head=%.0f,%.0f feet=%.0f,%.0f  hp=%.0f  d=%.1f  vis=%d",
                                    (int)i, (double)list[i].head.x, (double)list[i].head.y,
                                    (double)list[i].feet.x, (double)list[i].feet.y,
                                    (double)list[i].health, (double)list[i].distance, list[i].visible ? 1 : 0);
                        // 骨骼排查：设了环境变量 NL_BONE_DEBUG=1 才把每个关节的屏幕坐标写进日志
                        static const bool boneDebug = (GetEnvironmentVariableA("NL_BONE_DEBUG", nullptr, 0) > 0);
                        if (boneDebug)
                        {
                            for (int j = 0; j < nl::Bone_Count; ++j)
                            {
                                if (!list[i].bonesValid[j])
                                    continue;
                                nl::FileLog("[bone]   p%d %-10s = %.0f,%.0f", (int)i, nl::BoneJointName(j),
                                            (double)list[i].bones[j].x, (double)list[i].bones[j].y);
                            }
                        }
                    }
                }
            }
        }
        if (!g_menuVisible)
            Sleep(15);
    }
    nl::ConfigSaveLast(nl::g_settings);
    nl::RenderShutdown();
    DestroyWindow(g_hwnd);
    UnregisterClassW(wc.lpszClassName, hInstance);
    return 0;
}
