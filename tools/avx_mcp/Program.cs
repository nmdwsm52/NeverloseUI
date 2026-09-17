// ---------------------------------------------------------------------------
//  AvxMcp —— AVX-Debugger MCP 客户端（stdio + 行分隔 JSON-RPC 2.0）
//
//    AvxMcp.exe --pid <pid> list
//    AvxMcp.exe --pid <pid> call <工具名> ["{json 参数}"] [call <工具名> ...]
//    AvxMcp.exe --pid <pid> raw "<一行 JSON-RPC>"
//
//  用法要点：
//    * 一次调用里可以串多个 call，省掉反复启动 MCP Host 的开销；
//    * 写操作要加 --allow-writes，执行 PowerShell 要加 --allow-command；
//    * 没写 --pid/--name 时先 set_process 再调别的工具。
// ---------------------------------------------------------------------------
using System.Diagnostics;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace AvxMcp;

internal static class Program
{
    private static string ResolveHostPath()
    {
        var candidates = new[]
        {
            @"D:\AVX-Debugger\AVX-Debugger\src\AVX.App\bin\Debug\net9.0-windows\AVX.McpHost.exe",
            @"D:\AVX-Debugger\AVX-Debugger\src\AVX.McpHost\bin\Debug\net9.0-windows\AVX.McpHost.exe",
            @"D:\AVX-Debugger\AVX-Debugger\src\AVX.McpHost\bin\Release\net9.0-windows\AVX.McpHost.exe",
        };
        foreach (var c in candidates)
        {
            if (File.Exists(c))
                return c;
        }
        throw new FileNotFoundException("找不到 AVX.McpHost.exe，请先 dotnet build 调试器工程");
    }

