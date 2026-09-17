# neverlose.ui  ·  Neverlose 风格交互界面

基于 Dear ImGui 1.93(docking) + DirectX 11 + Win32 的独立菜单界面，视觉与交互参照 Neverlose Last：
左侧侧栏（头像 / 授权条 / 搜索 / 图标页签）、毛玻璃质感面板、自绘控件（滑动开关、滑条、下拉、按键绑定、取色器、
分组框、分段页签）、皮肤色板、状态栏与独立水印覆盖层。

## 目录

| 路径 | 说明 |
| --- | --- |
| `../imgui`（用 `-DIMGUI_DIR` 指定） | Dear ImGui 源码（docking 分支，外部依赖，不随本仓库提交） |
| `src/main.cpp` | Win32 窗口、消息循环、拖拽 / 缩放、INSERT / ESC 开关、覆盖层调度 |
| `src/render/renderer.*` | D3D11 设备与交换链、逐像素透明 / 颜色键透明、ImGui 后端初始化 |
| `src/ui/style.*` | 主题配色、字体、DPI 缩放、动画与颜色工具 |
| `src/ui/icons.*` | 纯矢量图标（不依赖任何图标字体），30+ 个 |
| `src/ui/widgets.*` | 自绘控件库（Neverlose 风格） |
| `src/ui/menu.cpp` | 菜单整体布局与各页签内容 |
| `src/features/config.*` | 设置结构体 + INI 配置读写（`nl_configs\last.ini`） |
| `src/features/esp.*` | 玩家绘制（预览 + 真实 ESP 共用同一套函数） |
| `src/features/bonemap.*` | 骨骼关节枚举 + `ClassifyBones()`：从骨骼几何认关节（纯逻辑，可离线回归） |
| `src/features/aimbot.*` | 瞄准逻辑占位（待接入 CreateMove） |
| `tools/bone_probe/` | `NLBoneProbe.exe`：骨骼表诊断 / 连续采样 / 分类器自检（只读） |
| `build.bat` / `run.bat` | 一键编译 / 启动 |

## 编译与运行

```bat
build.bat          :: 需要 VS Build Tools 2022+（MSVC x64 + CMake），产物 NeverloseUI.exe
run.bat
```

### 依赖：Dear ImGui（docking 分支，仓库外）

```bat
git clone -b docking https://github.com/ocornut/imgui.git ..\imgui
```

手动编译：

```bat
cmake -S . -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DIMGUI_DIR=..\imgui
cmake --build build
```

## 操作

| 按键 | 作用 |
| --- | --- |
| `INSERT` | 显示 / 隐藏菜单（隐藏后覆盖层仍然工作） |
| `ESC` | 关闭菜单 |
| `CTRL+Q` | 退出程序 |

鼠标：顶部 16px 条拖动面板，四边 / 四角 6px 拖动缩放；双击滑条右侧数值可直接输入；
右键点击按键绑定框可清除绑定。

## 页面

- **Rage / Legit**：瞄准键、部位、FOV、平滑、命中率、最小伤害、自动开墙、目标优先级、回溯、双发等
- **Anti-Aim**：Pitch / Yaw / Jitter / Desync 与实时 desync 可视化、假延迟、假下蹲、假探头
- **Visuals**：玩家 ESP 开关（方框 / 名字 / 血量 / 武器 / 距离 / 骨骼 / 连线 / 弹药 / 标记）、
  颜色、**实时预览面板**（左键点击可直接看到绘制效果）、世界选项、Chams
- **Misc**：连跳 / 自动转向 / 边缘跳、自动接受、军衔、聊天与标签
- **Scripts**：脚本列表 + 控制台日志（保存配置、切换页签等都会写入日志）
- **Settings**：UI 缩放、水印、动画、提示、8 套配色、配置保存 / 读取 / 删除 / 重置

## 后续人物绘制的接入点

绘制逻辑与数据来源已经解耦，`src/features/esp.h` 里的 `PlayerView` 是唯一的数据契约：

```cpp
struct PlayerView {
    ImVec2 head, feet;              // 头顶 / 脚底屏幕坐标
    ImVec2 bones[Bone_Count];       // 骨骼点（可选）
    float  health, distance, ammo;
    int    team;
    bool   visible, defuser, scoped;
    const char* name; const char* weapon;
};
void DrawPlayerOverlay(ImDrawList*, const PlayerView&, const VisualConfig&, float alpha);
```

预览面板就是用模拟数据填 `PlayerView` 后调用 `DrawPlayerOverlay()`，所以接入真实数据只需：

