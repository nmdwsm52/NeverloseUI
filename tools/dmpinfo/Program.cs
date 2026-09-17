// ---------------------------------------------------------------------------
//  DmpInfo —— 不装 WinDbg 也能看崩溃现场的最小 minidump 阅读器
//
//    DmpInfo.exe <dump 文件> [--stack 64] [--context]
//
//  做三件事：
//    1. 打印异常记录（异常码、异常地址）与出错线程的 RIP/RSP；
//    2. 用模块表把 RIP 归到"哪个模块 + 偏移"（是游戏自己的代码，还是注入的 DLL）；
//    3. 从栈顶往下扫，把落在模块区间里的值当作返回地址列出来，给出粗略调用栈。
//
//  只读 dump 文件，不碰任何进程。
// ---------------------------------------------------------------------------
using System.Buffers.Binary;
using System.Text;

namespace DmpInfo;

internal static class Program
{
    private const uint StreamThreadList = 3;
    private const uint StreamModuleList = 4;
    private const uint StreamException = 6;
    private const uint StreamMemory64List = 9;

    private sealed class Module
    {
        public ulong Base;
        public uint Size;
        public string Name = "";
        public ulong End => Base + Size;
    }

    private static byte[] _dump = Array.Empty<byte>();
    private static readonly List<Module> Modules = new();
    private static readonly List<(ulong Start, ulong Size, uint Rva)> MemoryRanges = new();

