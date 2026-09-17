# CS2 逆向笔记（用 AVX-Debugger 边调边记）

## 进度总览（每轮更新）

| 轮次 | 内容 | 证据 |
| --- | --- | --- |
| R1 | 定位 CCSGOInput 对象/虚表、视角写入点；建 tools/avx_mcp | 见第 0~2 节 |
| R2 | **不依赖 CreateMove 先把 Misc 做出来**：输入模拟层 + 8 个功能 | 见第 4 节（jumps 计数、aim 距离） |

## 4. Misc 行为层：不靠 CreateMove 就能生效的那一批（R2）

新增 `src/features/input.*`（SendInput + 扫描码的输入模拟）与 `src/features/misc.*`。
思路：能与游戏"用输入对话"的功能先做出来，CreateMove 钩子只留给必须写 cmd 的部分
（anti-aim / silent aim / 精确按键位）。

| 功能 | 实现方式 | 现场证据 |
| --- | --- | --- |
| bhop（**按住跳键才连跳，松手即停**） | 见下方"重按"说明 | `key=0/0 → jumps=0`（没按）；`key=1/1 → jumps=23`（按住 15 秒）；松开后 `jumps` 停在 24 |
| autoStrafe | 只在"用户按住跳键"期间、离地时按视角转动方向交替 A/D | `strafe=1/-1` 随转身翻转 |
| noFlash | 写本地 `m_flFlashMaxAlpha = 0`（关闭恢复 255） | `flash=1` + `[misc] noFlash: m_flFlashMaxAlpha -> 0` |
| triggerbot | 准星 12px 内且可见的敌人 → 点击（带 `triggerDelay`） | `aim=9999→168→76→16px` 随准星靠近敌人下降 |
| autoPistol | 手枪 + 物理左键按住时按射速点射 | 代码路径每帧运行无异常 |
| knifeBot | 2.2m 内可见敌人 → 左键 | 同上 |
| autoPeek | 按住记录原点，松开自动走回去（W/S/A/D 依视角度投影） | 状态位 `peek` |
| edgeJump | **近似**：站地、移动中、按住键时补跳（真 edge jump 要射线判断悬空，留到 CreateMove 阶段） | 代码里如实标注 |

### 连跳的正确语义与"重按"技巧（这一版踩的坑）

用户指出：连跳必须是**"用户按住跳键时才接管，松手立即停"**，不能做成常开自动跳。第一版写成
"站在地面就按跳"，等于玩家不动也会一直跳 —— 错的。

改成"按住才生效"后又踩了第二个坑：**注入的 key-up 会把用户自己的按住状态一起抹掉**。
现象是按住空格 15 秒却只跳 2 次：第一次脉冲（按下→松开）之后，系统里空格已经是"抬起"，
游戏和我们的采样都认为用户松手了，于是不再连跳。

**第二次修（用户反馈"有些地方会断"）**：原来的重按只在"检测到站在地面"的那一帧做，
只要我们的帧没采到落地那一帧（或者两次落地之间正好整帧错过），这次落地就没有新按键 → 断链。
现在改成**按住期间固定 25ms 一个重按周期**（抬起 8ms → 按回），落地无论落在周期里的哪一刻，
最多 25ms 内必然吃到一次全新按键。判据也换成更硬的**起跳/落地次数**（链没断时两者应该相等）：

```
A) 不按:        bhop=0 jump=0  land=0  cyc=0    key=0/0
B) 按住 20 秒:  bhop=1 jump=33 land=33 cyc=504  key=1/1   ← jump==land = 每次落地都立刻起跳，链没断
C) 松开 5 秒:   jump=43 land=43                 （松手瞬间的余量，之后停）
```

（`cyc` 是注入周期数，25ms 一个 ≈ 25/s；`jump` 是"地面→空中"的真实跃迁，才算数。）

正确做法是**重按（re-press）**：落地的瞬间做"抬起 → 约 15ms 后按回"，**最终状态仍然是按下**，
既制造了游戏需要的"新按键"，又保留了用户的按住语义。配套细节：

* 只在**没有注入、也没有重按**的帧采样 `GetAsyncKeyState`，避免把自己的注入当成用户按键；
* 重按后如果玩家还在地面（说明这次按键没被 tick 采到），**250ms 后重试**，否则会卡死不再跳；
* 松手判定：注入的按下已抬起 + 系统状态为抬起 → 判定用户松手，立刻停并清状态；
* 日志里有 `[misc] bhop 跳键状态变化：用户按住=%d（async 原始值=%d）`，对照 `[tick]` 行的
  `key=async/user` 就能复现整个判定过程。

> 这套东西是"没有 CreateMove 时能做到的最好版本"。等 CreateMove 钩子接上，直接改
  `cmd->m_nButtons` 的 `IN_JUMP` 位就能做到真正的"按键级重按"，不再有注入副作用。

遥测都进了 `[tick]` 行：

```
[tick] drawn=1 scanned=232 bones=1 readErr=0 ... misc[bhop=1 jumps=20 strafe=-1 trig=0 flash=1 peek=0 aim=16px]
```

顺带补的基础设施：
* `Memory::WriteRaw/Write`（内部模式直接写 + SEH 兜底；外部模式需要写权限句柄）——
  noFlash 这类"改游戏字段"的功能靠它，后续 aimbot 的 silent aim 也用它；
* `GameLocalState`（pawn/origin/velocity/viewAngles/flags/weaponId/onGround/eyeZ/flashAlphaAddr），
  Misc 与后续 aimbot 共用；