1. 在 Present / CreateMove 钩子里读取实体列表与 view matrix；
2. `WorldToScreen(entity->origin)` 填充 `head` / `feet`（骨骼矩阵填充 `bones`）；
3. 调用 `DrawPlayerOverlay()`——`src/features/esp.cpp` 的 `OnOverlayTick()` 是入口，注释里写了具体步骤。

## 内存层与偏移（已实现）

| 模块 | 作用 |
| --- | --- |
| `src/core/memory.*` | 外部（ReadProcessMemory）/ 内部（自身被注入）两种模式的统一访问层，模块基址、指针链读取 |
| `src/core/sigscan.*` | 特征码解析与扫描（`48 8B 05 ?? ?? ??` 或紧凑十六进制都支持），支持分块扫描远程进程 |
| `src/features/offsets.*` | 59 个偏移槽位（名字/分组/中文说明/必需标记/可选特征码），`offsets.ini` 读写，整段粘贴解析，模板导出 |
| `src/features/game.*` | 连接进程 → 读实体列表 → 场景节点坐标 → 骨骼数组 → 视矩阵 → WorldToScreen → 填 `PlayerView` |
| `src/ui/tab_memory.cpp` | 菜单 **Memory** 页：进程选择/连接、状态、偏移粘贴与逐项清单（绿色=已填，红色=必需缺失） |
| `tools/test_target/` | 自检用「假游戏」进程（`NLTestTarget.exe`），在自身模块里摆出同构布局并写出 `offsets_nltest.ini` |

### 用假目标自检整条链路

```bat
NLTestTarget.exe --static        :: 静态相机，位置可预测；不带参数则相机与玩家持续运动
copy offsets_nltest.ini offsets.ini
NeverloseUI.exe                  :: Memory 页会自动连接（Auto attach 打开时）
```

自检结果（可复算）：静态模式下 7 个假玩家中 2 个 CT 玩家可见，
目标进程内坐标经视矩阵投影后的期望屏幕坐标与运行日志完全一致：

| 玩家 | 期望 head / feet | 实际 head / feet | 屏幕盒子（预测 → 实测） |
| --- | --- | --- | --- |
| #1 (CT, r=310.5) | 280,591 / 255,781 | 280,591 / 254,781 | x236..323 y591..781 → **x235..323 y590..781** |
| #7 (CT, r=299.4) | 960,593 / 960,714 | 960,593 / 960,714 | x932..988 y593..714 → **x931..988 y592..714** |

同队玩家（team check）、摄像机背后的玩家都被正确剔除，说明过滤与投影都对。

## 骨骼 ESP：关节是怎么认出来的（2026-09 重做）

### 之前错在哪

旧版是"按几何瞎猜"：在某个高度带里找离中轴最近的骨当脖子/胸口/骨盆，肩和手则在一堆骨里挑
"分得最开的一对"。实测（`nl_boot.log`）推出来的索引是：

```
head=96 neck=82 chest=95 pelvis=0 shoulder=16/62 hand=39/87 foot=74/77
```

其中 `16` 在身体**背后 10.7cm**（背心/背包装饰骨）、`87` 在**髋部**——于是胸口连到背后、
胸口连到髋部，画出来就是"一条线横穿屏幕 + 身体上一团折线"。另外 `kBoneCount=128` 而实际
模型只有 ~101 根骨，尾部全是 0；分类时还出现过 `y=988` 这种未初始化的垃圾骨。

### 现在的做法（`src/features/bonemap.cpp`）

1. **先筛掉不可能的骨**：`dz ∈ [-12, 95]`、离实体原点水平距离 `dh ≤ 60`（垃圾骨全在这里被扔掉）；
2. **头** = 中轴附近（dh ≤ 15，奔跑前倾会放宽到 22）40~88 之间最高的一根；
3. **躯干** = 按头高比例分高度带（0.90 / 0.72 / 0.53），并要求
   "离中轴近 + 离上一节脊柱近"（`score = |dz-目标| + 0.30·dh + 0.45·水平偏移`），
   所以不会再出现胸口左右横跳；
4. **左右轴用"骨架左右对称"拟合**，而不是两脚连线和眼睛朝向：
   姿势采样（`NLBoneProbe --watch`）实测——站姿转身的玩家，两脚连线方向 **121.6°**，
   眼睛左右轴 **178.2°**，对称面拟合 **175°**（平均镜像误差 2.13cm）。
   两脚一前一后时脚连线其实是**前后**方向，直接拿来当左右轴就会把左右肢判反；
   拟合出来的平面法线正负号无意义，再用视线左右轴定号；
5. **肩** = 肩高带里"一左一右、高度和离轴距离都对称、尽量靠外、别太靠前/靠后"的一对
   （靠前/靠后扣分，专门排除背心、背包、武器骨）；
6. **手** = 手臂高度带里离身体中轴最远、且离肩有半条胳膊远的骨（握枪时两只手都在身前，
   所以不强行左右对称）；