    private static int Main(string[] args)
    {
        if (args.Length == 0)
        {
            Console.WriteLine("用法: DmpInfo.exe <dump 文件> [--stack N] [--context]");
            return 2;
        }
        var path = args[0];
        var stackBytes = 256;
        var showContext = false;
        for (var i = 1; i < args.Length; ++i)
        {
            if (args[i] == "--stack" && i + 1 < args.Length)
                stackBytes = int.Parse(args[++i]);
            else if (args[i] == "--context")
                showContext = true;
        }
        if (!File.Exists(path))
        {
            Console.Error.WriteLine($"找不到文件: {path}");
            return 2;
        }

        _dump = File.ReadAllBytes(path);
        if (_dump.Length < 32 || Encoding.ASCII.GetString(_dump, 0, 4) != "MDMP")
        {
            Console.Error.WriteLine("不是 minidump（缺少 MDMP 头）");
            return 2;
        }

        var streams = uint.Parse("0");
        var numberOfStreams = BinaryPrimitives.ReadUInt32LittleEndian(_dump.AsSpan(8));
        var dirRva = BinaryPrimitives.ReadUInt32LittleEndian(_dump.AsSpan(12));
        Console.WriteLine($"=== minidump: {Path.GetFileName(path)}  {_dump.Length / 1024 / 1024} MB, {numberOfStreams} 个流 ===");

        uint exceptionRva = 0, threadListRva = 0, moduleListRva = 0;
        for (var i = 0; i < numberOfStreams; ++i)
        {
            var off = (int)dirRva + i * 12;
            var type = BinaryPrimitives.ReadUInt32LittleEndian(_dump.AsSpan(off));
            var rva = BinaryPrimitives.ReadUInt32LittleEndian(_dump.AsSpan(off + 8));
            switch (type)
            {
                case StreamException: exceptionRva = rva; break;
                case StreamThreadList: threadListRva = rva; break;
                case StreamModuleList: moduleListRva = rva; break;
                case StreamMemory64List: ReadMemory64List(rva); break;
            }
        }

        ReadModules(moduleListRva);
        Console.WriteLine($"--- 模块 {Modules.Count} 个（只列可疑的：非系统盘/非 Windows 目录）---");
        foreach (var m in Modules)
        {
            if (m.Name.Contains("\\Windows\\", StringComparison.OrdinalIgnoreCase))
                continue;
            Console.WriteLine($"  0x{m.Base:X16} +0x{m.Size:X8}  {m.Name}");
        }

        // ---- 异常 ----
        ulong faultRip = 0, faultRsp = 0;
        if (exceptionRva != 0)
        {
            var code = BinaryPrimitives.ReadUInt32LittleEndian(_dump.AsSpan((int)exceptionRva + 8));
            var flags = BinaryPrimitives.ReadUInt32LittleEndian(_dump.AsSpan((int)exceptionRva + 12));
            var addr = BinaryPrimitives.ReadUInt64LittleEndian(_dump.AsSpan((int)exceptionRva + 24));
            var nParams = BinaryPrimitives.ReadUInt32LittleEndian(_dump.AsSpan((int)exceptionRva + 32));
            Console.WriteLine();
            Console.WriteLine($"=== 异常 0x{code:X8} ({DescribeException(code)}) flags=0x{flags:X} 异常地址=0x{addr:X} 参数={nParams} ===");
            Console.WriteLine($"    {Describe(addr)}");
            for (var i = 0; i < nParams && i < 4; ++i)
            {
                var p = BinaryPrimitives.ReadUInt64LittleEndian(_dump.AsSpan((int)exceptionRva + 40 + i * 8));
                Console.WriteLine($"    参数{i} = 0x{p:X}" + (p > 0x10000 && p < 0x00007FFFFFFFFFFF ? $"  ({Describe(p)})" : ""));
            }

            // 出错线程的上下文
            var ctxRva = BinaryPrimitives.ReadUInt32LittleEndian(_dump.AsSpan((int)exceptionRva + 160));
            var ctxSize = BinaryPrimitives.ReadUInt32LittleEndian(_dump.AsSpan((int)exceptionRva + 156));
            if (ctxSize >= 0x100 && (ulong)ctxRva + ctxSize <= (ulong)_dump.Length)
            {
                faultRip = BinaryPrimitives.ReadUInt64LittleEndian(_dump.AsSpan((int)ctxRva + 0xF8));
                faultRsp = BinaryPrimitives.ReadUInt64LittleEndian(_dump.AsSpan((int)ctxRva + 0x98));
                Console.WriteLine($"    RIP=0x{faultRip:X}  {Describe(faultRip)}");
                Console.WriteLine($"    RSP=0x{faultRsp:X}");
                if (showContext)
                {
                    string[] regs = { "Rax", "Rcx", "Rdx", "Rbx", "Rsp", "Rbp", "Rsi", "Rdi", "R8 ", "R9 ", "R10", "R11", "R12", "R13", "R14", "R15", "Rip" };
                    for (var i = 0; i < regs.Length; ++i)
                    {
                        var v = BinaryPrimitives.ReadUInt64LittleEndian(_dump.AsSpan((int)ctxRva + 0x78 + i * 8));
                        Console.Write($"{regs[i]}=0x{v:X16}  ");
                        if (i % 4 == 3)
                            Console.WriteLine();
                    }
                    Console.WriteLine();
                }
            }
        }

        // ---- 粗略栈回溯：扫栈上的值，落在模块里的当返回地址 ----
        if (threadListRva != 0)
        {
            var count = BinaryPrimitives.ReadUInt32LittleEndian(_dump.AsSpan((int)threadListRva));
            for (var t = 0; t < count; ++t)
            {
                // MINIDUMP_THREAD: ThreadId@0 SuspendCount@4 PriorityClass@8 Priority@12
                //                  Teb@16 Stack{Start@24, DataSize@32, Rva@36} ThreadContext{Rva@44}
                var off = (int)threadListRva + 4 + t * 48;
                var tid = BinaryPrimitives.ReadUInt32LittleEndian(_dump.AsSpan(off));
                var stackStart = BinaryPrimitives.ReadUInt64LittleEndian(_dump.AsSpan(off + 24));
                var stackSize = BinaryPrimitives.ReadUInt32LittleEndian(_dump.AsSpan(off + 32));
                var stackRva = BinaryPrimitives.ReadUInt32LittleEndian(_dump.AsSpan(off + 36));
                var ctxRva = BinaryPrimitives.ReadUInt32LittleEndian(_dump.AsSpan(off + 44));
                var rip = (ctxRva != 0) ? BinaryPrimitives.ReadUInt64LittleEndian(_dump.AsSpan((int)ctxRva + 0xF8)) : 0;
                var rsp = (ctxRva != 0) ? BinaryPrimitives.ReadUInt64LittleEndian(_dump.AsSpan((int)ctxRva + 0x98)) : 0;
                if (faultRip != 0 && rip != faultRip)
                    continue;   // 只打印出错线程
                Console.WriteLine();
                Console.WriteLine($"=== 出错线程 tid={tid} 栈 0x{stackStart:X}+0x{stackSize:X}（扫 {stackBytes} 字节）===");
                // 优先从 RSP 开始扫（栈里更高处才是调用链）
                var scanStart = stackRva;
                if (rsp >= stackStart && rsp < stackStart + stackSize)
                {
                    var delta = (int)(rsp - stackStart);
                    if (delta + stackBytes < stackSize)
                        scanStart = (uint)(stackRva + delta);
                }
                var bytes = Math.Min(stackBytes, _dump.Length - (int)scanStart);
                for (var i = 0; i + 8 <= bytes; i += 8)
                {
                    var v = BinaryPrimitives.ReadUInt64LittleEndian(_dump.AsSpan((int)scanStart + i));
                    var m = FindModule(v);
                    if (m is null)
                        continue;
                    var mark = (v == rip) ? "  <- RIP" : "";
                    Console.WriteLine($"  [+0x{i:X3}] 0x{v:X16}  {m.Name}+0x{v - m.Base:X}{mark}");
                }
                break;
            }
        }
        return 0;
    }

