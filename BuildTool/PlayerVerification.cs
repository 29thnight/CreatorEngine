using System.Text.Json.Nodes;
using System.Text.RegularExpressions;

namespace CreatorBuildTool;

internal static class PlayerVerification
{
    public static async Task<JsonObject> Verify(BuildContext context, string stage, string temp, string tempOwner,
        Preflight preflight, FileEntry[] entries, bool shipping, int frames, int timeout)
    {
        Paths.AssertChild(temp, tempOwner);
        if (Directory.Exists(temp)) throw new BuildException("Player verify temp already exists.");
        Directory.CreateDirectory(temp);
        var windows = Environment.GetFolderPath(Environment.SpecialFolder.Windows);
        var result = await context.Run(Path.Combine(stage, "Player.exe"), ["--smoke", frames.ToString()], stage,
            new() { ["TEMP"] = temp, ["TMP"] = temp, ["PATH"] = string.Join(Path.PathSeparator, stage, Path.Combine(windows, "System32"), windows) }, timeout, check: false);
        File.WriteAllText(Path.Combine(stage, "verify.stdout.log"), result.Output); File.WriteAllText(Path.Combine(stage, "verify.stderr.log"), result.Error);
        var combined = result.Output + "\n" + result.Error;
        foreach (var logRoot in new[] { Path.Combine(stage, "Log"), Path.Combine(temp, $"CreatorEngine/Player/{result.ProcessId}/RuntimeData/Log") })
            if (Directory.Exists(logRoot)) foreach (var file in Paths.Files(logRoot).Where(p => Path.GetExtension(p).ToLowerInvariant() is ".html" or ".log" or ".txt")) combined += "\n" + File.ReadAllText(file);
        if (result.ExitCode != 0) throw new BuildException($"Player smoke failed with exit code {result.ExitCode}. Logs: {temp}");
        var metrics = ValidateMarkers(result.Output, combined, preflight, shipping, frames);
        var playerRoot = Path.Combine(temp, "CreatorEngine/Player");
        var owners = Directory.Exists(playerRoot) ? Directory.GetDirectories(playerRoot) : [];
        if (owners.Length != 1 || Path.GetFileName(owners[0]) != result.ProcessId.ToString()) throw new BuildException("Player runtime owner is not exactly the launched PID.");
        var unpacked = Path.Combine(owners[0], "RuntimeContent");
        var unpackedFiles = Paths.Files(unpacked).ToArray();
        if (unpackedFiles.Length != entries.Length || unpackedFiles.Any(p => Path.GetExtension(p).ToLowerInvariant() is ".meta" or ".json") ||
            !File.Exists(Path.Combine(unpacked, "Assets/Derived/asset-manifest.cemf"))) throw new BuildException("Unpacked runtime content closure mismatch.");
        Metadata.Verify(unpacked, entries, context.Cancellation);
        Paths.DeleteTree(temp, tempOwner); return metrics;
    }
    public static JsonObject ValidateMarkers(string stdout, string combined, Preflight preflight, bool shipping, int frames)
    {
        static Match Require(string text, string pattern, string name)
        { var match = Regex.Match(text, pattern); return match.Success ? match : throw new BuildException($"Player smoke missing {name}."); }
        var catalog = Require(stdout, @"\[asset\.catalog\]\s*source=cemf\s+identities=([1-9]\d*)\s+metaParsed=0", "CEMF-only catalog");
        var cooked = Require(stdout, @"\[cooked\.catalog\]\s*mount\s+[^\r\n]*\s+entries=([1-9]\d*)\s+sources=([1-9]\d*)\s+stale=(\d+)", "cooked catalog");
        if (catalog.Groups[1].Value != cooked.Groups[2].Value) throw new BuildException("Player catalog source counts differ.");
        var cookedSceneDocuments = Regex.Matches(stdout, @"\[scene\.document\]\s*source=cooked\s+guid=[0-9a-f-]{36}").Count;
        if (cookedSceneDocuments < 1 || Regex.IsMatch(stdout, @"\[scene\.document\]\s*source=authoring\s+guid=[0-9a-f-]{36}")) throw new BuildException("Player did not use only cooked scene documents.");
        var service = Require(stdout, @"\[player\.service\]\s*compiled=(yes|no)\s+enabled=(yes|no)", "configuration marker");
        if ((service.Groups[1].Value == "yes") == shipping) throw new BuildException("Player ran the wrong Shipping/Development configuration.");
        var parser = Require(stdout, @"\[runtime\.text-parser\]\s*calls=(\d+)", "text-parser counter");
        if (parser.Groups[1].Value != "0") throw new BuildException("Player runtime called an authoring text parser.");
        Require(combined, @"Scene loaded:[^\r\n]*" + Regex.Escape(preflight.StartupScene), "startup scene success");
        var rows = stdout.Split('\n').Where(p => p.StartsWith("[player.smoke] ")).Select(p => JsonNode.Parse(p[15..])!).ToArray();
        if (rows.Length != 1 || rows[0].Int("schemaVersion") != 1 || !rows[0].Bool("ready") || rows[0].Int("registeredScriptTypes") <= 0) throw new BuildException("Invalid managed runtime readiness result.");
        if (preflight.RequiresManagedLifecycle)
        {
            var initialized = Regex.Matches(stdout, @"\[SMOKE\]\s*managed OnInitialized:\s*PackageSmokeProbe");
            var simulation = Regex.Matches(stdout, @"\[SMOKE\]\s*managed OnBeginSimulation:\s*PackageSmokeProbe");
            if (initialized.Count != 1 || simulation.Count != 1 || initialized[0].Index >= simulation[0].Index) throw new BuildException("Managed lifecycle count/order mismatch.");
        }
        var end = Require(combined, @"\[SMOKE\]\s*frame limit reached[^\r\n]*\((\d+)\s+GT frames,\s*display frame\s+(\d+),\s*promotions\s+(\d+)\)", "clean exit");
        var gameFrames = int.Parse(end.Groups[1].Value); var display = int.Parse(end.Groups[2].Value); var promotions = int.Parse(end.Groups[3].Value);
        if (gameFrames < frames || display < 2 || promotions < 2) throw new BuildException("Player frame/display progress is insufficient.");
        foreach (var pattern in new[] { @"\[SMOKE\]\s*startup scene load FAILED", @"\[SMOKE\]\s*render pipeline FAILED", @"\[model\.generation\]\s*게시 전 검증 실패", "MeshRenderer 모델 generation 해석 실패",
            @"\[CRASH\]", @"\[CLR\].*실패", @"\[RenderBackend\].*(실패|오류)", @"Failed to load PhysXGpu_64\.dll", "GPU solver/Bp pipeline failed", "typed ops 미등록 타입", @"PxScene::simulate\(\) called with a zero elapsedTime" })
            if (Regex.IsMatch(combined, pattern, RegexOptions.IgnoreCase)) throw new BuildException($"Player failure marker: {pattern}");
        return Metadata.Object(new { exitCode = 0, gameThreadFrames = gameFrames, displayFrame = display, promotions,
            registeredScripts = rows[0].Int("registeredScriptTypes"), managedLifecycle = preflight.RequiresManagedLifecycle,
            catalogSource = "cemf", metaParsed = 0, sourceIdentities = int.Parse(catalog.Groups[1].Value), cookedEntries = int.Parse(cooked.Groups[1].Value), cookedSceneDocuments, textParserCalls = 0 });
    }
}
