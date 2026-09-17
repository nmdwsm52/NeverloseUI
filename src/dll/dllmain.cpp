// -----------------------------------------------------------------------------
// neverlose.ui —— 注入版入口
//   挂钩链：IDXGISwapChain::Present(8) / ResizeBuffers(13) + 游戏窗口 WndProc
//   热键：  INSERT 开关菜单 / ESC 关菜单 / F6 安全卸载 DLL
//   卸载：  摘钩子 -> 关 ImGui -> 还原 WndProc -> 独立线程 FreeLibraryAndExitThread
// -----------------------------------------------------------------------------
#include <windows.h>
#include "dll/hooks.h"
#include "core/log.h"
#include "core/paths.h"
#include "core/memory.h"
#include "features/config.h"
#include "features/offsets.h"
#include "features/game.h"
#include "features/createmove.h"
#include "features/input.h"
#include "features/convar.h"
#include "features/thirdperson.h"
#include "render/render_internal.h"
#include <string>

namespace {
HMODULE g_module = nullptr;

DWORD WINAPI InitThread(LPVOID)
{
    // 日志与配置都落在"数据目录"（模块目录；手动映射时是 %LOCALAPPDATA%\NeverloseUI）
    nl::FileLog("[dll] attach: base=%p module_dir=%s host_dir=%s data_dir=%s loader_module=%d",
                (void*)g_module, nl::ModuleDirectory().c_str(), nl::HostDirectory().c_str(),
                nl::DataDirectory().c_str(), nl::IsLoaderModule() ? 1 : 0);
    nl::ConfigLoadLast(nl::g_settings);
    const bool offsets = nl::OffsetsLoad();
    nl::FileLog("[dll] config loaded, offsets=%d filled=%d/%d", offsets ? 1 : 0,
                nl::OffsetsFilledCount(), nl::OffsetSlotCount());

    // 注入模式下内存访问走"内部"路径：直接指针解引用，不再走 RPM
    nl::g_mem.Open("self");
    if (nl::g_settings.menu.autoAttach)
    {
        if (nl::GameAttach("self", nl::g_settings.menu.targetModule))
        {
            // 注入后立刻把偏移体检报告写进日志：坏指针一律被吞掉，不会把游戏带崩
            const int bad = nl::GameVerifyAndLog();
            if (bad > 0)
            {
                // 有 BAD 就先试着自动标定实体列表（版本更新最常坏的就是这条链）
                std::string fix;
                nl::GameCalibrateEntityList(&fix);
                nl::FileLog("[auto] entity-list calibration: %s", fix.c_str());
                std::string fix2;
                nl::GameCalibrateHandleOffsets(&fix2);
                nl::FileLog("[auto] handle calibration: %s", fix2.c_str());
                std::string fix3;
                nl::GameCalibrateNameOffset(&fix3);
                nl::FileLog("[auto] name calibration: %s", fix3.c_str());
                std::string fix4;
                nl::GameCalibrateBones(&fix4);
                nl::FileLog("[auto] bone calibration: %s", fix4.c_str());
                if (bad > 0)
                    nl::GameVerifyAndLog();
            }
        }
    }

    // 引擎接口自检（convar 扫描 + 结构校验）
    nl::ConVarSelfTest();
    nl::ThirdPersonInit(nl::GameModuleBase(), 0x27DE000);
    {
        // 探测一批「可能能做功能」的 convar —— 有就能实现，没有就如实记下来
        static const char* kProbe[] = {
            "cl_mute_enemy_team", "mat_monitorgamma", "r_fullscreen_gamma", "r_drawparticles",
            "r_drawmodeldecals", "r_drawsprites", "viewmodel_fov", "cl_clanid",
            "cl_teamid_overhead_always", "fov_cs_debug", "cl_radar_always_centered", "cl_radar_scale",
            "sensitivity", "cl_showfps", "sv_cheats",
            "thirdperson", "thirdpersonshoulder", "c_thirdpersonshoulder", "c_thirdpersonshoulderdist",
            "c_thirdpersonshoulderheight", "c_thirdpersonshoulderoffset",
        };
        nl::ConVarProbe(kProbe, (int)(sizeof(kProbe) / sizeof(kProbe[0])));
    }

    nl::FileLog("[vis] 启动设置：radar=%d glow=%d fov=%.0f（改完 ini 要重新注入才生效）",
                nl::g_settings.vis.radarHack ? 1 : 0, nl::g_settings.vis.glow ? 1 : 0,
                (double)nl::g_settings.vis.fovOverride);

    // 真人按键钩子：bhop 等需要区分"用户按的"和"我们注入的"
    nl::input::StartPhysicalKeyHook();

    // CreateMove 钩子（探针模式）：拿到每 tick 的 CUserCmd，为精确 bhop / anti-aim / aimbot 铺路
    if (!nl::SwitchFlag("no_createmove"))
        nl::CreateMoveInstall(nl::GameModuleBase());
    else
        nl::FileLog("[cm] nl_switch.ini 里 no_createmove=1，跳过 CreateMove 钩子");

    if (!nl::SwitchFlag("no_hooks") && !nl::DllInstallHooks())
    {
        nl::FileLog("[dll] hook 安装失败，卸载自身");
        if (nl::IsLoaderModule())
        {
            FreeLibraryAndExitThread(g_module, 1);
        }
        nl::FileLog("[dll] 手动映射镜像无法 FreeLibrary，直接结束初始化线程");
        return 1;
    }
    nl::FileLog("[dll] 等待第一次 Present 完成初始化");
    return 0;
}
}

BOOL WINAPI DllMain(HMODULE module, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_module = module;
        DisableThreadLibraryCalls(module);
        nl::DllSetModule(module);
        if (HANDLE t = CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr))
            CloseHandle(t);
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        // F6 路径已经清理过；如果是外部 FreeLibrary（injector eject），这里补一次清钩子
        nl::FileLog("[dll] detach reserved=%p", reserved);
        if (reserved == nullptr && nl::DllHooksInstalled())
        {
            nl::DllRemoveHooks();
            nl::RenderInternalShutdown();
            nl::FileLog("[dll] detach: hooks + imgui cleaned by external eject");
        }
    }
    return TRUE;
}