    private static int Main(string[] args)
    {
        int? pid = null;
        string? name = null;
        var allowWrites = false;
        var allowCommand = false;
        var timeoutMs = 120000;
        var actions = new List<(string kind, string a, string? b)>();

        for (var i = 0; i < args.Length; ++i)
        {
            switch (args[i])
            {
                case "--pid": pid = int.Parse(args[++i]); break;
                case "--name": name = args[++i]; break;
                case "--allow-writes": allowWrites = true; break;
                case "--allow-command": allowCommand = true; break;
                case "--timeout": timeoutMs = int.Parse(args[++i]); break;
                case "list":
                    actions.Add(("list", "", null));
                    break;
                case "call":
                {
                    var tool = args[++i];
                    string? json = null;
                    if (i + 1 < args.Length && args[i + 1].StartsWith('{'))
                        json = args[++i];
                    actions.Add(("call", tool, json));
                    break;
                }
                case "raw":
                    actions.Add(("raw", args[++i], null));
                    break;
                case "sleep":
                    actions.Add(("sleep", args[++i], null));
                    break;
                default:
                    Console.Error.WriteLine($"未知参数: {args[i]}");
                    PrintUsage();
                    return 2;
            }
        }

        if (actions.Count == 0)
        {
            PrintUsage();
            return 2;
        }

        var host = ResolveHostPath();
        var psi = new ProcessStartInfo(host)
        {
            RedirectStandardInput = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false,
            CreateNoWindow = true,
        };
        if (pid.HasValue)
        {
            psi.ArgumentList.Add("--pid");
            psi.ArgumentList.Add(pid.Value.ToString());
        }
        else if (name is not null)
        {
            psi.ArgumentList.Add("--name");
            psi.ArgumentList.Add(name);
        }
        if (allowWrites)
            psi.ArgumentList.Add("--allow-writes");
        if (allowCommand)
            psi.ArgumentList.Add("--allow-command");

        using var proc = Process.Start(psi) ?? throw new InvalidOperationException("启动 MCP Host 失败");
        var stderr = new StringBuilder();
        proc.ErrorDataReceived += (_, e) =>
        {
            if (e.Data is not null)
                stderr.AppendLine(e.Data);
        };
        proc.BeginErrorReadLine();

        var stdin = proc.StandardInput;
        var stdout = proc.StandardOutput;
        var nextId = 1;

        JsonObject? Call(string method, JsonObject? ps)
        {
            var req = new JsonObject
            {
                ["jsonrpc"] = "2.0",
                ["id"] = nextId++,
                ["method"] = method,
            };
            if (ps is not null)
                req["params"] = ps;
            var line = req.ToJsonString(new JsonSerializerOptions { Encoder = System.Text.Encodings.Web.JavaScriptEncoder.UnsafeRelaxedJsonEscaping });
            stdin.WriteLine(line);
            stdin.Flush();
            var sw = Stopwatch.StartNew();
            while (sw.ElapsedMilliseconds < timeoutMs)
            {
                var respLine = stdout.ReadLine();
                if (respLine is null)
                {
                    Console.Error.WriteLine("[x] MCP Host 提前退出");
                    if (stderr.Length > 0)
                        Console.Error.WriteLine(stderr.ToString());
                    return null;
                }
                if (respLine.Trim().Length == 0)
                    continue;
                JsonNode? node;
                try { node = JsonNode.Parse(respLine); }
                catch { continue; }   // 非 JSON 的输出（日志）直接跳过
                if (node is not JsonObject obj)
                    continue;
                if (obj["id"] is null)
                    continue;         // 通知
                return obj;
            }
            Console.Error.WriteLine($"[x] {method} 超时（{timeoutMs}ms）");
            return null;
        }

        // ---- initialize ----
        var init = Call("initialize", new JsonObject
        {
            ["protocolVersion"] = "2024-11-05",
            ["capabilities"] = new JsonObject(),
            ["clientInfo"] = new JsonObject { ["name"] = "AvxMcp", ["version"] = "1.0" },
        });
        if (init is null)
            return 3;
        var serverName = init["result"]?["serverInfo"]?["name"]?.ToString() ?? "?";
        Console.WriteLine($"[+] MCP 已连接: {serverName}  (host={Path.GetFileName(host)})");
        stdin.WriteLine(new JsonObject { ["jsonrpc"] = "2.0", ["method"] = "notifications/initialized" }.ToJsonString());
        stdin.Flush();

        var exit = 0;
        foreach (var (kind, a, b) in actions)
        {
            if (kind == "list")
            {
                var resp = Call("tools/list", null);
                var tools = resp?["result"]?["tools"]?.AsArray();
                if (tools is null)
                {
                    Console.Error.WriteLine("[x] tools/list 失败");
                    exit = 1;
                    continue;
                }
                Console.WriteLine($"[+] 共 {tools.Count} 个工具:");
                foreach (var t in tools)
                    Console.WriteLine($"    {t?["name"],-28} {t?["description"]?.ToString()?.Split('\n')[0]}");
                continue;
            }

            if (kind == "raw")
            {
                stdin.WriteLine(a);
                stdin.Flush();
                var respLine = stdout.ReadLine();
                Console.WriteLine(respLine);
                continue;
            }

            if (kind == "sleep")
            {
                // 有些工具是"挂上监视后等命中"的模型：必须在同一个 MCP 会话里等一会儿再取结果
                var ms = int.Parse(a);
                Console.WriteLine($"[~] 等待 {ms}ms ...");
                Thread.Sleep(ms);
                continue;
            }

            // call
            JsonObject arguments;
            if (string.IsNullOrWhiteSpace(b))
            {
                arguments = new JsonObject();
            }
            else
            {
                arguments = JsonNode.Parse(b!)?.AsObject() ?? new JsonObject();
            }
            var callResp = Call("tools/call", new JsonObject
            {
                ["name"] = a,
                ["arguments"] = arguments,
            });
            if (callResp is null)
            {
                exit = 1;
                continue;
            }
            if (callResp["error"] is JsonObject err)
            {
                Console.Error.WriteLine($"[x] {a} 出错: {err["message"]}");
                exit = 1;
                continue;
            }
            var content = callResp["result"]?["content"]?.AsArray();
            var isError = callResp["result"]?["isError"]?.GetValue<bool>() ?? false;
            Console.WriteLine($"=== {a} {(isError ? "(isError)" : "")} ===");
            if (content is not null)
            {
                foreach (var item in content)
                {
                    var text = item?["text"]?.ToString();
                    if (text is not null)
                        Console.WriteLine(text);
                }
            }
            else
            {
                Console.WriteLine(callResp["result"]?.ToJsonString(new JsonSerializerOptions { WriteIndented = true }));
            }
        }

        try
        {
            // 收尾必做：硬断点/地址监视挂在热点地址上会把游戏拖到"无响应"，
            // 所以任何一次调用结束前都主动释放掉
            Call("stop_watch", null);
            Call("list_breakpoints", null);
            Call("shutdown", null);
            stdin.Close();
            if (!proc.WaitForExit(3000))
                proc.Kill(true);
        }
        catch
        {
            // 收尾失败无所谓
        }
        return exit;
    }

    private static void PrintUsage()
    {
        Console.WriteLine("""
            AvxMcp —— AVX-Debugger MCP 客户端

              AvxMcp.exe --pid <pid> list
              AvxMcp.exe --pid <pid> call read_memory --json "{\"address\":\"0x1234\",\"size\":64}"
              AvxMcp.exe --pid <pid> call get_debug_status call list_modules
              AvxMcp.exe --allow-writes --pid <pid> call write_memory --json "{...}"
            """);
    }
}
