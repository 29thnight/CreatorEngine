using System.Diagnostics;
using System.Text;
using System.Text.Json;

namespace CreatorBuildTool;

internal sealed record ProcessResult(int ExitCode, int ProcessId, string Output, string Error);

internal sealed class BuildContext : IDisposable
{
    public Options Options { get; }
    public CancellationToken Cancellation { get; }
    private readonly StreamWriter? log;
    private readonly object gate = new();
    public BuildContext(Options options, CancellationToken cancellation)
    {
        Options = options; Cancellation = cancellation;
        Console.OutputEncoding = new UTF8Encoding(false);
        if (options.Get("log-path") is { Length: > 0 } path)
        {
            Paths.NoReparseAncestors(path);
            Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(path))!);
            log = new StreamWriter(path, false, new UTF8Encoding(false)) { AutoFlush = true };
        }
    }
    public void Log(string message, string kind = "log")
    {
        lock (gate)
        {
            log?.WriteLine(message);
            var line = Options.Flag("json") ? JsonSerializer.Serialize(new { schemaVersion = 1, type = kind, message }) : message;
            if (kind == "error" && !Options.Flag("json")) Console.Error.WriteLine(line); else Console.WriteLine(line);
        }
    }
    public void Error(string message) => Log(message, "error");
    public void Result(string path) => Log(path, "result");
    public async Task<ProcessResult> Run(string executable, IEnumerable<string> arguments, string? workingDirectory = null,
        Dictionary<string, string>? environment = null, int timeoutSeconds = 1800, bool check = true, bool echo = true)
    {
        Cancellation.ThrowIfCancellationRequested();
        var start = new ProcessStartInfo(executable) { UseShellExecute = false, CreateNoWindow = true,
            RedirectStandardOutput = true, RedirectStandardError = true, StandardOutputEncoding = Encoding.UTF8, StandardErrorEncoding = Encoding.UTF8 };
        if (workingDirectory != null) start.WorkingDirectory = workingDirectory;
        foreach (var argument in arguments) start.ArgumentList.Add(argument);
        if (environment != null) foreach (var pair in environment) start.Environment[pair.Key] = pair.Value;
        using var process = new Process { StartInfo = start };
        if (!process.Start()) throw new BuildException($"Cannot start {executable}");
        using var job = new ProcessJob(process);
        async Task<string> Read(StreamReader reader)
        {
            var output = new StringBuilder();
            while (await reader.ReadLineAsync() is { } line) { output.AppendLine(line); if (echo) Log(line); }
            return output.ToString();
        }
        var stdout = Read(process.StandardOutput); var stderr = Read(process.StandardError);
        using var deadline = CancellationTokenSource.CreateLinkedTokenSource(Cancellation);
        if (timeoutSeconds > 0) deadline.CancelAfter(TimeSpan.FromSeconds(timeoutSeconds));
        try { await process.WaitForExitAsync(deadline.Token); }
        catch (OperationCanceledException)
        {
            try { process.Kill(entireProcessTree: true); } catch (InvalidOperationException) { }
            await process.WaitForExitAsync();
            await Task.WhenAll(stdout, stderr);
            Cancellation.ThrowIfCancellationRequested();
            throw new BuildException($"{Path.GetFileName(executable)} timed out after {timeoutSeconds} seconds.");
        }
        var result = new ProcessResult(process.ExitCode, process.Id, await stdout, await stderr);
        if (check && result.ExitCode != 0) throw new BuildException($"{Path.GetFileName(executable)} failed with exit code {result.ExitCode}.\n{result.Error}");
        return result;
    }
    public void Dispose() => log?.Dispose();
}