7. **膝** = 归属这条腿（离这只脚比离另一只脚近）、位于脚与骨盆之间的骨；
8. **脚** = 贴地那一对中分得最开的；
9. 最后做一次几何体检：关节数 ≥ 8、骨盆 < 胸 < 脖 < 头、肩高在胸与脖之间，任意一条不满足
   就整体判定失败（宁可退回盒子骨架，也不画半真半假的线）。

绘制端（`esp.cpp`）另有两道闸：关节跑出玩家框太多就丢掉、单根骨头比整个人还长就不画——
这样即使哪天骨骼索引又错了，也不会再出现"飞到屏幕角落的长线"。

### 自检与证据

```bat
NLBoneProbe.exe --selftest                  :: 合成骨架 + 真实骨骼表回归 + 垃圾表反例
NLBoneProbe.exe cs2.exe --dump out.txt      :: 打印整张骨骼表 + 分类结果
NLBoneProbe.exe cs2.exe --watch 12          :: 连续采样，用来判定身体朝向用哪根轴
```

`--selftest` 里的 4 个用例（全部通过，报告写进 `--dump` 指定的文件）：

| 用例 | 内容 | 结果 |
| --- | --- | --- |
| case 0~2 | 合成骨架（脊柱台阶 + 帽子抖动骨 + 离轴 8cm 干扰骨 + 离轴 200cm 废骨），yaw 0/37/210 | 14 个关节全部落在期望的解剖区间，且左右正确 |
| case 3 | 整张表都是垃圾（全部离轴 565+） | 分类失败（不会硬凑骨架） |
| case 4 | **真实 CS2 骨骼表**（101 根，采集自活着的 `cs2.exe`） | 头 96 / 脖 80 / 胸 4 / 骨盆 1 / 肩 82·81 / 手 30·30 / 膝 79·18 / 脚 77·74，左右轴 -2.2°、对称面误差 1.87 |

真实数据现场（CT 玩家，`origin=(1216.00,-16.00,-163.97)`）：原始骨骼表里
`bone[0]` 是 `1216.00, -16.00, -165.68` + `0x0C` 处 `1.0f` + `0x10` 处单位四元数，
因此 **CS2 的 CTransform 是"位置在 0x00、四元数在 0x10、0x0C 是标量"**（读法没错，错的只是关节索引）。

运行日志里能直接看到分类结果：

```
[bones] model=0x46C7FDA2600 bones=128 head=96(66.8cm) neck=80 chest=4 pelvis=1
        shoulder=82/81 elbow=71/11 hand=30/30 knee=79/18 foot=77/74 左右轴=-4.0° 对称面误差=1.84cm
```

改前 / 改后（同一名玩家，同一套 ESP）：

| 改前（旧索引） | 改后 |
| --- | --- |
| `work/bone_dbg/z_top.png` `z_mid.png` `z_bot.png`（用户截图放大：胸口横线穿到背后、长线冲出方框） | `work/bone_dbg/final_player.png`（头/脊柱/双肩/双臂/双膝/双脚对齐人体） |

想把每个关节的屏幕坐标也写进日志，启动前设 `NL_BONE_DEBUG=1` 即可（默认关闭，避免刷屏）。

Memory 页的 **骨骼自动标定** 现在会调用同一个分类器：它只负责找到"骨骼数组在哪"，
关节索引直接由分类器给出（不再用"按身高比例硬凑"的旧办法），并把结果写回 `offsets.ini`。
运行时的索引来自分类器缓存，`kBoneIdx*` 只在分类失败时兜底，所以**换模型不用再手改偏移**。

## 注入版（DLL）与 F6 卸载

同一个代码库编译出两个产物：

| 产物 | 用途 |
| --- | --- |
| `NeverloseUI.exe` | 外部版：独立全屏透明覆盖层（DComp / 颜色键），适合不开游戏时调 UI 与偏移 |
| `NeverloseUI.dll` | 注入版：挂钩游戏自己的交换链，在 `Present` 前把界面画进后缓冲 |
| `NLInjector.exe` | 标准 `CreateRemoteThread + LoadLibraryA` 注入器：`NLInjector.exe <进程名\|PID> [dll]` |
| `NLHookHost.exe` | 假游戏（自带窗口 + D3D11 循环）用于验证注入/卸载，不依赖真实游戏 |

### 挂钩方式

- **VMT Hook**（`src/core/vmt.h`）：用一个临时设备+交换链取到 `IDXGISwapChain` 虚表，改写
  `Present`(index **8**) 与 `ResizeBuffers`(index **13**)，原始函数指针保存下来；卸载时逐个写回。
  不改机器码、不加 trampoline，可反复挂/卸。
