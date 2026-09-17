// ---------------------------------------------------------------------------
//  AvxInject —— 用 AVX-Debugger 的内核桥接注入 DLL（命令行版）
//
//    AvxInject status
//    AvxInject inject --pid <pid> | --name <进程名> --dll <DLL 路径> [--no-wait]
//    AvxInject query  --base 0x<ImageBase>
//    AvxInject modules --pid <pid> | --name <进程名> [--find <子串>]
//
//  注入路径 = AVX.App 里 InjectPanel 的注入按钮：
//    KernelSession.TryOpen() -> PE 校验 -> KernelBridge.ManualMap(pid, image)
//    -> ManualMapQuery(imageBase) 轮询握手状态
//  手动映射（manual map）不改目标进程的模块链表，所以注入后 list_modules 里看不到；
//  判断是否生效要看握手状态 + 模块自己写出的日志 / 画面。
// ---------------------------------------------------------------------------
using System.Diagnostics;
using System.Runtime.InteropServices;
using AVX.Core.Kernel;

namespace AvxInject;

internal static class Program
{
    private static int Main(string[] args)
    {
        if (args.Length == 0 || args[0] is "-h" or "--help" or "help")
        {
            PrintUsage();
            return args.Length == 0 ? 2 : 0;
        }

        try
        {
            switch (args[0].ToLowerInvariant())
            {
                case "status": return Status();
                case "inject": return Inject(args[1..]);
                case "query": return Query(args[1..]);
                case "modules": return Modules(args[1..]);
                default:
                    Console.Error.WriteLine($"未知命令: {args[0]}");
                    PrintUsage();
                    return 2;
            }
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine("[x] 异常: " + ex);
            return 3;
        }
    }

    private static void PrintUsage()
    {
        Console.WriteLine("""
            AvxInject —— AVX-Debugger 内核桥接注入器

              AvxInject status
              AvxInject inject --pid <pid> | --name <进程名> --dll <DLL 路径> [--no-wait]
              AvxInject query  --base 0x<ImageBase>
              AvxInject modules --pid <pid> | --name <进程名> [--find <子串>]
            """);
    }

    // ------------------------------------------------------------------ status
    private static int Status()
    {
        var ok = KernelSession.TryOpen();
        Console.WriteLine($"kernel_bridge={(ok ? "open" : "closed")}");
        if (!ok)
        {
            Console.WriteLine($"kernel_open_error=Win32 {KernelSession.LastOpenWin32Error} " +
                              $"({new System.ComponentModel.Win32Exception(KernelSession.LastOpenWin32Error).Message})");
            Console.WriteLine("提示：先用 KDU / 服务把 AVXKernel.sys 加载起来，再重试。");
            return 1;
        }

        var bridge = KernelSession.Bridge!;
        Console.WriteLine($"ping={bridge.Ping()}");
        var v = bridge.GetVersion();
        if (v is not null)
        {
            var ver = v.Value;
            Console.WriteLine($"driver_version={ver.MajorVersion}.{ver.MinorVersion}.{ver.BuildNumber}  " +
                              $"features=0x{ver.SupportedFeatures:X}  " +
                              $"manual_map_max=0x{KernelNative.AVX_MANUAL_MAP_MAX_IMAGE_SIZE:X}");
        }
        return 0;
    }