* 卸载路径调用 `MiscShutdown()` → `input::ReleaseAll()`，不会卡键。

## 6. 谁在读 dwViewAngles（R3：找 CreateMove 的关键一步）

对 `client+0x23E2C98`（dwViewAngles）挂 **访问** 监视 1.5 秒（注意：热点地址只挂一两秒，
上次挂 8 秒把游戏搞成"无响应"），抓到 5 条唯一指令，全在同一线程：

| 次数 | 指令 | 说明 |
| --- | --- | --- |
| 272 | `client+0xBA82CC` | 相机/视图方向 |
| 253 | `client+0xB229FA` | **落在虚表[5]那个大函数里（0xB22210..）** —— 命令流水线在读视角 |
| 166 | `client+0xB1CDE5` | 已知"字段拷贝"函数的写入点 |
| 166 | `client+0xB1C1C5` | 另一处拷贝 |
| 98 | `client+0xB291C4` | 已知的第二个写入点 |

结论：**虚表[5]（`client+0xB223E0`）就是命令流水线那一支**（它既读视角、又含
"Duplicating command" 的补命令逻辑），高度符合 `CCSGOInput::CreateMove` 的行为。
入口字节 `85 D2 / 0F 85 EC 12 00 00`（`test edx,edx; jne`）说明它按 `(this, int, ...)`
取值，和我们按 4 个寄存器参数转发的钩子形状一致。

下一步（已实现，见 `src/features/createmove.cpp`）：按 `kCreateMoveIndex`（默认 5）
在 `client+0x23CE610` 这个 CCSGOInput 对象上挂虚表钩子，先**只记录**（
`this/slot/a3/a4` 与调用频率），确认它每 tick 被调用、参数合理之后，再把
bhop/AA/aimbot 接进去。


1. **CreateMove**：仍要接（anti-aim / silent aim / 精确 bhop 按键位都依赖它）。
   已知落点见第 1、2 节；`find_references` 对虚函数无效（只能看到 `call [rax+idx*8]`），
   所以要用"硬件执行断点 + 读寄存器"来确认参数签名 —— 注意别挂热点地址（第 0 节的教训）。
2. **会话类**（autoAccept / revealRanks / clantag / chatSpam / muteEnemy / antiAfk）：
   需要"执行引擎命令"的能力。下一步用调试器找 `ClientCmd`/`Cbuf` 那条链
   （可以从字符串 "sv_cheats" 或命令名反查 `find_references`）。
3. **世界视觉**（chams/glow/夜视/去雾/去镜/FOV/雷达）：`materialsystem2.dll` 材质接口
   + 引擎 convar，先摸虚表再写。
4. **Rage/Legit aimbot**：目标枚举/FOV/部位/平滑都已有原料（PlayerView 的骨骼 + 上面那个
   `aim=xx px` 判据），只差"把角度写进 cmd"或"鼠标移动"两种落地方式。

> 所有地址都写成 **模块相对偏移**（`client.dll+偏移`），换进程/重启游戏照样能用；
> 单次进程里的绝对地址只用于当时的调试。

## 10. 关键突破（R6）：按键位偏移 + Anti-Aim 生效

### 按键位 = `cmd + 0x60`（命令对象里）

用调试器的 CE 式扫描逐次收窄（按住不同键组合，值必须精确跟着变）：

```
first_scan  SPACE        -> value=2   (IN_JUMP)
next_scan   SPACE+CTRL   -> value=6   (IN_JUMP|IN_DUCK)
next_scan   CTRL         -> value=4   (IN_DUCK)
```

三个条件同时成立的地址只剩 **一个**；再把扫描放到"CreateMove 原函数返回之后"（时机很关键：
进入钩子时命令还没填按键位），得到：

| 偏移 | 含义 |
| --- | --- |
| **`cmd + 0x60`** | **命令的按键位**（4 种不同掩码下都精确吻合） |
| `input + 0x250` / `+0x258` | CCSGOInput 里的按键状态（两个相邻 dword） |

连跳实现（`src/features/createmove.cpp`，开关 `bhop_cmd=1`）：**空中清掉 IN_JUMP，落地那一帧
再置上** -> 每次落地都是"全新按下"，不会漏、不需要 SendInput，也没有"注入抹掉用户按住状态"
的问题。开启后 SendInput 那套自动让位。

### Anti-Aim 已生效（写命令角度 = 静默）

命令角度在 `+0x18`(pitch) / `+0x1C`(yaw)，由 **CreateMove 内部**写入 -> 钩子里**先调原函数、
返回前再覆盖**。实测日志：

```
[aa] 已写入命令角度：pitch=89.0 yaw=-1.4（原视角 pitch=1.3 yaw=-1.4）共 2801 次
```

命令里 89°（朝天）、客户端相机仍 1.3°（不动）= 真静默。当前支持 pitch（关/朝天/朝地/水平）、
yawBase（视角/背面/世界）、yawAdd、yawJitter。未做：desync / bodyYaw / fakeLag（需 choke 命令）/
fakeDuck / airStuck / slowWalk。
## 11. Aimbot 静默版已实现（R7）

有了"能改命令角度"的能力之后，aimbot 就是在这条链上做两件事：