- **WndProc Hook**：`SetWindowLongPtrW(GWLP_WNDPROC)` 接管游戏窗口消息，处理
  `INSERT`（开关菜单）/ `ESC`（关菜单）/ `F6`（卸载），其余转发给 `ImGui_ImplWin32_WndProcHandler`
  与原窗口过程。
- 渲染：`src/render/render_internal.cpp` 用游戏自己的 device/context，`Present` 前
  `NewFrame → 绘制菜单/ESP/水印 → RenderDrawData`，画完把渲染目标还原，不打断游戏画面。

### F6 安全卸载顺序（`src/dll/hooks.cpp`）

1. 摘掉 Present / ResizeBuffers 的 VMT 条目与 WndProc（游戏立刻回到原生流程）；
2. `ConfigSaveLast` / `OffsetsSave` 落盘；
3. `ImGui_ImplDX11_Shutdown` / `ImGui_ImplWin32_Shutdown` / `ImGui::DestroyContext`；
4. 另起线程等 400ms 后 `FreeLibraryAndExitThread` —— 保证卸载时没有线程还在执行本模块代码。

外部 `FreeLibrary`（injector eject / 宿主主动释放）也会在 `DLL_PROCESS_DETACH` 里补一次清钩子，
但推荐用 F6，因为卸载动作发生在渲染线程上更可控。

### 实测（`NLHookHost.exe` + 注入器，非真实游戏）

| 步骤 | 结果 |
| --- | --- |
| `NLInjector.exe NLHookHost.exe NeverloseUI.dll` | 注入成功，`VMT hook installed=1`，首次 Present 完成 ImGui 初始化 |
| 菜单渲染 | 游戏窗口内出现完整 Neverlose 界面（强调色像素 9833 / 侧栏底色 308296） |
| `INSERT` 开关菜单 | 隐藏后强调色 70、侧栏底色 758；再按恢复 9839 / 308347 |
| **F6 卸载** | 日志 `unload begin → hooks removed → detach`；`NeverloseUI.dll` 从模块列表消失；进程存活、画面继续刷新（前后两帧 100% 像素变化 = 仍在渲染） |
| 再次注入 → 再 F6 | 第二个生命周期同样干净退出（`detach reserved=0` 表示显式 FreeLibrary） |

![注入版界面（画在假游戏窗口里）](C:/Users/Administrator/Documents/Codex/2026-09-17/l/outputs/neverlose_ui_injected.png)

### 需要清楚的前提

- 这是常规的用户态挂钩（VMT + WndProc），**对带反作弊的游戏会被检测**，绕过反作弊不在本项目范围内；
  我也不会声称它能绕过任何 AC。
- 挂钩的是 `IDXGISwapChain` 的虚表（同驱动下所有实例共享），因此游戏必须走 D3D11 + 该交换链；
  若游戏全屏独占或自绘合成路径特殊，可能需要改成挂钩其自身的渲染入口。
- 卸载安全性依赖"没有其他线程还在跑本模块代码"，F6 路径已处理；若你用别的工具强退 DLL，
  请确保不是在 Present 执行期间。

## 用 AVX-Debugger 注入测试（内核手动映射 / KDU 加载的 AVXKernel.sys）

`tools/avx_inject` 就是 AVX 调试器 GUI 里那个"Inject"按钮的命令行版：它引用调试器自己的
`AVX.Core.dll`，走同一条路径 `KernelSession.TryOpen()` → `KernelBridge.ManualMap()` →
`ManualMapQuery()` 轮询握手，只是做成了脚本可调用。

```bat
:: 编译（AvxDir 默认指向 AVX.App 的输出目录，可用 -p:AvxDir=... 覆盖）
dotnet build tools\avx_inject\AvxInject.csproj -c Release

:: 驱动状态：kernel_bridge / ping / driver_version
tools\avx_inject\bin\Release\net10.0-windows\AvxInject.exe status

:: 注入（按 pid 或进程名；注入前先把 offsets.ini + nl_configs 复制到 %LOCALAPPDATA%\NeverloseUI）
AvxInject.exe inject --name cs2.exe --dll NeverloseUI.dll
AvxInject.exe inject --pid 23228 --dll NeverloseUI.dll
AvxInject.exe query  --base 0x26220640000
AvxInject.exe modules --name cs2.exe --find client
```

### 手动映射带来的两点差异（代码已经处理）