    // ------------------------------------------------------------------ inject
    private static int Inject(string[] args)
    {
        var pid = 0;
        var name = (string?)null;
        var dll = (string?)null;
        var wait = true;

        for (var i = 0; i < args.Length; ++i)
        {
            switch (args[i])
            {
                case "--pid": pid = int.Parse(args[++i]); break;
                case "--name": name = args[++i]; break;
                case "--dll": dll = args[++i]; break;
                case "--no-wait": wait = false; break;
                default:
                    Console.Error.WriteLine($"未知参数: {args[i]}");
                    return 2;
            }
        }

        if (pid == 0 && name is not null)
        {
            var procs = Process.GetProcessesByName(Path.GetFileNameWithoutExtension(name));
            if (procs.Length == 0)
            {
                Console.Error.WriteLine($"[x] 找不到进程 {name}");
                return 2;
            }
            pid = procs[0].Id;
            Console.WriteLine($"[+] {name} -> pid {pid}");
        }
        if (pid == 0)
        {
            Console.Error.WriteLine("[x] 需要 --pid 或 --name");
            return 2;
        }
        if (string.IsNullOrWhiteSpace(dll) || !File.Exists(dll))
        {
            Console.Error.WriteLine($"[x] DLL 不存在: {dll}");
            return 2;
        }

        if (!KernelSession.TryOpen())
        {
            Console.Error.WriteLine($"[x] 内核桥接未打开（Win32 {KernelSession.LastOpenWin32Error}）：" +
                                    "AVXKernel.sys 没加载？");
            return 1;
        }
        var bridge = KernelSession.Bridge!;

        var full = Path.GetFullPath(dll);
        var image = File.ReadAllBytes(full);
        Console.WriteLine($"[+] DLL {image.Length} 字节: {full}");
        if (!ValidatePe(image, out var peError))
        {
            Console.Error.WriteLine("[x] PE 校验失败: " + peError);
            return 2;
        }
        if (image.Length > KernelNative.AVX_MANUAL_MAP_MAX_IMAGE_SIZE)
        {
            Console.Error.WriteLine($"[x] 镜像超过驱动上限 " +
                                    $"0x{KernelNative.AVX_MANUAL_MAP_MAX_IMAGE_SIZE:X}");
            return 2;
        }
        Console.WriteLine("[+] PE 校验通过（x64 / PE32+）");

        var sw = Stopwatch.StartNew();
        var ok = bridge.ManualMap(pid, image, out var imageBase, out var error);
        sw.Stop();
        if (!ok)
        {
            Console.Error.WriteLine($"[x] ManualMap 失败（{sw.ElapsedMilliseconds}ms）: {error}");
            return 1;
        }

        Console.WriteLine($"[+] ManualMap 已提交: ImageBase=0x{imageBase:X}  ({sw.ElapsedMilliseconds}ms)");
        Console.WriteLine($"    （手动映射不会出现在模块链表里，list_modules 查不到是正常的）");
        if (!wait)
            return 0;

        return PollLoop(bridge, imageBase);
    }

    // ------------------------------------------------------------------- query
    private static int Query(string[] args)
    {
        ulong imageBase = 0;
        for (var i = 0; i < args.Length; ++i)
        {
            if (args[i] == "--base")
            {
                var text = args[++i].Replace("0x", "", StringComparison.OrdinalIgnoreCase);
                imageBase = Convert.ToUInt64(text, 16);
            }
        }
        if (imageBase == 0)
        {
            Console.Error.WriteLine("[x] 需要 --base 0x<ImageBase>");
            return 2;
        }
        if (!KernelSession.TryOpen())
        {
            Console.Error.WriteLine("[x] 内核桥接未打开");
            return 1;
        }
        return PollLoop(KernelSession.Bridge!, imageBase);
    }

    // ----------------------------------------------------------------- modules
    private static int Modules(string[] args)
    {
        var pid = 0;
        string? find = null;
        for (var i = 0; i < args.Length; ++i)
        {
            switch (args[i])
            {
                case "--pid": pid = int.Parse(args[++i]); break;
                case "--name":
                {
                    var procs = Process.GetProcessesByName(Path.GetFileNameWithoutExtension(args[++i]));
                    if (procs.Length == 0)
                    {
                        Console.Error.WriteLine("[x] 找不到进程");
                        return 2;
                    }
                    pid = procs[0].Id;
                    break;
                }
                case "--find": find = args[++i]; break;
            }
        }
        if (pid == 0)
        {
            Console.Error.WriteLine("[x] 需要 --pid 或 --name");
            return 2;
        }
        if (!KernelSession.TryOpen())
        {
            Console.Error.WriteLine("[x] 内核桥接未打开");
            return 1;
        }
        var list = KernelSession.Bridge!.EnumModules(pid);
        if (list is null)
        {
            Console.Error.WriteLine("[x] EnumModules 失败");
            return 1;
        }
        var count = 0;
        foreach (var m in list)
        {
            if (find is not null && !m.Name.Contains(find, StringComparison.OrdinalIgnoreCase))
                continue;
            Console.WriteLine($"0x{m.BaseAddress:X16}  size=0x{m.SizeOfImage:X8}  entry=0x{m.EntryPoint:X16}  {m.Name}");
            ++count;
        }
        Console.WriteLine($"[+] 共 {list.Count} 个模块，匹配 {count} 个");
        return 0;
    }

