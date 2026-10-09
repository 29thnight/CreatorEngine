using System.Security.Cryptography;
using System.Text.Json.Nodes;
using System.Text.RegularExpressions;

namespace CreatorBuildTool;

internal static class GameCompiler
{
    public static readonly string[] CoreFiles = ["ScriptCore.dll", "ScriptCore.deps.json", "ScriptCore.runtimeconfig.json"];
    public static readonly string[] ManagedFiles = [.. CoreFiles, "Scripts/GameScripts.dll", "Scripts/GameScripts.deps.json", "Scripts/GameScripts.dll.engine.json"];
    // Keep both input identities open without write/delete sharing until the
    // exact copied bytes are checked. Never bless changed bytes with a new receipt.
    internal static string CopyPrebuiltAssembly(EngineDistribution engine, string prebuilt, string assembly)
    {
        Paths.NoReparseAncestors(prebuilt);
        Paths.NoReparseAncestors(prebuilt + ".engine.json");
        Paths.NoReparseAncestors(assembly);
        using var receipt = new FileStream(prebuilt + ".engine.json", FileMode.Open, FileAccess.Read, FileShare.Read);
        if (receipt.Length is <= 0 or > 16384) throw new BuildException("Prebuilt assembly receipt is empty or oversized.");
        var receiptBytes = new byte[checked((int)receipt.Length)];
        receipt.ReadExactly(receiptBytes);
        if (receipt.ReadByte() != -1) throw new BuildException("Prebuilt assembly receipt changed during capture.");
        ReadOnlySpan<byte> receiptJson = receiptBytes;
        if (receiptJson.StartsWith(new byte[] { 0xef, 0xbb, 0xbf })) receiptJson = receiptJson[3..];
        var identity = JsonNode.Parse(receiptJson)?.AsObject() ?? throw new BuildException("Invalid prebuilt assembly receipt.");
        var expected = identity.Text("sha256");
        if (identity.Int("schemaVersion") != 1 || identity.Text("engineBuildId") != engine.Manifest.Text("buildId")
            || identity.Int("scriptApi") != engine.Manifest.Int("scriptApi") || !Regex.IsMatch(expected, "^[0-9a-f]{64}$"))
            throw new BuildException("Prebuilt GameScripts.dll does not match the selected engine or its recorded bytes.");
        using var source = new FileStream(prebuilt, FileMode.Open, FileAccess.Read, FileShare.Read);
        if (Convert.ToHexStringLower(SHA256.HashData(source)) != expected)
            throw new BuildException("Prebuilt GameScripts.dll differs from its captured receipt.");
        source.Position = 0;
        using var output = new FileStream(assembly, FileMode.CreateNew, FileAccess.ReadWrite, FileShare.None);
        source.CopyTo(output);
        output.Flush(true);
        output.Position = 0;
        if (Convert.ToHexStringLower(SHA256.HashData(output)) != expected)
            throw new BuildException("Copied GameScripts.dll differs from its captured receipt.");
        receipt.Position = 0;
        var after = new byte[receiptBytes.Length];
        receipt.ReadExactly(after);
        if (receipt.ReadByte() != -1 || !after.AsSpan().SequenceEqual(receiptBytes))
            throw new BuildException("Prebuilt assembly receipt changed while copying.");
        return expected;
    }