1. **目标解算**（每帧，`AimbotTick`）：遍历 `GamePlayerList()`，用 `bonesWorld`（关节世界坐标）
   与本地眼睛位置算 `pitch/yaw`，按 `rage.fov` 过滤，按 `rage.priority`（0=离准星最近 /
   1=离得最近 / 2=血最少）挑一个；部位由 `rage.hitbox`（0=头 1=脖 2=胸 3=骨盆）决定，
   该骨骼没推导出来就往下退。
2. **落地**（`CreateMove`，原函数返回后）：把 `(pitch, yaw)` 写进命令 `+0x18/+0x1C`；
   准星夹角 ≤1.6° 且 `rage.autoFire` 时，再给 `cmd+0x60` 置 `IN_ATTACK(1)`。

因为只改"发给服务器的命令"，**客户端相机一动不动 → 真静默**。按键用 `rage.key`
（现在绑定鼠标侧键 XBUTTON1 = VK 0x05，FOV 20°，瞄头，自动开火开）。

还没实现（需要弹道/伤害模型，下一批做）：`hitchance`、`mindamage`、`autowall`、
`backtrack`、后坐力补偿、以及 legit 模式（鼠标平滑版，走 SendInput 移动鼠标）。
## 12. 视觉类：雷达 + 发光 + FOV 已实现（R8）

这三项不需要材质系统，直接写 schema 字段就行（偏移都是可配置槽位，过期了改 ini 即可）：