1. **模块不在 PEB 模块链表里**：`GetModuleHandleExA(FROM_ADDRESS)` 会失败，`ModuleDirectory()` 只能返回 `"."`。
   现在 `DataDirectory()` 会退回 `%LOCALAPPDATA%\NeverloseUI`，只读查找顺序是
   **模块目录 → `%LOCALAPPDATA%\NeverloseUI` → 主 EXE 目录 → 当前工作目录**，
   所以 `offsets.ini` / `nl_configs` / `nl_boot.log` 都还能落地。
   注入前的准备就是两条命令：
   ```bat
   mkdir "%LOCALAPPDATA%\NeverloseUI"
   copy offsets.ini "%LOCALAPPDATA%\NeverloseUI\" & xcopy /e /i /y nl_configs "%LOCALAPPDATA%\NeverloseUI\nl_configs"
   ```
   日志里 `[dll] attach: ... loader_module=0 data_dir=...` 就是这套判定的结果（`loader_module=0` = 手动映射）。
2. **`FreeLibraryAndExitThread` 对非装载模块无效**：F6 现在会"摘钩子 + 关 ImGui + 留驻"，
   日志写 `手动映射镜像：跳过 FreeLibrary，钩子已摘除，模块留驻`，进程不会因为释放镜像而崩。

### 实测（2026-09-17，KDU 加载的 AVXKernel 1.0.42）

| 步骤 | 结果 |
| --- | --- |
| `AvxInject status` | `kernel_bridge=open ping=True driver_version=1.0.42` |
| 注入 `NLHookHost.exe` | ManualMap 4ms 返回；握手"完成（DllMain 已返回）"；`VMT hook installed=1`、菜单画进宿主窗口 |
| 注入 `cs2.exe` | 内部模式 attach `client.dll`；偏移校验 18/20 通过；骨骼分类 `head=96 neck=80 chest=4 pelvis=1 shoulder=82/81 hand=45/45 knee=79/18 foot=77/74 左右轴=-5.2° 对称面误差=1.82cm` |
| 运行遥测 | `[tick] drawn=1 scanned=236 bones=1 readErr=0`，"小鱼"身上画出完整骨架（`work/bone_dbg/avx_cs2_skel.png`） |
| F6 卸载 | `unload begin → hooks removed → 手动映射镜像：跳过 FreeLibrary`，进程存活且继续渲染 |
| INSERT | 游戏窗口收到 INSERT（WndProc 钩子），菜单隐藏后 ESP 照常工作 |

注：`build.bat` 现在会把 `build\NeverloseUI.dll` 一起复制到项目根目录（以前只复制 EXE）；
如果目标进程正加载着旧 DLL，复制会失败并打印 `[!] NeverloseUI.dll 被占用`，先关掉目标进程再编译。

## 注入版性能：为什么会掉到 5fps（2026-09-17 实测）

### 症状
注入 cs2.exe 后帧率极低（遥测：**197ms/帧 ≈ 5fps**），而且转身时方框会从人身上"滑开"。

### 定位过程
给 Present 钩子加了分段遥测（`[perf]` 行，`NL_PERF=0` 可关），一行就能看出钱花在哪：

```
取数据 均 196.78 max 419.37 [入口段 1.21 找实体 3.24/194.38 读实体 78.42 读骨骼 1.18 零碎 113.94 C4 0.00]
```

再往下量（`NLBoneProbe --memtest`，同一段 1MB 内存做 A/B）：

```
裸 VirtualQuery               39.91 ms  (1.996 us/次)
IsValidRange(带 region 缓存)     0.07 ms  (0.004 us/次)   命中 20000/20000   加速比 544x
```

结论：`Memory::IsValidRange()` 在内部模式下**每次读之前都调一次 `VirtualQuery`**。
普通进程里它约 2µs，但在这个被 KDU 加载的 AVXKernel 手动映射、VAD 被驱动融合过的进程里
要 **5~30µs**；一帧里实体链表 + 字段检查有上千次，于是 40~190ms/帧全花在这里。
（外部 overlay 版没有这个问题：External 模式的 `IsValidRange` 只做 1 字节探测。）

### 修法（`src/core/memory.cpp`）
1. **内部模式彻底不调 `VirtualQuery`**：改成"4KB 页粒度探针 + 页表缓存"——
   一页只探一次（探针读由 `SafeMemcpy` 的 SEH 兜底），可读/不可读都缓存
   （1024 槽直接映射，`ReadRaw` 失败时把该页标记为不可读，后面不再去踩异常）。
2. **实体索引扫描按冷却走**（每 60 帧一次），不再"缓存为空就每帧重扫"——
   主菜单这种"一个玩家都没有"的状态以前会每帧扫 4096 个索引。
3. 段耗时改成按 120 帧窗口**累加 + 记最大值**（只打印最后一帧会全是 0）。

### 结果（同一个 CS2 进程、实战中，5 个玩家）