    public static async Task Compile(BuildContext context, EngineDistribution engine, string project, string output, string configuration, string prebuilt)
    {
        project = Paths.Canonical(project, true); output = Paths.Canonical(output);
        if (engine.Configuration != configuration) throw new BuildException("Managed configuration differs from selected engine.");
        engine.AssertProject(project);
        Paths.Disjoint(output, engine.Root); Paths.Disjoint(output, Paths.Canonical(Path.Combine(project, "Assets"), true));
        Paths.Disjoint(output, Paths.Canonical(Path.Combine(project, "ProjectSetting"), true));
        Directory.CreateDirectory(output);
        using var compileLock = new FileStream(Paths.Child(output, ".compile.lock"), FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None);
        var work = Paths.Child(output, ".compiler-" + Guid.NewGuid().ToString("N")); Directory.CreateDirectory(Path.Combine(work, "Scripts"));
        var assembly = Path.Combine(work, "Scripts/GameScripts.dll");
        try
        {
            foreach (var name in CoreFiles) Paths.Copy(Path.Combine(engine.BinaryRoot, "Managed", name), Path.Combine(work, name));
            string? prebuiltHash = null;
            if (prebuilt.Length > 0)
            {
                prebuiltHash = CopyPrebuiltAssembly(engine, prebuilt, assembly);
            }
            else
            {
                var sourceRoot = Path.Combine(project, "Assets/Script");
                var sources = Paths.Files(sourceRoot).Where(p => Path.GetExtension(p).Equals(".cs", StringComparison.OrdinalIgnoreCase)).Order(StringComparer.Ordinal).ToArray();
                var globals = Path.Combine(work, "GlobalUsings.g.cs");
                File.WriteAllText(globals, "global using System; global using System.Collections.Generic; global using System.IO; global using System.Linq; global using System.Net.Http; global using System.Threading; global using System.Threading.Tasks;");
                static string Quote(string path) => '"' + path + '"';
                var arguments = new List<string> { "/nologo", "/target:library", "/langversion:latest", "/nullable:enable", "/unsafe+", "/nostdlib+", "/deterministic+",
                    "/out:" + Quote(assembly), "/reference:" + Quote(Path.Combine(work, "ScriptCore.dll")),
                    "/analyzer:" + Quote(Path.Combine(engine.Root, "Scripting/ScriptCore.Generators.dll")),
                    "/pathmap:" + Quote(project + "=/_/Project," + work + "=/_/Build") };
                arguments.AddRange(configuration == "Debug" ? ["/define:DEBUG;TRACE", "/debug:portable", "/optimize-"] : ["/define:TRACE", "/optimize+"]);
                arguments.AddRange(Paths.Files(Path.Combine(engine.Root, "Scripting/References")).Where(p => p.EndsWith(".dll", StringComparison.OrdinalIgnoreCase)).Order(StringComparer.Ordinal).Select(p => "/reference:" + Quote(p)));
                arguments.Add(Quote(globals)); arguments.AddRange(sources.Select(Quote));
                var response = Path.Combine(work, "compile.rsp"); File.WriteAllLines(response, arguments);
                await context.Run(Path.Combine(engine.BinaryRoot, "Runtime/DotNet/dotnet.exe"), ["exec", Path.Combine(engine.Root, "Scripting/Compiler/csc.dll"), "@" + response]);
            }
            Metadata.Write(assembly + ".engine.json", new { schemaVersion = 1, engineBuildId = engine.Manifest.Text("buildId"), scriptApi = engine.Manifest.Int("scriptApi"), sha256 = prebuiltHash ?? Metadata.Hash(assembly) });
            Metadata.Write(Path.Combine(work, "Scripts/GameScripts.deps.json"), new
            {
                runtimeTarget = new { name = ".NETCoreApp,Version=v10.0", signature = "" }, compilationOptions = new { },
                targets = new Dictionary<string, object> { [".NETCoreApp,Version=v10.0"] = new Dictionary<string, object> { ["GameScripts/1.0.0"] = new { runtime = new Dictionary<string, object> { ["GameScripts.dll"] = new { } } } } },
                libraries = new Dictionary<string, object> { ["GameScripts/1.0.0"] = new { type = "project", serviceable = false, sha512 = "" } }
            });
            context.Cancellation.ThrowIfCancellationRequested();
            if (prebuiltHash != null && Metadata.Hash(assembly) != prebuiltHash)
                throw new BuildException("Prebuilt GameScripts.dll changed before publication.");
            foreach (var relative in ManagedFiles.Concat(File.Exists(Path.Combine(work, "Scripts/GameScripts.pdb")) ? ["Scripts/GameScripts.pdb"] : Array.Empty<string>()))
            {
                var source = Paths.Child(work, relative); var destination = Paths.Child(output, relative);
                if (File.Exists(destination) && Metadata.Hash(destination) == Metadata.Hash(source)) continue;
                Directory.CreateDirectory(Path.GetDirectoryName(destination)!); File.Move(source, destination, true);
            }
            context.Log($"GameScripts compiled for engine {engine.Manifest.Text("buildId")}");
        }
        finally { Paths.DeleteTree(work, output); }
    }
}