    private static void ReadModules(uint rva)
    {
        if (rva == 0)
            return;
        var count = BinaryPrimitives.ReadUInt32LittleEndian(_dump.AsSpan((int)rva));
        for (var i = 0; i < count; ++i)
        {
            var off = (int)rva + 4 + i * 108;
            var m = new Module
            {
                Base = BinaryPrimitives.ReadUInt64LittleEndian(_dump.AsSpan(off)),
                Size = BinaryPrimitives.ReadUInt32LittleEndian(_dump.AsSpan(off + 8)),
            };
            var nameRva = BinaryPrimitives.ReadUInt32LittleEndian(_dump.AsSpan(off + 20));
            m.Name = ReadString(nameRva);
            Modules.Add(m);
        }
    }

    private static void ReadMemory64List(uint rva)
    {
        var count = BinaryPrimitives.ReadUInt64LittleEndian(_dump.AsSpan((int)rva));
        var baseRva = BinaryPrimitives.ReadUInt64LittleEndian(_dump.AsSpan((int)rva + 8));
        for (ulong i = 0; i < count && i < 100000; ++i)
        {
            var off = (int)rva + 16 + (int)i * 16;
            var start = BinaryPrimitives.ReadUInt64LittleEndian(_dump.AsSpan(off));
            var size = BinaryPrimitives.ReadUInt64LittleEndian(_dump.AsSpan(off + 8));
            var dataRva = (uint)(baseRva + (ulong)i * 0);   // 数据紧接着上一段，用累加算
            MemoryRanges.Add((start, size, dataRva));
        }
        // 重新按顺序累加真实偏移
        var cursor = baseRva;
        for (var i = 0; i < MemoryRanges.Count; ++i)
        {
            var (s, sz, _) = MemoryRanges[i];
            MemoryRanges[i] = (s, sz, (uint)cursor);
            cursor += sz;
        }
    }

    private static string ReadString(uint rva)
    {
        if (rva == 0 || rva + 4 > _dump.Length)
            return "?";
        var len = BinaryPrimitives.ReadUInt32LittleEndian(_dump.AsSpan((int)rva));
        if (len == 0 || rva + 4 + len > _dump.Length)
            return "?";
        return Encoding.Unicode.GetString(_dump, (int)rva + 4, (int)len);
    }

    private static Module? FindModule(ulong addr)
    {
        foreach (var m in Modules)
        {
            if (addr >= m.Base && addr < m.End)
                return m;
        }
        return null;
    }

    private static string Describe(ulong addr)
    {
        var m = FindModule(addr);
        return m is null ? "（不在任何模块里）" : $"{m.Name}+0x{addr - m.Base:X}";
    }

    private static string DescribeException(uint code) => code switch
    {
        0xC0000005 => "ACCESS_VIOLATION",
        0xC0000096 => "PRIVILEGED_INSTRUCTION",
        0xC000001D => "ILLEGAL_INSTRUCTION",
        0xC00000FD => "STACK_OVERFLOW",
        0xC0000374 => "HEAP_CORRUPTION",
        0x80000003 => "BREAKPOINT",
        0xC0000409 => "STACK_BUFFER_OVERRUN / FAST_FAIL",
        _ => "?",
    };
}