| 指标 | 修前 | 修后 |
| --- | --- | --- |
| 取数据（GameTick） | 196.78 ms | **0.84 ms**（最坏 6.1ms） |
| 其中 找实体 / 读实体 / 零碎 | 3.24 / 78.42 / 113.94 ms | 0.09 / 0.30 / 0.44 ms |
| 钩子总耗时 | 173.88 ms | **1.17~1.36 ms**（最坏 7.4ms） |
| 帧率（我们算的：帧间隔+钩子） | ≈5 fps | **44~77 fps** |
| 绘制 / 上传 | 0.18 / 0.07 ms | 0.05~0.22 / 0.03~0.16 ms |

"转身时方框滑开"就是这个 197ms 的直接后果：整帧落后 200ms，镜头一转，框还停在上一个位置。
现在每帧只占 1~2ms，落后量级降到一帧（十几毫秒）。若还想再压，可用 `NL_MATRIX_DELAY=N`
让 ESP 用 N 帧之前的视矩阵对比着看。

## 注入版稳定性：两次崩溃的排查与加固

测试期间 cs2.exe 崩过两次（`%LOCALAPPDATA%\CrashDumps\cs2.exe.*.dmp`）。没有 WinDbg 也能看：
`tools/dmpinfo`（自己写的 minidump 阅读器）给出异常码/地址/出错线程栈：

```
=== 异常 0xC0000005 (ACCESS_VIOLATION) 参数0=1(写) ===
    C:\Windows\System32\ntdll.dll+0x5586   →  栈：KERNELBASE ← nvwgf2umx(NVIDIA) ← tier0/rendersystemdx11
    第二次：ntdll.dll+0x5C2C              →  栈：KERNELBASE ← SDL3.dll ← tier0/rendersystemdx11
```

两次都是"堆地址上的写违例"，一次在渲染路径、一次在 **SDL3 的窗口消息路径**。据此做了四项加固：

| 加固 | 说明 |
| --- | --- |
| **ImGui 不再挂进游戏窗口过程** | 以前每条消息都转给 `ImGui_ImplWin32_WndProcHandler`，它会对游戏窗口 `SetCapture` 等副作用（SDL3 正管着这些状态）。现在窗口过程只处理 INSERT/ESC/F6 + 记滚轮/按键，鼠标位置/按键/滚轮/键盘都在 Present 里自己喂给 ImGui |
| **不重复挂同一层钩子** | 挂载前检查 Present 虚表项是否指向"不在模块链表里的镜像"（= 另一个手动映射实例），是就放弃本次挂载；手动映射镜像卸载不掉，叠多层会互相踩指针 |
| **摘钩子时确认虚表项还是自己的** | 只有 `vtable[i] == 我们写进去的函数` 才还原，否则跳过并记日志，避免摘掉别人的钩子/留下野指针 |
| **交换链变化时跳过绘制** | 游戏重建 swapchain 后继续用旧渲染目标画会画出花屏甚至崩在驱动里（同时保留 ResizeBuffers 里重建 RTV） |

另外加了 `tools/avx_inject` 的二分开关文件 `<数据目录>\nl_switch.ini`（键=1 生效），再出问题可以快速切：

```ini
no_hooks=1     ; 完全不挂钩子（只验证注入与初始化）
no_wndproc=1   ; 不接管窗口过程
no_draw=1      ; 挂钩子但不画（只保留取数据）
no_tick=1      ; 画界面但不读实体
```

### 转身时方框轻微脱离人物：怎么量、怎么调

帧时间从 197ms 降到 ~0.9ms 之后，"整帧落后"已经消失，但如果还剩一点偏差，只可能是
"我们读到的视矩阵" 和 "游戏渲染这一帧用的视矩阵" 不是同一帧。为此加了三个东西：

```ini
; <数据目录>\nl_switch.ini
dual_box=1        ; 用"上一帧的视矩阵"把同一个人再投影一次，多画一个红框
matrix_delay=1    ; ESP 用 N 帧之前的视矩阵（0=当前帧，1=上一帧）
```

* **红框对比**：转身时看哪个框贴着人物 —— 白框贴 = 当前帧矩阵是对的（保持 0）；
  红框贴 = 我们读到的矩阵偏新，把 `matrix_delay=1` 打开即可；
  两个框都落后于人物 = 需要往前预测（再告诉我，我加 `matrix_lead`）。
* **[perf] 行里的 `1帧矩阵差 均/最大 px`**：把上一条的差直接量化成像素。
  站在原地不动时它是 0.0；转身时它有多大，"差一帧"的偏差就有多大。
* 调完记得把 `nl_switch.ini` 里的这两行删掉（红框只是排查用）。

### 头骨选错：为什么"从侧面看方框脱离人物"（2026-09-17 修）

