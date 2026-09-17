# 注入 CS2 的完整流程（build 14181 偏移已就位）

## 0. 现状（已在真实 CS2 上跑通）

实测环境：`cs2.exe`（PatchVersion 1.41.8.1 / dump build 14181），注入后未崩溃，
体检 **18/20 通过**、读到 **13 个玩家 Pawn**、骨骼/名字/坐标全部合理，ESP 已画在真玩家身上。

### dump 值与实际运行版本的差异（已用自动标定修正）

| 项 | dump/默认值 | 实际命中值 | 怎么发现的 |
| --- | --- | --- | --- |
| 实体列表步长 `kEntityStride` | 0x78 | **0x70** | 用「已知 controller/pawn 指针」反推 |
| 块指针偏移 `kEntityChunkOffset` | 0x10 | 0x10 ✓ | 同上 |
| `m_hController` | 0x13D0 | **0x1040** | 扫描 pawn 侧句柄，看哪个能解析出 controller |
| `m_hPlayerPawn` | 0x914 | 0x914 ✓ | 同上（controller 侧） |
| 骨骼数组 | scene+m_modelState+0x80 | scene+0x1C0 ✓ | 场景节点宽范围扫「头骨特征」 |
| `kBoneIdxHead` | 6（CS:GO 时代） | **106** | 同上（dz≈70 判据） |
| `kBoneIdxLeftFoot/RightFoot` | 23/24 | **81/86** | 同上（贴地判据） |
| `m_iszPlayerName` | 0x6F4 | 0x6F4 ✓ | 多控制器样本都像人名 |
| 实体索引上限 | kMaxPlayers=64 | **220** | `dwGameEntitySystem_highestEntityIndex`（玩家索引远超 64） |

> 这些都是 `Memory` 页三个按钮（实体列表标定 / 句柄标定 / 骨骼自动标定）自动扫出来并写回
> `offsets.ini` 的；游戏更新后偏移变了，重跑一次即可，不需要重新找 dump。

| 项 | 值 |
| --- | --- |
| 偏移来源 | `C:\Users\Administrator\Desktop\CS2dumper\output`（cs2-dumper，2026-09-17，build **14181**）+ 现场自动标定 |
| 已写入 | `NeverloseUI\offsets.ini`；CS:GO 测试目标用 `offsets_nltest.ini`，故意写坏的回归集用 `offsets_garbage.ini` |
| 目标 | `cs2.exe` / `client.dll`（`nl_configs\last.ini` 里 `autoAttach=1`） |
| 产物 | `NeverloseUI.dll`（注入版）、`NLInjector.exe`（LoadLibrary 注入器） |

## 1. 注入

```bat
NLInjector.exe cs2.exe NeverloseUI.dll      :: 也可用 PID
```

进程内在启动线程里完成：加载配置 -> `Memory::Open("self")`（内部模式，直接解引用，不走 RPM）
-> 用临时设备取 `IDXGISwapChain` 虚表 -> 挂 `Present`(8) / `ResizeBuffers`(13) -> 首次 Present 时
初始化 ImGui 并挂 WndProc。

## 2. 热键

| 键 | 作用 |
| --- | --- |
| `INSERT` | 开关菜单 |
| `ESC` | 关菜单 |
| `F6` | **安全卸载 DLL**（摘钩子 → 关 ImGui → 独立线程 FreeLibrary），卸载后可立刻重新注入 |

## 3. 菜单 Memory 页的操作顺序

1. **Attach**：注入模式下目标就是本进程，进程名填 `self` 或留空都行，Module 填 `client.dll`；
2. **校验当前偏移**：逐项合理性检查（视矩阵第 4 行模长、实体列表非空/字段合理数、
   血量/队伍/生活状态/休眠、坐标来源、场景节点指针、**头骨骼 dz≈66**、脚骨贴地、
   名字可打印、武器句柄、本地 pawn/controller）。报告里 `[BAD]` 会直接写清可疑原因；