    // ------------------------------------------------------------ 握手状态轮询
    private static int PollLoop(KernelBridge bridge, ulong imageBase)
    {
        for (var i = 0; i < 12; ++i)
        {
            Thread.Sleep(500);
            var q = bridge.ManualMapQuery(imageBase);
            if (q is null)
            {
                Console.Error.WriteLine("[x] ManualMapQuery 失败");
                return 1;
            }
            var r = q.Value;
            if (r.Status != 0)
            {
                Console.Error.WriteLine($"[x] 查询失败 NTSTATUS=0x{r.Status:X8} " +
                                        $"(sessions={r.SessionCount} init={r.SessionsInit} " +
                                        $"first=0x{r.Session0ImageBase:X} req=0x{r.RequestImageBase:X} " +
                                        $"found={r.FoundIndex})");
                return 1;
            }

            Console.WriteLine($"[{i * 500,5}ms] 状态={DescribeState(r.State)} " +
                              $"pending_apc={r.PendingApcs} iret_skip={r.IretSkipCount} " +
                              $"epResult={r.EntryPointResult} " +
                              $"vad(image/loader/stub)={r.VadMergeImage}/{r.VadMergeLoader}/{r.VadMergeStub}");

            var msg = new string(r.ErrorMsg).TrimEnd('\0');
            if (msg.Length > 0)
                Console.WriteLine("         " + msg);

            switch (r.State)
            {
                case KernelNative.AVX_MANUAL_MAP_STATE_COMPLETED:
                    Console.WriteLine("[+] 注入完成：DllMain 已返回");
                    return 0;
                case KernelNative.AVX_MANUAL_MAP_STATE_IMPORT_FAIL:
                case KernelNative.AVX_MANUAL_MAP_STATE_EP_FAIL:
                case KernelNative.AVX_MANUAL_MAP_STATE_SKIPPED:
                    Console.Error.WriteLine("[x] 注入失败（见上面的 loader 消息）");
                    return 1;
                case KernelNative.AVX_MANUAL_MAP_STATE_EP_RUNNING when r.PendingApcs == 0:
                    Console.WriteLine("[+] DllMain 已执行且 APC 派发完毕（驱动里没等到返回）：" +
                                      "按已生效处理");
                    return 0;
            }
        }
        Console.Error.WriteLine("[x] 6 秒内没有拿到终态（DllMain 可能还在跑）");
        return 1;
    }

    private static string DescribeState(int state) => state switch
    {
        KernelNative.AVX_MANUAL_MAP_STATE_NONE => "排队中（loader 未开始）",
        KernelNative.AVX_MANUAL_MAP_STATE_RUNNING => "Loader 执行中",
        KernelNative.AVX_MANUAL_MAP_STATE_COMPLETED => "完成（DllMain 已返回）",
        KernelNative.AVX_MANUAL_MAP_STATE_IMPORT_FAIL => "导入解析失败",
        KernelNative.AVX_MANUAL_MAP_STATE_EP_FAIL => "DllMain 返回 FALSE",
        KernelNative.AVX_MANUAL_MAP_STATE_SKIPPED => "APC 未派发/iret 帧无效",
        KernelNative.AVX_MANUAL_MAP_STATE_EP_RUNNING => "DllMain 已执行",
        _ => $"未知({state})",
    };

