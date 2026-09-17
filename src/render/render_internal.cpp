#include "render/render_internal.h"
#include "ui/style.h"
#include "ui/menu.h"
#include "features/config.h"
#include "features/esp.h"
#include "features/game.h"
#include "features/misc.h"
#include "features/visuals.h"
#include "features/offsets.h"
#include "core/log.h"
#include "core/memory.h"
#include "core/paths.h"
#include "dll/hooks.h"
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace nl {

namespace {
ID3D11Device*           g_device = nullptr;
ID3D11DeviceContext*    g_context = nullptr;
IDXGISwapChain*         g_swapChain = nullptr;
ID3D11RenderTargetView* g_rtv = nullptr;
HWND                    g_hwnd = nullptr;
bool                    g_ready = false;
bool                    g_styleApplied = false;

void CreateRTV()
{
    if (g_rtv)
    {
        g_rtv->Release();
        g_rtv = nullptr;
    }
    ID3D11Texture2D* back = nullptr;
    if (SUCCEEDED(g_swapChain->GetBuffer(0, IID_PPV_ARGS(&back))) && back != nullptr)
    {
        g_device->CreateRenderTargetView(back, nullptr, &g_rtv);
        back->Release();
    }
}

// ---------------------------------------------------------------- 性能遥测
// 目的：FPS 掉下来时能一眼看出是"我们的每帧开销"还是"游戏自己的帧时间"。
// 每 120 次 Present 调用写一行 [perf]：窗口时长 / 调用次数 / 帧间隔 / 钩子耗时 / 各阶段拆解。
// 同时记录这次窗口里出现过几个交换链、几个线程 —— 多交换链会让"帧间隔"和"每帧耗时"对不上，
// 必须先排除这种污染，否则数字没法解释。
struct PerfAcc {
    LARGE_INTEGER freq{};
    LARGE_INTEGER windowStart{};
    bool  hasWindowStart = false;
    LARGE_INTEGER lastCallEnd{};
    bool  hasLastCall = false;
    int   calls = 0;
    double hookSum = 0, hookMax = 0;
    double frameDtSum = 0, frameDtMax = 0;
    double gamePresentSum = 0, gamePresentMax = 0;
    double newFrameSum = 0, tickSum = 0, drawSum = 0, renderSum = 0, uploadSum = 0;
    double tickMax = 0, uploadMax = 0;
    double bigHook = 0;     // >5ms 的调用次数
    double hugeHook = 0;    // >50ms 的调用次数
    const void* chains[4] = {};
    int chainCount = 0;
    unsigned long threads[4] = {};
    int threadCount = 0;
    static double Ms(const LARGE_INTEGER& f, const LARGE_INTEGER& a, const LARGE_INTEGER& b)
    {
        return (double)(b.QuadPart - a.QuadPart) * 1000.0 / (double)f.QuadPart;
    }
};
PerfAcc g_perf;
bool    g_perfEnabled = true;
bool    g_perfInit = false;

void PerfReset()
{
    g_perf.hookSum = g_perf.hookMax = 0;
    g_perf.frameDtSum = g_perf.frameDtMax = 0;
    g_perf.gamePresentSum = g_perf.gamePresentMax = 0;
    g_perf.newFrameSum = g_perf.tickSum = g_perf.drawSum = g_perf.renderSum = g_perf.uploadSum = 0;
    g_perf.tickMax = g_perf.uploadMax = 0;
    g_perf.bigHook = g_perf.hugeHook = 0;
    g_perf.calls = 0;
    g_perf.chainCount = 0;
    g_perf.threadCount = 0;
    g_perf.hasWindowStart = false;
}
}

void RenderInternalPerfNoteHook(double hookMs, double frameDtMs, double gamePresentMs,
                                const void* swapChain, unsigned long threadId)
{
    if (!g_perfEnabled)
        return;
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    if (!g_perf.hasWindowStart)
    {
        g_perf.windowStart = now;
        g_perf.hasWindowStart = true;
    }
    g_perf.hookSum += hookMs;
    g_perf.frameDtSum += frameDtMs;
    g_perf.gamePresentSum += gamePresentMs;
    if (hookMs > g_perf.hookMax)
        g_perf.hookMax = hookMs;
    if (frameDtMs > g_perf.frameDtMax)
        g_perf.frameDtMax = frameDtMs;
    if (gamePresentMs > g_perf.gamePresentMax)
        g_perf.gamePresentMax = gamePresentMs;
    if (hookMs > 5.0)
        g_perf.bigHook += 1;
    if (hookMs > 50.0)
        g_perf.hugeHook += 1;
    if (g_perf.chainCount < 4)
    {
        bool seen = false;
        for (int i = 0; i < g_perf.chainCount; ++i)
            seen = seen || (g_perf.chains[i] == swapChain);
        if (!seen)
            g_perf.chains[g_perf.chainCount++] = swapChain;
    }
    if (g_perf.threadCount < 4)
    {
        bool seen = false;
        for (int i = 0; i < g_perf.threadCount; ++i)
            seen = seen || (g_perf.threads[i] == threadId);
        if (!seen)
            g_perf.threads[g_perf.threadCount++] = threadId;
    }
    if (++g_perf.calls >= 120)
    {
        const double n = (double)g_perf.calls;
        const double windowMs = PerfAcc::Ms(g_perf.freq, g_perf.windowStart, now);
        const double dt = g_perf.frameDtSum / n;
        const GameStatus& gs = GameStatusGet();
            const MiscStatus& ms = MiscStatusGet();
            const VisualsStats& vs = VisualsStatsGet();
        GameTickPhases phases;
        GameTickPhasesGet(&phases);
        // "每帧位移"= 目标头顶在屏幕上每秒帧移动多少像素：转身时这个数就是"差一帧"会错开的像素数
        double moveAvg = 0.0, moveMax = 0.0;
        {
            const std::vector<PlayerView>& list = GamePlayerList();
            static ImVec2 prevHead(0, 0);
            static bool hasPrev = false;
            static double sum = 0.0, mx = 0.0;
            static int cnt = 0;
            if (!list.empty())
            {
                const ImVec2 h = list[0].head;
                if (hasPrev)
                {
                    const double d = sqrt((double)(h.x - prevHead.x) * (h.x - prevHead.x) +
                                          (double)(h.y - prevHead.y) * (h.y - prevHead.y));
                    sum += d;
                    if (d > mx)
                        mx = d;
                    ++cnt;
                }
                prevHead = h;
                hasPrev = true;
            }
            moveAvg = (cnt > 0) ? (sum / cnt) : 0.0;
            moveMax = mx;
            sum = 0.0;
            mx = 0.0;
            cnt = 0;
        }
        FileLog("[perf] 窗口 %.2fs / %d 次Present（交换链 %d 个，线程 %d 个）  "
                "帧间隔 %.2fms + 钩子 %.2fms = %.2fms/帧(≈%.0ffps, 最长 %.1f)  "
                "钩子max %.2f(>5ms %d 次, >50ms %d 次)  游戏Present 均 %.2f max %.2f  |  "
                "NewFrame %.2f  取数据 均 %.2f max %.2f [入口段 %.2f 找实体 %.2f/%.2f 读实体 %.2f/%.2f 读骨骼 %.2f/%.2f 零碎 %.2f C4 %.2f]  "
                "绘制 %.2f  Render %.2f  上传 %.2f/%.2f  |  players=%d bones=%d  头顶位移/帧 %.1f/%.1f px  "
                "1帧矩阵差 均 %.1f 最大 %.1f px",
                windowMs / 1000.0, (int)n, g_perf.chainCount, g_perf.threadCount,
                dt, g_perf.hookSum / n, dt + g_perf.hookSum / n,
                (dt + g_perf.hookSum / n > 0.0) ? 1000.0 / (dt + g_perf.hookSum / n) : 0.0,
                g_perf.frameDtMax,
                g_perf.hookMax, (int)g_perf.bigHook, (int)g_perf.hugeHook,
                g_perf.gamePresentSum / n, g_perf.gamePresentMax,
                g_perf.newFrameSum / n, g_perf.tickSum / n, g_perf.tickMax,
                phases.preMs,
                phases.scanMs, phases.scanMaxMs, phases.entityMs, phases.entityMaxMs,
                phases.boneMs, phases.boneMaxMs,
                phases.postMs, phases.bombMs,
                g_perf.drawSum / n, g_perf.renderSum / n,
                g_perf.uploadSum / n, g_perf.uploadMax,
                gs.playersFound, gs.boneHits, moveAvg, moveMax,
                phases.matrixErrAvg, phases.matrixErrMax);
        PerfReset();
    }
}

bool RenderInternalReady() { return g_ready; }
ID3D11Device* RenderInternalDevice() { return g_device; }
ID3D11DeviceContext* RenderInternalContext() { return g_context; }
IDXGISwapChain* RenderInternalSwapChain() { return g_swapChain; }

bool RenderInternalInit(IDXGISwapChain* swapChain, HWND hwnd)
{
    if (swapChain == nullptr || hwnd == nullptr)
        return false;
    if (g_ready)
        return true;
    if (FAILED(swapChain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&g_device))) || g_device == nullptr)
    {
        FileLog("[internal] GetDevice 失败");
        return false;
    }
    g_device->GetImmediateContext(&g_context);
    if (g_context == nullptr)
    {
        FileLog("[internal] GetImmediateContext 失败");
        return false;
    }
    g_swapChain = swapChain;
    g_hwnd = hwnd;
    CreateRTV();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    if (!ImGui_ImplWin32_Init(g_hwnd) || !ImGui_ImplDX11_Init(g_device, g_context))
    {
        FileLog("[internal] ImGui 后端初始化失败");
        return false;
    }
    const float dpi = (float)GetDpiForWindow(g_hwnd) / 96.0f;
    ConfigLoadLast(g_settings);
    OffsetsLoad();
    SetupStyle(dpi * g_settings.menu.uiScale);
    g_styleApplied = true;
    g_colorKeyMode = false;   // 注入模式下直接叠加在游戏画面上，不需要颜色键
    g_ready = true;
    ui::LogPush("injected ready  ·  hwnd=%p  dpi=%.2f", (void*)g_hwnd, (double)dpi);
    FileLog("[internal] ready hwnd=%p device=%p", (void*)g_hwnd, (void*)g_device);
    return true;
}