3. **骨骼自动标定**：不知道骨骼索引时点它，会扫描骨骼数组、自动反推
   头/颈/胸/骨盆/脚 的索引并写回 `offsets.ini`（同时自动试 `m_modelState+{0x70,0x78,0x80,0x88,0x90}`）；
4. 回到 **Visuals 页** 开方框/骨骼/血量/名字看效果；`Memory` 页的 `scanned / drawn / bones`
   三个计数能立刻说明"读到几个实体、画了几个、几份骨骼成功"。

## 4. CS2 特有的三处结构差异（代码已适配 + 自动探测）

| 字段 | CS2 位置 | 处理方式 |
| --- | --- | --- |
| `m_bDormant` | **CGameSceneNode**(0x103)，不在实体上 | `kDormantFromSceneNode` 开关 + 两边都试，读到非 0/1 自动换另一边 |
| `m_iszPlayerName` | **CBasePlayerController**(0x6F4) | pawn -> `m_hController`(0x13D0) -> 控制器 -> 名字；取不到再退回实体（兼容 CS:GO） |
| `m_bSpotted` | 在 `EntitySpottedState_t` 里(实体+0x1C60+0x8) | 先走嵌套链，读不到退回实体直接读 |
| 武器 ID | weapon + `m_AttributeManager`(0x11A8) + **C_EconItemView**(0x50) + 0x1BA | 中间层偏移会随版本变，校验时自动试 0x40/0x48/0x50/0x58/0x60 并写回可用值 |
| 骨骼 | 场景节点 + `m_modelState`(0x140) + 0x80 | `m_modelState_boneOffset` 可标定；头/脚骨骼索引可自动标定 |

## 5. 配合 AVX-Debugger 交叉验证

`AVX.McpHost`（stdio MCP）已在 `~/.codex/config.toml` 注册为 `avx-debugger`
（备份在 `config.toml.bak-20260917`），重启 Codex 后可直接调用：

- `read_memory` / `read_string`：手动走一遍 `client.dll + dwEntityList -> chunk -> entity`，
  与菜单校验报告里的数字对齐；
- `list_modules`：确认 `client.dll` 基址与 `.text` 范围；
- `first_scan` / `next_scan`：把已知值（血量、坐标）扫出来反向确认字段偏移；
- `disassemble` / `string_scan`：当某个偏移在新版本变了，用特征码/引用找到新的位置。

写内存类工具（`write_memory` / `patch_instruction`）默认关闭，需要时在 `args` 里加
`"--allow-writes"`；`execute_powershell` 需要 `"--allow-command"`。

## 6. 必须清楚的前提

### 崩溃与防崩（重要）

注入版走的是**内部模式**：偏移链里的指针直接解引用，一旦某个偏移不对就是硬崩（曾把 CS2 打崩过一次）。
现在已修：

- 所有内部读取走 SEH 守卫（`core/memory.cpp` 的 `SafeMemcpy`，坏地址只失败不崩）；
- 解引用指针前先 `IsValidRange`（`VirtualQuery`）确认可读；
- 新增「读取失败次数」统计，出现在体检报告里；
- 回归测试：`offsets_garbage.ini`（整套故意写坏的偏移）注入假游戏进程，进程不崩、报告里逐项标 BAD。

- VAC 会检测这类注入与挂钩，本工具链**不包含任何反检测/绕过**，别在官方服务器上用；
  练枪/离线/社区服务器自测。
- `Present` 钩子依赖 CS2 使用 D3D11 + `IDXGISwapChain`；若某次更新后黑屏或界面不出现，
  先用 `get_debug_status` / `list_modules` 确认渲染路径，再考虑改挂 `IDXGISwapChain1`/交换链工厂。
- 全屏独占模式下 DWM 合成路径不同，建议 CS2 用**无边框窗口**跑。