| 功能 | 写法 | 偏移 |
| --- | --- | --- |
| 雷达（radarHack） | 给敌人写 `m_bSpotted=1` + `m_bSpottedByMask=0xFFFFFFFF` | `m_entitySpottedState=0x1C60` + `m_bSpotted=0x8`（已有） |
| 发光（glow） | `CGlowProperty::m_bGlowing=1` + `m_glowColorOverride=ARGB` | `m_Glow=0xC00`、`m_bGlowing=0x51`、`m_glowColorOverride=0x40`（来自 [user0x96/CS2-Glow](https://github.com/user0x96/CS2-Glow)） |
| FOV | 本地玩家 `m_pCameraServices -> m_iFOV` | `m_pCameraServices=0x1218`、`m_iFOV=0x290`（已有） |

**安全措施**：glow 偏移在第一次写入前会先校验（`m_bGlowing` 必须读到 0/1、颜色必须是 0 或合理
ARGB），不通过就整块跳过并打日志 `[vis] glow 偏移校验：不通过` —— 宁可功能不生效，也不写坏内存。

还没做（需要引擎/材质接口）：chams、nightMode、removeSmoke、removeScope、bulletImpacts。
## 13. 引擎接口（ConVar）打通 + muteEnemy 生效（R9）

剩下的功能里有一大批本质是"改一个 convar"或"执行一条控制台命令"，所以先把 ConVar 通道做出来。

### ConVar_t 布局（调试器实测，用 sensitivity 验证）

```
+0x00  const char* name         -> "sensitivity"
+0x20  const char* description  -> "Mouse sensitivity."
+0x28  uint32 flags
+0x58  float value              -> 0.95
```

### 查找方式（关键：convar 对象在**堆**上，不在模块数据段）

第一版按"模块数据段里找指向名字字符串的指针"扫 → 找不到（对象和名字字符串都在堆上）。
改成**锚点式**：`dwSensitivity`（官方 dumper = 0x23C9F18）指向 sensitivity 那个 convar 对象，
在它前后各 48MB 的可读内存里扫"`+0x00` 指向的字符串 == 目标名"，再用
`+0x20`（描述指针可读）与 `+0x58`（有限浮点）两个条件确认 —— 三个都过才认，写之前再校验一次。

实测：

```
[cvar] 自检通过：sensitivity 对象=0x5189031EB60 当前值=0.950
[misc] cl_mute_enemy_team = 1（敌人已静音）
```

**muteEnemy 现在是真功能**（找 convar → 校验 → 写 → 读回确认）。这条通道后面可以直接用于
视野/调试开关类功能。

### 同时暴露的一个问题：glow 偏移过期

`m_Glow=0xC00 / m_bGlowing=0x51 / m_glowColorOverride=0x40` 是别人项目里的值，本构建上校验
不通过 → 按设计**整块跳过**（`[vis] glow 偏移校验：不通过`）。下一步要用调试器重新定位这几个
字段（或用当前版本的 schema dump 对一遍），而不是硬写。
## 14. 偏移审计 + glow 修好（R10）

关键收获：找到一份**和本构建完全匹配**的 schema 数据
（[sezzyaep/CS2-OFFSETS](https://github.com/sezzyaep/CS2-OFFSETS)，`offsets.hpp` 里
`dwCSGOInput=0x23E2610`、`dwEntityList=0x2577BE0`、`dwSensitivity=0x23C9F18` 与我们逐个吻合），
里面还有 `client_dll.json`（443KB，全部类的字段偏移）。下载到本地后只 grep 需要的字段，
既不污染上下文，又能把我们的偏移表**逐条对账**。

对账结果（本构建）：

| 字段 | 正确值 | 我们原来的值 | 结论 |
| --- | --- | --- | --- |
| `C_BaseModelEntity::m_Glow` | **0xDE0** | 0xC00 | ❌ 已修（这就是 glow 校验不通过的原因） |
| `CGlowProperty::m_bGlowing` | 0x51 | 0x51 | ✅ |
| `CGlowProperty::m_glowColorOverride` | 0x40 | 0x40 | ✅ |
| `C_BasePlayerPawn::m_pCameraServices` | **0x1240** | 0x1218 | ❌ 已修 |
| `CCSPlayerBase_CameraServices::m_iFOV` | 0x290 | 0x290 | ✅ |
| `m_fFlags` / `m_iHealth` / `m_iTeamNum` | 0x3F4 / 0x34C / 0x3E7 | 同 | ✅ |
| `m_flFlashMaxAlpha` / `m_bIsScoped` / `m_entitySpottedState` | 0x1424 / 0x1C78 / 0x1C60 | 同 | ✅ |
| `m_pWeaponServices` / `m_hActiveWeapon` | 0x1208 / 0x60 | 同 | ✅ |

修完之后实测：

```
[vis] 雷达写入=1263 发光写入=1263 目标=1263 glow偏移=ok FOV=默认
```

**雷达 + 发光都是真的在生效了**（写之前的三条件校验通过，写入后目标数持续增长）。

顺带修掉一个误导项：`[tick]` 里的 `readErr` 之前是累计值，被 convar 大范围扫描（会读到大量
未映射地址）冲到 5 万多；现在改成"本窗口新增"（实测 `+0`），并在 convar 自检后清零。
## 15. chams（渲染色）+ FOV 覆盖（R11）

有了构建匹配的 schema，这两项也是"写字段"就能做：

| 功能 | 字段 | 偏移（schema 实测） | 实测 |
| --- | --- | --- | --- |
| chams（渲染色） | `C_BaseModelEntity::m_clrRender`（Color = 4 字节 R,G,B,A） | 0xC98 | `chams写入=2877` 与雷达/发光同步增长 |
| FOV 覆盖 | `C_BasePlayerPawn::m_pCameraServices (0x1240) -> m_iFOV (0x290)` | 0x1240 / 0x290 | `[vis] FOV = 110（游戏当前算的是 40，我们每帧覆盖）` |

细节：
* chams 写之前也会校验原值像不像颜色（`0xFFFFFFFF`/`0`/alpha 非 0），并记下原值以便关闭时恢复；
* FOV 是**每帧对抗**：游戏每帧重算 FOV，我们每帧覆盖一次，日志必须限速（实测每帧写会产生
  490/750 行日志，已改成 5 秒一条）；
* `m_iFOV` 在相机服务里读到的"游戏当前值"是 40（可能是某个状态字段），但写入 110 后能读回，
  视觉上以用户确认为准。

还没解决：`nightMode`（光照/雾）、`removeSmoke`（粒子）、`removeScope`（覆盖层）、
`bulletImpacts` —— 这些不是实体字段，需要材质/粒子/引擎接口。
## 16. 用户反馈三连修（R12）

### ① 盆骨骨骼点没居中

`pickSpine` 原来对骨盆用的是和脖子/胸口一样的水平容差 8（厘米）。CS2 骨架里**左右胯骨在轴外
8~10cm**，于是骨盆经常被选到胯骨上（画出来就是"骨盆点偏在身体一侧"）。
修法：骨盆必须先按 `dh <= 3.5` 找（贴中轴），实在没有才放宽到 6。

### ② AA 看着没效果 —— 少写了一个字段

命令里的角度（`cmd+0x18/+0x1C`）决定的是**打枪方向**；别人看到的**模型朝向**来自角色身上的
`m_angEyeAngles`（客户端网络同步字段）。只写命令 → 在别人眼里完全没有变化，这正是"AA 没效果"。
修法：AA 现在同时写 `pawn + m_angEyeAngles`（pitch/yaw 各 1 个 float），
并且它**不会带动本地相机**（相机走 `dwViewAngles`/输入角度），所以仍然"自己看不出来"。
怕出问题就加开关关掉：`nl_switch.ini` 里写 `aa_no_eyefield=1`。
遥测：`[aa] 命令角度=(..) 原视角=(..) 命令写入=N 次 模型朝向写入=M 次`。

### ③ 狙击开镜会闪一下

FOV 覆盖是"每帧对抗"（游戏每帧重算 FOV），但**开镜时游戏用的是变焦 FOV**，我们写回去就会
和它打架 → 开镜瞬间闪一下。修法：`m_bIsScoped` 为真时**完全不碰 FOV**。

### 附带：convar 探测结论（决定哪些视觉功能可行）

建了个 ±48MB 窗口的 convar 索引（**8192 个**），查找变成查表（原来每查一个不存在的名字都要
重扫 ~12M 个位置，几秒起步）。探测结果：

| convar | 是否存在 | 用途 |
| --- | --- | --- |
| `cl_mute_enemy_team` | ✅ 值=1 | 静音敌人（已实现） |
| `viewmodel_fov` | ✅ 值=68 | 手模视野（已接线） |
| `cl_clanid` | ✅ 存在 | 战队标签（**只能填数字战队 id**，CS2 没有自定义文本入口） |
| `cl_radar_scale` | ✅ 值=0.7 | 雷达缩放（已接线，菜单加了滑条） |
| `mat_monitorgamma` / `r_fullscreen_gamma` | ❌ 不存在 | 夜视/亮度 —— CS2 已移除这些 convar |
| `r_drawparticles` / `r_drawmodeldecals` / `r_drawsprites` | ❌ 不存在 | 去雾/去弹孔 —— 同样已移除 |
| `fov_cs_debug` / `sv_cheats` / `cl_teamid_overhead_always` | ❌ 不存在 | —— |

结论：**夜视/去雾/去弹孔在 CS2 里不能用 convar 实现**，只能走材质/粒子接口。
## 17. 按键绑定修好（R13）：VK 码 + Hold/Toggle/Always

用户反馈"Mouse3 绑不上、右键不能选 Hold/Toggle/Always"。查代码发现两个真问题：

1. **控件存的是 ImGui 按键枚举**（`ImGuiKey_*`），而运行时用的是 **Windows VK 码**
   （`input::PhysicalKeyHeld(vk)`、`VK_SPACE`）。所以 UI 里绑的键在运行时根本对不上 ✗
   —— 这才是"绑了没反应"的根因，比鼠标侧键的问题更严重。
2. 监听循环里 `continue` 掉了 `ImGuiKey_MouseLeft/Right/Middle`，侧键（Mouse4/5）压根没处理 ✗。

修法：
* 监听时轮询 **Windows VK**（8..255 + `VK_LBUTTON/RBUTTON/MBUTTON/XBUTTON1/XBUTTON2`），
  Esc 清空按键；左/右键留着点控件，不当绑定键；
* 绑定值 = **低 16 位 VK 码 + 高 16 位模式**（`src/features/bind.h`），旧 ini 的纯 VK 值天然兼容
  （模式默认 0 = Hold）；
* 右键绑定框弹出菜单：**Hold / Toggle / Always**，显示名带后缀（`T` = toggle，`A` = always）；
* 运行时统一走 `BindActive(binding)`：Hold = 真人按住、Toggle = 按键边沿翻转、
  Always = 常开 —— aimbot / bhop / trigger / autoPeke / edgeJump 全部改过来了。

### 第三人称：目前没做（如实说明）

schema 里**没有任何 ThirdPerson 字段**（grep 过全表）。有关系的是相机/观察者那一组：
`m_nCameraMode`、`m_vecCameraOffset`、`m_flObserverChaseDistance`、`m_iObserverMode`、
`m_hObserverTarget`，以及 `dwViewRender`（0x23D2258，官方 dumper 给的值）。
做第三人称比较现实的路子是**改 view setup 的原点**（在相机方向的反方向把眼睛往后挪），
需要拿 `dwViewRender` 里的 view setup 结构（原点/角度/FOV 各在什么偏移）—— 这是下一件要做的 RE。
## 18. cs2-sdk.com：签名表是个大金矿（R14）

用户给了 https://cs2-sdk.com/ —— 它是个 SPA，数据来自
[scros22/cs2-universal-offsets](https://github.com/scros22/cs2-universal-offsets) 的 `include/`：
**503 条具名签名**（含 client/engine2/materialsystem2/tier0/particles…）、接口虚表、
以及一份 `verified_features.json`（社区已验证的 7 个功能 + 具体字段/钩子）。

对我们要做的事直接有用的几条：

| 名字 | 模块 | 说明 |
| --- | --- | --- |
| `ConCommand_thirdperson` / `ThirdPersonOnHandler` / `ThirdPersonOffHandler` | client | CS2 **自带 thirdperson 命令** |
| `ThirdPersonReset` / `OverrideView` / `pViewRender` | client | 相机/视图相关 |
| `ExecuteStringCommand` / `ClientCommand` / `RunCommand_Context` | engine2 | 执行控制台命令 |
| `SendChatMessage` / `HudChatPrintf` / `GetChatObject` | client | 聊天相关（chatSpam） |
| `CreateMaterial` / `PrepareSceneMaterial` / `SetMaterialGroup` | materialsystem2/client | 材质（chams） |
| `DrawScopeOverlay` / `DrawSmokeArray` / `DrawSmokeVertex` | client | 去镜/去烟 |
| `GlowObjectManager_GetInstance` / `pGlowManager` / `OnGlowTypeChanged` | client | 发光管理器 |
| `WriteSubtick`（CSGOInputHistoryEntry） | client | 静默打点的**正路**（比写 cmd 角度更正确） |

另外 `verified_features.json` 里的两条经验直接改进了我们现有实现：
1. **FOV**：只写 `m_iFOV` 会被引擎重算覆盖 → 要**同时写 `m_iFOVStart`**（已改进），
   并且**开镜时不写**（我们之前也已按这条修了）；
2. **Aimbot** 的最终角度投递应走 `CSGOInputHistoryEntry::WriteSubtick`（我们目前写 cmd 角度，
   是"外部静默"那条路，能用但不是最正的）。

**注意**：签名表是 build 14175，我们客户端是 14181 → **RVA 不能直接用**（例如它标
`CreateMove=0xC9A010` 而我们的是 `0xB22210` 附近），但**字节模式（pattern）能用** ——
第三人称就是靠这个做出来的。

### 第三人称：已实现（写状态字节，不调用游戏函数）

`ThirdPersonOffHandler` 的模式里自带答案：

```
48 8B 05 ?? ?? ?? ??   mov rax,[rip+?]        ; 取全局对象
C6 80 ?? ?? ?? ?? ??   mov byte [rax+?],0     ; 关掉第三人称
```

于是做法是：扫这段代码 → 解出全局 + 那个字节偏移 → 自己往那儿写 1/0。实测：

```
[tp] 第三人称开关已定位：handler=0x7FF9AC7DE7E0 全局=0x7FF9ADD854E8 偏移=0x22A -> 开关字节=0x7FF9ADD85712
```

注意解出来的偏移是 **0x22A**（别人表里是 0x229）—— 通配符让它在我们这个构建上自己解对了。
菜单 Visuals 页新增 **Third person** 开关 + **TP key** 绑定（支持 Hold/Toggle/Always）。
扫不到模式时整块跳过并打日志，绝不用猜的偏移写内存。
## 0. 怎么用调试器干活

调试器 GUI：`D:\AVX-Debugger\AVX-Debugger\src\AVX.App\bin\Debug\net9.0-windows\AVX.App.exe`
脚本化：`tools/avx_mcp/AvxMcp.exe`（把调试器的 66 个 MCP 工具当命令行用）

```bat
AvxMcp.exe --pid <pid> list
AvxMcp.exe --pid <pid> call get_debug_status
AvxMcp.exe --pid <pid> call list_modules '{"limit":200}'
AvxMcp.exe --pid <pid> call read_memory '{"address":"0x...","length":64}'
AvxMcp.exe --pid <pid> call resolve_function '{"address":"0x..."}'
AvxMcp.exe --pid <pid> call decompile '{"address":"0x..."}'
AvxMcp.exe --pid <pid> call find_references '{"address":"0x...","max_results":10}'
:: 挂地址监视 + 等命中必须在同一个会话里（一个 AvxMcp 进程 = 一个 MCP 会话）
AvxMcp.exe --pid <pid> --allow-writes call watch_address '{"address":"0x...","mode":"write","size":8}' sleep 8000 call get_watch_hits '{"limit":20}'
```

**坑：硬件监视别挂在热点地址上。** 在 `dwViewAngles` 上挂了 8 秒写入监视，命中 1637 次，
游戏随后被 Windows 判为"无响应"（事件日志 13:14:08 Application Hang，无崩溃转储）。
所以：监视只挂几秒、用完立刻 `stop_watch`；`AvxMcp.exe` 现在每次调用结束都会自动 `stop_watch`。

## 1. CCSGOInput（CreateMove 所在对象）

| 项 | 值 | 怎么确认的 |
| --- | --- | --- |
| 对象地址 = `client.dll+0x23E2610` | 这是**内联全局对象**，不是指向对象的指针（offsets.ini 里的 `dwCSGOInput`） | 读该处 8 字节得到 `0x...C05C8BB0`，而它正是虚表地址 |
| 虚表 `client.dll+0x1B58BB0` | 指向它自己的引用有两条 `lea rax,[rel ...]`：`client+0xEAF7C`、`client+0xC8887A`（构造函数/访问器） | `find_references` |
| 虚表前 32 项 | 见下表 | 逐项 `read_memory` |

```
[ 0] client+0xC8B980   [ 1] client+0xCA8660   [ 2] client+0xCBA020   [ 3] client+0xCAC4D0
[ 4] client+0xCB49D0   [ 5] client+0xB223E0   [ 6] client+0xC9B4A0   [ 7] client+0xB3E6E0
[ 8] client+0xC98770   [ 9] client+0xB340B0   [10] client+0xC95B80   [11] client+0xCA88F0
[12] client+0xCB4380   [13] client+0xCB3F60   [14] client+0xCB4500   [15] client+0xCB41D0
[16] client+0xCBA570   [17] client+0xB2EB30   [18] client+0xC91260   [19] client+0xC91410
[20] client+0xC91310   [21] client+0xB201E0   [22] client+0xC91500   [23] client+0xCAAB70
[24] client+0xB3C710   [25] client+0xCB1890   [26] client+0xCAEB50   [27] client+0xCAEBD0
[28] client+0xB3CB10   [29] client+0xB2AE80   [30] client+0xB34F80   [31] client+0xB34F70
```

`[5] client+0xB223E0` 落在函数 `sub_client+0xB22210` 里，反编译出来是**用户命令补给逻辑**：

```
"Last server executed command is %d on tick %d. Duplicating command %d %dx to catch up.
 New outgoing command is %d\n"
```

说明 `[5]` 这一支就是命令流水线（CreateMove/命令队列）附近 —— 下一步要在这里定出
CreateMove 的确切入口与参数签名（`this, int slot, CUserCmd* cmd`）。

## 2. 谁在写 dwViewAngles（client+0x23E2C98）

硬件写入监视 8 秒抓到两条指令（都是 `client.dll` 内、同一线程 tid）：

| 命中次数 | 指令地址 | 所属函数 |
| --- | --- | --- |
| 1123 | `client+0xB1CDE5` | `client+0xB1CD75`（反向拷贝一批字段：`+0x30/+0x38/+0x40/+0x50/+0x460/+0x468`，像是 CUserCmd 的复制/克隆） |
| 514 | `client+0xB291C4` | `client+0xB29174` |

`client+0xB1CD75` 的反编译（节选）读 `rsi + rbx*8 + 0xB58`（数组）与 `r15+r13+0x430/0x438/0x43C`，
再写到 `rdi+0x30..0x468` —— 三个连续的 float（0x430/0x434/0x438 附近）很像角度/移动量，
高度怀疑是 `CUserCmd`/`CBaseUserCmdPB` 的字段复制。**下一步：确认 CUserCmd 的
viewangles / buttons / forwardmove 偏移**（这是 aimbot / anti-aim / bhop 都要写的字段）。

## 7. CreateMove 钩子已跑通（R4）

### 结论：索引是 **25**，不是 5

* 先试了公开资料常见的索引 5：钩子能装上、`self` 也对（就是 `client+0x23E2610` 那个对象），
  但第三参数是**一个 bool**（`arg3=0x...01`，prologue 是 `mov [rax+18h], r8b`），
  读出来的"cmd"全是 `CC CC CC` 未初始化填充 —— 不是 CreateMove。
* 于是把 40 个虚表项逐个反汇编，找"**第三个参数被当指针整字保存**"（`4C 89 40 18`）的那种，
  命中 **`[25] = client+0xCB1890`**（prologue：`48 8B C4 / 4C 89 40 18 / 48 89 48 08`）。
* 挂上之后一次就对：`self` = CCSGOInput 对象，`arg3` = 堆上的 CUserCmd，调用约 **250 次/秒**
  （CS2 的 sub-tick 输入每帧造命令），而命令里的 tick 计数**每秒正好 +64**（= tickrate）✓

```
[cm] CreateMove 调用 250 次/秒 self=0x7FF9B8062610 slot=0 arg3=0x28F537C5E58 ... cmd[0..3]=B775BF00 00007FF9 000039D5 00000000
```

### CUserCmd 前 0x60 字节（实测，配合按住 W / 空格的对照）

| 偏移 | 内容 | 说明 |
| --- | --- | --- |
| +0x00 | `0x7FF9B775BF00` | 虚表（client.dll+0xADBF00） |
| +0x08 | 例 `0x4565` | **tick 计数**（每秒 +64） |
| +0x10 | `0x7FF9B775BF48` | 第二个虚表/接口指针 |
| +0x20 | `1` | flag（预测/槽位相关） |
| +0x38 | `7` | 常量标志 |
| +0x3C / +0x44 | 两个堆指针 | 指向命令明细对象（protobuf 那层） |
| +0x50 | `FF..FF` | 常量（实测与按 W/空格无关） |
| +0x5C | `0x7FF9B7670308` | client.dll 内指针 |

按 W/空格时前 0x60 字节没有对应变化 → **按键位/前后左右移动在 +0x3C / +0x44 指向的子对象里**
（CS2 的 `CUserCmd` 是壳，真正字段在 `CBaseUserCmdPB` 上）。

### 下一步（接着做）

1. 跟 `+0x3C` / `+0x44` / `+0x10` 读子对象，量出 `m_nButtons`、`m_vecViewAngles`
   （多半是指向 QAngle 的指针）、`m_flForwardMove/SideMove`、`m_nTickCount`。
2. 量出来后做**真·按键级**功能：`cmd->buttons |= IN_JUMP` 的精确 bhop、写角度的
   anti-aim / silent aim —— 不再依赖 SendInput，也没有"注入把用户按住状态抹掉"的问题。
3. 探针现在每秒写一行 `[cm]` + 一行 `[cmd]`；字段确认后关掉（`nl_switch.ini` 里
   `no_createmove=1`，或把日志降频）。

## 3. 下一步（按依赖顺序）

1. **定 CreateMove**：从 `[5]` 那支往上找调用者（`find_references` + `resolve_function`），
   确认参数签名与 vtable 索引；用假宿主先验证"钩子能拿到 cmd 指针"。
2. **量 CUserCmd 布局**：在 CreateMove 里 `debug_break` 或对 `cmd->m_nButtons` 下访问监视，
   标出 `viewangles 指针 / m_nButtons / m_flForwardMove / m_flSideMove / m_nTickCount`。
3. **接钩子**：`src/dll/hooks.cpp` 里加 CCSGOInput 的 VMT 钩子（对象就是 `client+0x23E2610`，
   我们的 `VmtHook` 直接可用），把 cmd 传给 `AimbotOnCreateMove()`。
4. **第一个功能做 bhop**（最容易验证：起跳键 + `m_nButtons` 位，落地即跳），
   跑通"钩子 → 写 cmd → 游戏行为变化"这条链路，再往上堆 aimbot / anti-aim / misc。
5. 世界视觉（chams/glow/夜视/去雾/去镜/FOV/雷达）另开一条线：
   `materialsystem2.dll` 的材质 API + 引擎 convar，先用调试器把材质接口虚表摸出来。


**第三次修（用户反馈"没按空格还是跳"）**：把重按改成 25ms 固定周期之后，`g_jumpInjecting` /
`g_repressInFlight` 两个标志**总有一个为真** → 采样窗口被自己的注入占满 → `g_userHoldsJump`
再也刷不成 false → 用户松手后无限连跳。这是必然踩到的坑：**GetAsyncKeyState 分不清
"用户按的"和"我们注入的"**。

修法：加**低级键盘钩子**（`WH_KEYBOARD_LL`，专门一个带消息循环的线程），只统计
**不带 `LLKHF_INJECTED` 的真人事件**；只要收到一次真人 keyup，立刻判定松手并停止注入。

实测（用 `nl_switch.ini` 的 `fake_physical=1` 让我注入的按键也被当真人，才能自测完整周期）：

```
A) 不按:       bhop=0 jump=0  land=0              ← 完全不动
B) 按住 12 秒: bhop=1 jump=19 land=19 cyc=372     ← 每次落地都起跳，链没断
C) 松开 8 秒:  bhop=0 jump=21 land=21             ← 只多松手瞬间 2 次，之后停
```

日志同时给出判据行：`[misc] bhop 跳键：收到真人抬起事件 → 立刻停止连跳`。

## 9. GitHub 参考资料（R5，用户建议）

| 来源 | 有用信息 |
| --- | --- |
| [hzqst/CS2_External](https://github.com/hzqst/CS2_External) | `Offsets.h`: `ClientInput_ViewAngle = 0x688` → **视角就在 `dwCSGOInput + 0x688`**，`SetViewAngle()` 直接写 8 字节 `{Pitch, Yaw}`；`Bunnyhop.hpp` 的连跳靠写一个 "ForceJump" 整数（65537=按下 / 256=抬起），但那一行地址被他们注释成 `0` 了（自己也没启用） |
| [a2x/cs2-dumper offsets.json](https://github.com/a2x/cs2-dumper/blob/main/output/offsets.json) | 当前构建官方偏移：`dwCSGOInput=0x23E2610`、`dwViewAngles=0x23E2C98`（**和 offsets.ini 完全一致**，我们的偏移没过期）；表里**没有** `dwForceJump`（CS2 已移除该全局） |
| [TKazer/CS2-External-Silent-AimBot](https://github.com/TKazer/CS2-External-Silent-AimBot) | 命令结构里 **`+0x18`=pitch、`+0x1C`=yaw**（CreateMove 内部写入）；挂钩改这两个值就是"**改射击方向而镜头不动**"的真静默 |
| [s2v.app SchemaExplorer](https://s2v.app/SchemaExplorer/cs2/client) | 联网类字段偏移查询 |

结论：
1. `dwCSGOInput + 0x688` = `{pitch, yaw}`（8 字节）**可直接写** → anti-aim / 静默的第一条路线；
2. 连跳没有可靠的 force-jump 全局 → 正路就是**在 CreateMove 里写命令按键位**（与我们 hook index 25 拿到的对象一致：它 +0x18/+0x1C 一开始是 0，正好印证"角度是 CreateMove 内部才写进去的"）；
3. 我们的 `dwCSGOInput`/`dwViewAngles` 与官方 dumper 一致 → 后面查字段可以直接拿官方表对。










### 第三人称：修正（第一次没效果的原因）

第一次"定位成功"但用户验证**没有效果**。原因是：我只扫了 Off-handler 的模式，而那个模式
**匹配到了一个形状相似但不相干的函数**（命中 `client+0xB1E7E0`，解出来的偏移是 `+0x22A`），
写了个无关字节，自然没有第三人称。

修正做法（两条经验值得记住）：
1. **用更独特的模式交叉验证**：On-handler 的模式里带 `41 8B 80 50 0B 00 00`（`mov eax,[r8+0xB50]`）
   这个常量，指向性强得多；命中后它写的正是 **`+0x229 = 1`**，和签名表完全一致。
2. **解 rip 相对全局时窗口要往匹配点之前扩**：`mov rax,[rip+disp]` 往往在模式起点**之前**，
   只从匹配点往后找会解不出（第一版就是这样失败的）。

现在的结果：
```
[tp] On 模式命中 = client+0xB20800
[tp] 第三人称开关已定位：全局=0x7FF9ADD854E8 偏移=0x229 -> 开关字节=0x7FF9ADD85711
[tp] 第三人称 = 开（写入 0x7FF9ADD85711）
```

教训记一条：**扫到模式不代表扫对了函数**——要么用更独特的模式，要么拿第二个独立特征交叉验证。

## 19. 第三人称：为什么"开了开关也没用"（R15）

按用户要求，除了 SDK 还去 GitHub 对照了实际项目：

| 来源 | 结论 |
| --- | --- |
| [KKNecmi/ThirdPerson-Revamped](https://github.com/KKNecmi/ThirdPerson-Revamped) | CS2 的第三人称插件（CounterStrikeSharp **服务端**插件），实现方式就是把客户端的 `thirdperson` 命令发给玩家 |
| [scros22/cs2-universal-offsets](https://github.com/scros22/cs2-universal-offsets) 的 `convars.json` | CS2 里第三人称是一组：**`thirdperson`、`thirdpersonshoulder` 命令** + `c_thirdpersonshoulderdist / height / offset / aimdist / c_thirdpersonshoulder` convar |

**关键发现**：`thirdperson` 命令的 flags 是 `0x20004008` = **含 `FCVAR_CHEAT`** →
普通服务器上执行会被拒（所以服务端插件能用、我们客户端用不了）✗。
→ 我们必须**直接写状态**，不能走命令。

**而状态不止一个字节**：`ThirdPersonOffHandler` 里复位时写的是

```
C6 80 ?? ?? ?? ?? 00     mov byte  [rax+?], 0      ; 开关
C7 80 ?? ?? ?? ?? ?? ?? ?? ??   mov dword [rax+?], ?   ; 相机距离/偏移
```

我们之前**只写了开关字节**，距离/偏移没设 → 相机仍然贴在眼睛上，看着就跟没生效一样。
这正是"开了没效果"的原因。已按这个思路改：现在同时解 `C7 80`（dword 写）的偏移与默认值，
准备在开启时一并写入一个像样的距离。（要等游戏重新起来才能实测。）

### 顺带确认的两件事

* convar 索引容量从 8192 提到 3 万+并补了模块数据段（现在 11897 个），但
  `c_thirdpersonshoulder*` 这批**依然找不到** —— 它们不在 `dwSensitivity` 那片堆上，
  字符串兜底扫描也没命中。→ convar 路线暂时不可靠，反而更说明"直接写状态"是对的方向。
* 项目里**没有 inline hook 工具**（只有虚表钩子）。表里有 `OverrideView` 的签名，
  将来要走"钩相机函数"这条路的话，需要先补一个 inline hook 引擎 —— 但那也是
  `DrawScopeOverlay`（去镜）、`SetMaterialGroup`（chams）、`WriteSubtick`（更正的静默）
  的共同前提，值得做。