void RenderInternalShutdown()
{
    if (!g_ready)
        return;
    ConfigSaveLast(g_settings);
    OffsetsSave();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    if (g_rtv)
    {
        g_rtv->Release();
        g_rtv = nullptr;
    }
    if (g_context)
    {
        g_context->Release();
        g_context = nullptr;
    }
    g_device = nullptr;      // 设备属于游戏，不要 Release
    g_swapChain = nullptr;
    g_hwnd = nullptr;
    g_ready = false;
    g_styleApplied = false;
}

void RenderInternalOnResize(IDXGISwapChain* swapChain)
{
    if (!g_ready || swapChain != g_swapChain)
        return;
    if (g_rtv)
    {
        g_rtv->Release();
        g_rtv = nullptr;
    }
    // ResizeBuffers 之后重建渲染目标
}

void RenderInternalFrame(Settings& s, bool menuVisible, float dt)
{
    if (!g_ready)
        return;
    if (!g_perfInit)
    {
        QueryPerformanceFrequency(&g_perf.freq);
        char perfEnv[8] = { 0 };
        const DWORD perfLen = GetEnvironmentVariableA("NL_PERF", perfEnv, sizeof(perfEnv));
        g_perfEnabled = !(perfLen > 0 && perfEnv[0] == '0');   // 只有显式 NL_PERF=0 才关
        g_perfInit = true;
        FileLog("[perf] 遥测%s（NL_PERF=%s）", g_perfEnabled ? "开" : "关",
                perfLen > 0 ? perfEnv : "未设置");
    }
    LARGE_INTEGER t0, t1, t2, t3, t4, t5;
    QueryPerformanceCounter(&t0);
    if (g_rtv == nullptr)
        CreateRTV();
    if (g_rtv == nullptr)
        return;
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    nl::DllDrainInput();      // 鼠标/滚轮/键盘：由窗口过程记录、这里喂给 ImGui
    ImGui::NewFrame();
    QueryPerformanceCounter(&t1);

    if (!nl::SwitchFlag("no_tick"))
        GameTick(s);
    QueryPerformanceCounter(&t2);
    if (menuVisible)
        ui::DrawMenu(s, dt);
    GameDrawEsp(s, ImGui::GetBackgroundDrawList());
    OnOverlayTick(s, (float)ImGui::GetTime());
    if (ui::ConsumeRestyleRequest())
        SetupStyle((float)GetDpiForWindow(g_hwnd) / 96.0f * s.menu.uiScale);
#if 0
    const int tab = s.menu.activeTab;
#endif
    QueryPerformanceCounter(&t3);
    ImGui::Render();
    QueryPerformanceCounter(&t4);
    if (g_perfEnabled)
    {
        const double tickMs = PerfAcc::Ms(g_perf.freq, t1, t2);
        const double drawMs = PerfAcc::Ms(g_perf.freq, t2, t3);
        const double renderMs = PerfAcc::Ms(g_perf.freq, t3, t4);
        g_perf.newFrameSum += PerfAcc::Ms(g_perf.freq, t0, t1);
        g_perf.tickSum += tickMs;
        g_perf.drawSum += drawMs;
        g_perf.renderSum += renderMs;
        if (tickMs > g_perf.tickMax)
            g_perf.tickMax = tickMs;
    }

    // 备份游戏当前的渲染目标，画完后还原
    ID3D11RenderTargetView* prevRTV = nullptr;
    ID3D11DepthStencilView* prevDSV = nullptr;
    g_context->OMGetRenderTargets(1, &prevRTV, &prevDSV);
    g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    g_context->OMSetRenderTargets(1, &prevRTV, prevDSV);
    if (prevRTV) prevRTV->Release();
    if (prevDSV) prevDSV->Release();
    QueryPerformanceCounter(&t5);
    if (g_perfEnabled)
    {
        const double uploadMs = PerfAcc::Ms(g_perf.freq, t4, t5);
        g_perf.uploadSum += uploadMs;
        if (uploadMs > g_perf.uploadMax)
            g_perf.uploadMax = uploadMs;
    }

    // 每 2 秒写一行实时遥测，方便排「连上了但画不出来」
    {
        static int frames = 0;
        if (++frames >= 120)
        {
            frames = 0;
            const GameStatus& gs = GameStatusGet();
            const BombInfo& bomb = BombGet();
            const MiscStatus& ms = MiscStatusGet();
            const VisualsStats& vs = VisualsStatsGet();
            // readErr 改成"本窗口新增"（累计值会被 convar 大范围扫描带偏，看不出真实情况）
            const unsigned long long failedTotal = g_mem.FailedReads();
            static unsigned long long failedPrev = 0;
            const unsigned long long failedDelta = failedTotal - failedPrev;
            failedPrev = failedTotal;
            FileLog("[tick] drawn=%d scanned=%d bones=%d readErr=+%llu simTime=%.1f bomb=%d remain=%.1f  misc[bhop=%d jump=%d land=%d cyc=%d strafe=%d trig=%d flash=%d peek=%d aim=%.0fpx key=%d/%d]",
                    gs.playersFound, gs.entityScanned, gs.boneHits, failedDelta,
                    (double)GameLocalSimulationTime(), bomb.valid ? 1 : 0,
                    bomb.valid ? (double)ImMax(0.0f, bomb.blowTime - GameLocalSimulationTime()) : 0.0,
                    ms.bhopActive ? 1 : 0, ms.airTransitions, ms.landings, ms.repressCount,
                    ms.strafeSide, ms.triggerFireCount, ms.noFlashApplied ? 1 : 0,
                    ms.autoPeekWalking ? 1 : 0, (double)ms.crosshairDist,
                    ms.jumpKeyPhys ? 1 : 0, ms.jumpKeyUser ? 1 : 0);
            FileLog("[vis] 雷达写入=%d 发光写入=%d chams写入=%d 目标=%d glow偏移=%s FOV=%s",
                    vs.spottedWrites, vs.glowingWrites, vs.chamsWrites, vs.glowTargets,
                    vs.glowChecked ? (vs.glowOffsetOk ? "ok" : "stale") : "未校验",
                    vs.fovApplied ? "已改" : "默认");
            if (gs.playersFound == 0)
            {
                int shown = 0;
                const int limit = EntityIndexLimit(g_mem, gs.moduleBase);
                for (int i = 1; i < limit && shown < 4; ++i)
                {
                    const uintptr_t ent = EntityFromIndex(g_mem, gs.moduleBase, i);
                    if (ent == 0)
                        continue;
                    GameLogEntityFields(ent, i);
                    ++shown;
                }
            }
        }
    }
}

bool RenderInternalWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (!g_ready || hwnd != g_hwnd)
        return false;
    return ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam);
}
}