    // ------------------------------------------------------------- PE 校验
    //  与 AVX.App InjectPanel.ValidatePe 同一套判据（能过 GUI 注入的 DLL 也一定能过这里）
    private static bool ValidatePe(byte[] image, out string error)
    {
        error = string.Empty;
        if (image.Length < 0x40 || image[0] != (byte)'M' || image[1] != (byte)'Z')
        {
            error = "不是有效的 PE 文件（缺少 MZ 头）";
            return false;
        }

        var pe = BitConverter.ToInt32(image, 0x3C);
        if (pe < 0 || pe + 24 > image.Length)
        {
            error = "PE 头偏移无效";
            return false;
        }
        if (image[pe] != (byte)'P' || image[pe + 1] != (byte)'E' ||
            image[pe + 2] != 0 || image[pe + 3] != 0)
        {
            error = "PE 签名无效";
            return false;
        }

        var machine = BitConverter.ToUInt16(image, pe + 4);
        if (machine != 0x8664)
        {
            error = $"不是 x64 DLL（Machine=0x{machine:X}）";
            return false;
        }

        var numSections = BitConverter.ToUInt16(image, pe + 6);
        if (numSections == 0 || numSections > 96)
        {
            error = $"节数量无效: {numSections}";
            return false;
        }

        var optSize = BitConverter.ToUInt16(image, pe + 20);
        if (optSize < 0xC0 || pe + 24 + optSize + numSections * 40 > image.Length)
        {
            error = "OptionalHeader 异常";
            return false;
        }
        if (BitConverter.ToUInt16(image, pe + 24) != 0x20B)
        {
            error = "不是 PE32+ 镜像";
            return false;
        }

        var entryPoint = BitConverter.ToUInt32(image, pe + 24 + 0x10);
        var sizeOfImage = BitConverter.ToUInt32(image, pe + 24 + 0x38);
        var sizeOfHeaders = BitConverter.ToUInt32(image, pe + 24 + 0x3C);
        if (sizeOfImage == 0 || sizeOfImage < sizeOfHeaders || sizeOfImage > 0x40000000u)
        {
            error = $"SizeOfImage 无效: 0x{sizeOfImage:X}";
            return false;
        }
        if (sizeOfHeaders == 0 || sizeOfHeaders > image.Length || sizeOfHeaders > sizeOfImage)
        {
            error = $"SizeOfHeaders 无效: 0x{sizeOfHeaders:X}";
            return false;
        }
        if (entryPoint != 0 && entryPoint >= sizeOfImage)
        {
            error = "入口点超出镜像范围";
            return false;
        }

        var sectionTable = pe + 24 + optSize;
        for (var i = 0; i < numSections; ++i)
        {
            var off = sectionTable + i * 40;
            var virtualSize = BitConverter.ToUInt32(image, off + 8);
            var virtualAddr = BitConverter.ToUInt32(image, off + 12);
            var rawSize = BitConverter.ToUInt32(image, off + 16);
            var rawPtr = BitConverter.ToUInt32(image, off + 20);
            if (rawSize != 0 && (ulong)rawPtr + rawSize > (ulong)image.Length)
            {
                error = $"节 {i} 的原始数据超出文件范围";
                return false;
            }
            if ((ulong)virtualAddr + Math.Max(virtualSize, rawSize) > sizeOfImage)
            {
                error = $"节 {i} 超出 SizeOfImage";
                return false;
            }
        }

        var dataDir = pe + 24 + 0x70;
        foreach (var dir in new[] { 1, 3, 5, 9 })   // import / exception / basereloc / TLS
        {
            var off = dataDir + dir * 8;
            var rva = BitConverter.ToUInt32(image, off);
            var size = BitConverter.ToUInt32(image, off + 4);
            if (rva == 0)
                continue;
            if ((ulong)rva + size > sizeOfImage)
            {
                error = $"数据目录 {dir} 超出镜像范围";
                return false;
            }
        }
        return true;
    }
}