帧时间修复之后还剩一点脱框，截图一看：**红框（上一帧矩阵）和白框完全重合**，说明不是时序问题，
而是框中心压根不在人身上——框中心取的是"头"关节，而那个关节落在了**身前约 30cm** 的位置。

`NLBoneProbe --dump` 把 8 个玩家的骨骼表都摊开对比（同一个判据跑两遍）：

| 玩家 | 旧规则选中的"头" | dh≤6 时 40~88 里最高的骨 |
| --- | --- | --- |
| p1 | idx 106, dz 68.6, **dh 12.4** | idx 7, dz 64.0, dh 4.6 |
| p2 | idx 96, dz 68.7, **dh 12.3** | idx 7, dz 64.0, dh 4.5 |
| p3~p8 | idx 96/106, **dh 11.9~13.9** | 全是 idx 7, dz 63.3~64.3, dh 4.1~5.5 |

旧规则是"中轴 15cm 内最高的一根骨"，而**举枪的手/帽子挂件那根骨也在 15cm 内、还更高**
（所有模型都稳定偏前 ~30cm，所以每个模型的方框都整体偏到枪上）。收紧到 **dh ≤ 6**
（找不到再逐级放宽到 10、15）之后，8 个模型一致落在真正的中轴头骨上。

同时补了两条约束：

- **头必须在脖子正上方**：脖子选出来后，如果粗选的头离脖子水平 >8cm，而脖子正上方
  (≤6cm) 有骨，就换成那根；
- **胸口必须夹在骨盆与脖子之间、且在中点之上**：CS2 骨骼表里有一根固定在
  `origin+(0,0,40)` 的静止骨（不随姿势动），不约束的话它会被当成胸口。

回归测试也补了这个坑：合成骨架里加了一根"头顶高度、身前 12cm"的干扰骨（index 112），
分类器必须仍然选真正的中轴头骨。真实骨骼表回归（case 4）现在的结论是：

```
head 7 (dz 62.2, dh 4.7)  neck 12 (dz 54.9)  chest 4 (dz 46.5)  pelvis 20 (dz 33.1)
shoulder 72/85   hand 45/45   knee 21/18   foot 77/74
```

修复后实机验证：`work/bone_dbg/fix_zoom.png` —— 盒子贴着人物，头顶圆环落在头上
（改前是落在枪的瞄准镜附近）。

## 功能实现状态（2026-09-17 更新）

之前"只有开关没有实现"的 92 个配置项，现在开始逐个补齐。当前状态：

| 功能 | 状态 | 说明 |
| --- | --- | --- |
| ESP 全套（方框/名字/血量/武器/距离/骨骼/连线/标志/仅可见） | ✅ 已实现 | 骨骼用分类器，护甲条已按要求移除（头盔改在 flags 里显示 `HK`） |
| **CreateMove 钩子** | ✅ 已跑通 | CCSGOInput 虚表**索引 25**；`arg3` = CUserCmd（+0x00 虚表、+0x08 tick 每秒 +64、**+0x60 按键位**、+0x18/+0x1C 角度），≈190 次/秒 |
| **Aimbot（静默）** | ✅ 已实现（待实测） | 每帧解算目标（FOV/优先级/部位），CreateMove 里把角度写进命令 +0x18/+0x1C（相机不动=静默），准星压住时写 IN_ATTACK 自动开火。未做：hitchance/mindamage/autowall/backtrack/legit 平滑 |
| **ConVar 引擎接口** | ✅ 已打通 | 实测 `sensitivity=0.95`：对象在堆上，用 `dwSensitivity` 当锚点扫 ±48MB，按 name@0x00 / desc@0x20 / value@0x58 三条件校验，写入前再验一次 |
| **muteEnemy（静音敌人）** | ✅ 已生效 | 走 ConVar：`cl_mute_enemy_team=1`，写入后读回确认 |
| **antiAfk** | ⏳ 已实现待实测 | 每 10 秒送一次切武器按键（比乱动安全，不会把人挪出掩体） |
| **glow（发光）** | ✅ 已生效 | 用**构建匹配的 schema** 对账后发现父偏移错了（`m_Glow` 应为 0xDE0，我原来写的 0xC00 是旧版），修好后实测 `发光写入=1263 glow偏移=ok` |
| **Anti-Aim** | ✅ 双通道 | ①命令角度 `cmd+0x18/+0x1C`（打枪方向）②**`m_angEyeAngles`**（别人看到的模型朝向——之前只写①所以"看着没效果"）。不带动本地相机。可用 `aa_no_eyefield=1` 关② |
| **连跳（写命令按键位）** | ⏳ 待实测 | `bhop_cmd=1` 时直接改 `cmd+0x60` 的 IN_JUMP：空中清零、落地那一帧置上 → 每次落地都是新按键（理论零漏跳、无注入副作用）；需在比赛中验证 |
| **bunny hop** | ✅ 已实测 | 按住跳键才连跳、松手即停。两个必要机制：①按住期间 25ms 固定"抬起 8ms→按回"重按周期（不依赖落地帧，防断链）；②**低级键盘钩子**只看真人 keyup（LLKHF_INJECTED 事件忽略），否则自己的注入会把采样窗口占满导致"松手还在跳"。判据：不按 hop=0 jump=0；按住 jump=19 land=19（每落地必起跳）；松开立刻 hop=0 |
| **auto strafe** | ✅ 已实测 | 只在"按住跳键 + 离地"期间按视角转向交替 A/D，`strafe=±1` 随转身翻转 |
| **no flash** | ✅ 已实测 | 写本地 `m_flFlashMaxAlpha`，关闭时恢复 |
| **triggerbot** | ✅ 已实现 | 准星 12px 内可见敌人开火；`aim=xx px` 遥测已实测随准星变化 |
| **auto pistol / knife bot / auto peek** | ✅ 已实现 | 走同一套输入模拟（autoPeek = 记住原点，松键自动走回） |
| edge jump | ⚠️ 近似 | 站地移动中补跳；真正判断"前方悬空"要射线，留到 CreateMove 阶段 |
| jump bug | ⛔ 未实现 | 需要 tick 级精度（CreateMove） |
| Rage/Legit aimbot（30 项） | ⛔ 未实现 | `aimbot.cpp` 仍是空壳；目标/FOV/部位判据已就绪，缺"把角度写进 cmd" |
| Anti-Aim（19 项） | ⛔ 未实现 | 需要 CreateMove 钩子（落点已定位，见 `docs/re_cs2_notes.md`） |
| 世界视觉（chams/glow/夜视/去雾/去镜/FOV/雷达） | ⛔ 未实现 | 需要材质系统 / 引擎 convar |
| Misc 会话类（autoAccept/军衔/clantag/chat） | ⛔ 未实现 | 需要"执行控制台命令"（ConVar 通道已打通，命令执行那条链还没找）；muteEnemy/antiAfk 已完成 |
| Scripts 页 | ⛔ 空壳 | 现在只有硬编码列表 + 日志，没有脚本引擎 |

