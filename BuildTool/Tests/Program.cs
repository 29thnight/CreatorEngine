using System.Diagnostics;
using System.Text.Json.Nodes;
using CreatorBuildTool;

if (args.FirstOrDefault() == "--child") { await Task.Delay(60000); return 0; }
if (args.FirstOrDefault() == "--parent")
{
    using var child = Process.Start(new ProcessStartInfo(Environment.ProcessPath!) { ArgumentList = { "--child" }, UseShellExecute = false, CreateNoWindow = true })!;
    File.WriteAllText(args[1], child.Id.ToString()); await Task.Delay(60000); return 0;
}

var root = Path.Combine(Path.GetTempPath(), "CreatorBuildToolTests-" + Guid.NewGuid().ToString("N"));
Directory.CreateDirectory(root);
var checks = 0;
var succeeded = false;
void Check(bool value, string message) { if (!value) throw new Exception(message); ++checks; }
void Reject(Action action, string message)
{
    var rejected = false; try { action(); } catch (Exception) { rejected = true; } Check(rejected, message);
}
async Task RejectAsync(Func<Task> action, string message)
{
    var rejected = false; try { await action(); } catch (Exception) { rejected = true; } Check(rejected, message);
}
using var context = new BuildContext(new Options(["help"]), CancellationToken.None);
try
{
    Reject(() => new Options(["compile-game", "--project", "x", "--project", "y"]), "Duplicate CLI option accepted");
    Reject(() => new Options(["package-game", "--skpi-verify"]), "Unknown CLI option accepted");
    foreach (var value in new[] { @"\\?\C:\test", @"\\server\share", @"C:\test\a:stream", @"C:\test\CON.txt", @"C:\test\trailing.", @"C:\test\trailing " })
        Reject(() => Paths.Normal(value), "Ambiguous path accepted: " + value);
    Reject(() => Paths.Child(root, "../outside"), "Traversal accepted");
    Reject(() => Paths.DeleteTree(root, root), "Owner root removal accepted");
    Reject(() => Paths.Disjoint(root, Path.Combine(root, "Assets")), "Overlapping trees accepted");
    var junctionTarget = Path.Combine(root, "outside-target"); var link = Path.Combine(root, "symbolic"); Directory.CreateDirectory(junctionTarget);
    Directory.CreateSymbolicLink(link, junctionTarget);
    Reject(() => Paths.Canonical(Path.Combine(link, "output")), "Symlink output accepted");
    Reject(() => Paths.Files(root).ToArray(), "Recursive traversal followed symlink"); Directory.Delete(link);

    var engineRoot = Path.Combine(root, "engine"); Directory.CreateDirectory(engineRoot);
    File.WriteAllText(Path.Combine(engineRoot, "payload.txt"), "original payload");
    var files = Metadata.Entries(engineRoot);
    var version = Metadata.Object(new { schemaVersion = 1, productName = "CreatorEngine 2", featureRelease = "", version = "0.0.0.0", channel = "preview", localDevelopment = true });
    Metadata.ValidateVersion(version);
    foreach (var invalid in new[] { "1.2.3", "1.2.65536.0", "01.2.3.4", "1.2.3.4-preview" })
    { var bad = version.DeepClone(); bad["version"] = invalid; Reject(() => Metadata.ValidateVersion(bad), "Invalid version accepted"); }
    var manifest = Metadata.Object(new { schemaVersion = 1, productName = "CreatorEngine 2", featureRelease = "", version = "0.0.0.0", channel = "preview", localDevelopment = true,
        buildId = Guid.NewGuid().ToString("D"), payloadDigest = Metadata.Digest(files), platform = "win-x64", configuration = "Debug", shipping = false,
        binaryRoot = "Bin/x64-Debug", scriptApi = 24, hostAbi = 1, files });
    Metadata.Write(Path.Combine(engineRoot, "engine.manifest.json"), manifest); File.WriteAllText(Path.Combine(engineRoot, "engine.info"), Metadata.Info(manifest));
    var engine = EngineDistribution.Load(engineRoot, context); Check(engine.Configuration == "Debug", "Valid engine rejected");
    File.WriteAllText(Path.Combine(engineRoot, "extra.dll"), "extra"); Reject(() => EngineDistribution.Load(engineRoot, context), "Unlisted DLL accepted"); File.Delete(Path.Combine(engineRoot, "extra.dll"));
    File.WriteAllText(Path.Combine(engineRoot, "payload.txt"), "tampered payload"); Reject(() => EngineDistribution.Load(engineRoot, context), "Modified payload accepted");
    File.WriteAllText(Path.Combine(engineRoot, "payload.txt"), "original payload");
    Reject(() => Metadata.Digest([files[0], files[0] with { Path = files[0].Path.ToUpperInvariant() }]), "Case-duplicate payload accepted");
    var project = Path.Combine(root, "game 한글 & spaces"); Directory.CreateDirectory(Path.Combine(project, "ProjectSetting")); Directory.CreateDirectory(Path.Combine(project, "Assets/Script"));
    File.WriteAllText(Path.Combine(project, "Assets/Script/Example.cs"), "public class Example {}");
    File.WriteAllText(Path.Combine(project, "Assets/Script/Example.cs.meta"), "guid: 11111111-1111-4111-8111-111111111111");
    File.WriteAllText(Path.Combine(project, "Assets/Shader.hlsl"), "shader");
    File.WriteAllText(Path.Combine(project, "Assets/Shader.hlsl.meta"), "guid: 22222222-2222-4222-8222-222222222222");
    var cookInput = Path.Combine(root, "cook-input"); PackageInputs.CopyProject(project, cookInput, CancellationToken.None);
    Check(!Directory.Exists(Path.Combine(cookInput, "Assets/Script")), "C# source identities leaked into native cook input");
    Check(File.Exists(Path.Combine(cookInput, "Assets/Shader.hlsl.meta")) && File.Exists(Path.Combine(cookInput, "Assets/Shader.hlsl")), "Runtime source identity was removed from cook input");
    Reject(() => engine.AssertProject(project), "Missing project pin accepted"); engine.SelectProject(project); engine.AssertProject(project); ++checks;
    var wrong = manifest.DeepClone(); wrong["buildId"] = Guid.NewGuid().ToString("D"); Reject(() => new EngineDistribution(engineRoot, wrong.AsObject()).AssertProject(project), "Wrong project pin accepted");
    GamePackager.ValidatePack("[PAK-ENTRY] payload.txt\n", files); ++checks;
    Reject(() => GamePackager.ValidatePack("[PAK-ENTRY] wrong.txt\n", files), "Mismatched PAK listing accepted");
    Reject(() => GamePackager.ValidatePack("[PAK-ENTRY] payload.txt\n[PAK-ENTRY] payload.txt\n", files), "Duplicate PAK entry accepted");

    var shaderClosure = Path.Combine(root, "shader-closure");
    Directory.CreateDirectory(Path.Combine(shaderClosure, "Assets/Scenes"));
    Directory.CreateDirectory(Path.Combine(shaderClosure, "Assets/Shaders/DefaultPassShader"));
    File.WriteAllText(Path.Combine(shaderClosure, "Assets/Scenes/Demo.creator"), "m_Entities: []\n");
    var closureSettings = Path.Combine(shaderClosure, "runtime.asset");
    File.WriteAllText(closureSettings, "lastWindowSize:\n  x: 1280\n  y: 720\nrenderPassSettings:\nstartupSceneName: Demo.creator\nrender:\n  backend: dx12\nbuild:\n  render:\n    backend: dx12\n");
    var spriteShader = Path.Combine(shaderClosure, "Assets/Shaders/DefaultPassShader/WorldSprite.slang");
    File.WriteAllText(spriteShader, "// current renderer shader");
    Check(PackageInputs.Validate(shaderClosure, closureSettings).StartupScene == "Demo.creator", "Slang shader closure rejected");
    File.Move(spriteShader, Path.ChangeExtension(spriteShader, ".hlsl"));
    Reject(() => PackageInputs.Validate(shaderClosure, closureSettings), "Legacy-only shader closure accepted");

    var stage = Path.Combine(root, "stage"); Directory.CreateDirectory(stage); var pointer = Path.Combine(stage, "game.current.json");
    Metadata.Write(pointer, new { releaseDirectory = "old", verification = "passed" }); var pointerHash = Metadata.Hash(pointer);
    var candidate = Path.Combine(stage, "candidate"); var release = Path.Combine(stage, "release"); Directory.CreateDirectory(candidate); Directory.CreateDirectory(release);
    await RejectAsync(() => GamePackager.Publish(context, candidate, release, pointer, stage, new { releaseDirectory = "new" }), "Existing release overwritten");
    Check(Metadata.Hash(pointer) == pointerHash && Directory.Exists(candidate), "Failed publish altered current/candidate"); Directory.Delete(release);
    using var cancelled = new CancellationTokenSource(); cancelled.Cancel(); using var cancelContext = new BuildContext(new Options(["help"]), cancelled.Token);
    await RejectAsync(() => GamePackager.Publish(cancelContext, candidate, release, pointer, stage, new { releaseDirectory = "new" }), "Cancelled package published");
    Check(Metadata.Hash(pointer) == pointerHash, "Cancellation altered current pointer");
    await GamePackager.Publish(context, candidate, release, pointer, stage, new { releaseDirectory = "release", verification = "passed" });
    Check(Directory.Exists(release) && !Directory.Exists(candidate) && Metadata.Read(pointer).Text("releaseDirectory") == "release", "Atomic package publish failed");

    const string smoke = "[asset.catalog] source=cemf identities=1 metaParsed=0\n[cooked.catalog] mount test entries=2 sources=1 stale=0\n" +
        "[scene.document] source=cooked guid=11111111-1111-4111-8111-111111111111\n[player.service] compiled=yes enabled=no\n" +
        "[runtime.text-parser] calls=0\nScene loaded: Demo.creator\n[player.smoke] {\"schemaVersion\":1,\"ready\":true,\"registeredScriptTypes\":1}\n" +
        "[SMOKE] frame limit reached (120 GT frames, display frame 100, promotions 90)\n";
    var preflight = new Preflight("Demo.creator", "dx12", new(), false);
    PlayerVerification.ValidateMarkers(smoke, smoke, preflight, false, 120); ++checks;
    Reject(() => PlayerVerification.ValidateMarkers(smoke, smoke, preflight, true, 120), "Wrong Shipping binary accepted");
    Reject(() => PlayerVerification.ValidateMarkers(smoke.Replace("calls=0", "calls=1"), smoke, preflight, false, 120), "Text parser use accepted");
    Reject(() => PlayerVerification.ValidateMarkers(smoke, smoke + "[model.generation] 게시 전 검증 실패", preflight, false, 120), "Failed model generation accepted");
    Reject(() => PlayerVerification.ValidateMarkers(smoke.Replace("\"ready\":true", "\"ready\":false"), smoke, preflight, false, 120), "Unready CLR accepted");

    var pidFile = Path.Combine(root, "child.pid"); using var timeoutContext = new BuildContext(new Options(["help"]), CancellationToken.None);
    await RejectAsync(() => timeoutContext.Run(Environment.ProcessPath!, ["--parent", pidFile], timeoutSeconds: 2, echo: false), "Timed out process was accepted");
    Check(File.Exists(pidFile), "Timeout fixture did not start a child"); var childId = int.Parse(File.ReadAllText(pidFile));
    var alive = false; try { using var child = Process.GetProcessById(childId); alive = !child.HasExited; } catch (ArgumentException) { }
    Check(!alive, "Timeout left a child process running");

    if (args.Length == 2 && args[0] == "--engine")
    {
        var real = EngineDistribution.Load(args[1], context); real.SelectProject(project);
        var trace = Path.Combine(root, "private-runtime.trace");
        var installed = await context.Run(Paths.Child(real.Root, real.Manifest.Text("buildTool")),
            ["verify-engine", "--engine-distribution", real.Root, "--json"], project,
            new() { ["PATH"] = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Windows), "System32"),
                ["DOTNET_ROOT"] = Path.Combine(root, "NoDotnet"), ["DOTNET_MULTILEVEL_LOOKUP"] = "0",
                ["DOTNET_HOST_TRACE"] = "1", ["DOTNET_HOST_TRACEFILE"] = trace }, echo: false);
        Check(JsonNode.Parse(installed.Output)!.Text("type") == "result", "Installed tool did not produce a JSON result");
        var traceText = File.ReadAllText(trace);
        Check(traceText.Contains("Using app-relative location") && traceText.Contains(real.BinaryRoot) &&
            !traceText.Contains("Using global install location"), "Installed apphost used the system .NET instead of its bundled runtime");
        var source = Path.Combine(project, "Assets/Script/Example.cs"); File.WriteAllText(source, "public static class Example { public static int Value => 42; }");
        var output = Path.Combine(project, "Intermediate/Managed");
        await GameCompiler.Compile(context, real, project, output, real.Configuration, "");
        var assembly = Path.Combine(output, "Scripts/GameScripts.dll"); var hash = Metadata.Hash(assembly); var identityHash = Metadata.Hash(assembly + ".engine.json");
        Check(Metadata.Read(assembly + ".engine.json").Text("engineBuildId") == real.Manifest.Text("buildId"), "Compiler pin not recorded");
        File.WriteAllText(source, "this is invalid C#");
        await RejectAsync(() => GameCompiler.Compile(context, real, project, output, real.Configuration, ""), "Compiler failure accepted");
        Check(Metadata.Hash(assembly) == hash && Metadata.Hash(assembly + ".engine.json") == identityHash, "Failed compile replaced last working assembly");
        await GameCompiler.Compile(context, real, project, Path.Combine(project, "Intermediate/Prebuilt"), real.Configuration, assembly); ++checks;
        File.AppendAllText(assembly, "corrupt");
        await RejectAsync(() => GameCompiler.Compile(context, real, project, Path.Combine(project, "Intermediate/Rejected"), real.Configuration, assembly), "Tampered prebuilt assembly accepted");
    }
    succeeded = true; Console.WriteLine($"BUILD_TOOL_TESTS_OK checks={checks}"); return 0;
}
catch (Exception ex) { Console.Error.WriteLine(ex); Console.Error.WriteLine($"Test fixture retained: {root}"); return 1; }
finally
{
    if (succeeded) Paths.DeleteTree(root, Path.GetTempPath());
}