Misc 部分新增了 `src/features/input.*`（SendInput 输入模拟）与 `src/features/misc.*`，
遥测写在 `[tick]` 行里：

```
[tick] drawn=1 scanned=232 bones=1 readErr=0 ... misc[bhop=1 jumps=20 strafe=-1 trig=0 flash=1 peek=0 aim=16px]
```

## 渲染说明

启动日志写在 `nl_boot.log`（含实际启用的渲染模式）。窗口是全屏透明覆盖层，按顺序尝试：

| 顺序 | 方式 | 说明 |
| --- | --- | --- |
| 1 | **DirectComposition + 预乘 alpha** | 交换链交给 DWM 合成，逐像素透明，圆角/外发光完整（本机实际使用） |
| 2 | flip 模型 + 预乘 alpha | 部分驱动支持 `CreateSwapChainForHwnd + PREMULTIPLIED`，此时也走这条路 |
| 3 | 分层窗口 + 颜色键 | 兼容兜底（自动关掉外发光、圆角变小） |
| 4 | 不透明黑底 | 全部失败才用，仅供排查 |

菜单隐藏时窗口自动变为鼠标穿透并保持置顶，桌面 / 游戏操作不受影响；
鼠标位于面板内时隐藏系统光标、改由 ImGui 绘制自己的光标，面板外恢复系统光标，不会出现双光标。
点击 / 拖拽 / 缩放的命中测试都基于面板实际矩形，首帧未完成布局时会被忽略，不会把面板位置算坏。

## 字体与中文显示

`src/ui/style.cpp` 的 `SetupStyle()` 里做了三件事：

1. 主字体：`Segoe UI`（正文）/ `Segoe UI Semibold`（标题、数值），加载失败时依次回退 Tahoma / Verdana / ImGui 默认字体；
2. **中文回退**：把 `msyh.ttc`（微软雅黑，缺失时依次尝试 等线 / 黑体 / 宋体）以 `MergeMode` 合并进上面两个字体，
   字形范围用 `GetGlyphRangesChineseSimplifiedCommon()`，因此界面里出现中文时不会再渲染成 `?` 或方块；
3. DPI：按 `GetDpiForWindow()/96 * 设置里的 UI 缩放` 重新计算字体与控件尺寸，Settings 页拖动 UI 缩放会即时重载字体。

如需纯英文界面（完全对齐 Neverlose 观感），把 `src/ui/menu.cpp` 里 `TextSmall("中文…")` 那几行改成英文即可，
其余标签本来就是英文。












