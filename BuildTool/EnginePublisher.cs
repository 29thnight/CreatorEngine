using System.Diagnostics;
using System.Text.Json.Nodes;
using System.Text.RegularExpressions;

namespace CreatorBuildTool;

internal static class EnginePublisher
{
    public static async Task<string> Publish(BuildContext context, bool buildNative = false)
    {
        var options = context.Options;
        var repository = Paths.Repository(options.Get("repository"));
        var config = options.Choice("config", "Release", "Debug", "Release");
        var shipping = options.Flag("shipping");
        var output = Paths.Canonical(options.Get("output-root", Path.Combine(repository, "Build/Distributions")));
        var metadata = Metadata.Read(Path.Combine(repository, "EngineVersion.json")); Metadata.ValidateVersion(metadata);
        var version = metadata.Text("version");
        if (buildNative || options.Flag("build"))
        {
            var vswhere = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86), "Microsoft Visual Studio/Installer/vswhere.exe");
            var installation = (await context.Run(vswhere, ["-latest", "-products", "*", "-requires", "Microsoft.Component.MSBuild", "-property", "installationPath"], echo: false)).Output.Trim();
            var msbuild = Path.Combine(installation, "MSBuild/Current/Bin/amd64/MSBuild.exe");
            // Build individual native hosts; never overwrite the currently running build tool.
            foreach (var project in new[] { "Editor/CreatorEditor.vcxproj", "Player/Player.vcxproj", "Tools/AssetCooker/AssetCooker.vcxproj", "Tools/AssetPacker/AssetPacker.vcxproj" })
            {
                var arguments = new List<string> { Path.Combine(repository, project), "/m", "/t:Build", $"/p:Configuration={config}", "/p:Platform=x64", "/nologo", "/verbosity:minimal" };
                if (shipping && project.StartsWith("Player/")) arguments.Add("/p:EngineShipping=true");
                await context.Run(msbuild, arguments, repository, timeoutSeconds: 0);
            }
        }
        var binarySource = Path.Combine(repository, $"Bin/x64-{config}");
        Paths.Disjoint(output, Paths.Canonical(binarySource, true));
        Directory.CreateDirectory(output);
        var candidate = Paths.Child(output, ".candidate-" + Guid.NewGuid().ToString("N")); Directory.CreateDirectory(candidate);
        context.Log($"Engine candidate: {candidate}", "stage");
        var binaryTarget = Paths.Child(candidate, $"Bin/x64-{config}");
        void Copy(string source, string destination)
        {
            context.Cancellation.ThrowIfCancellationRequested(); Paths.AssertChild(destination, candidate);
            if (File.Exists(destination)) { if (Metadata.Hash(source) != Metadata.Hash(destination)) throw new BuildException($"Conflicting shared engine file: {destination}"); }
            else Paths.Copy(source, destination);
        }
        void Tree(string source, string destination)
        {
            Paths.Disjoint(Paths.Canonical(source, true), Paths.Canonical(destination));
            foreach (var file in Paths.Files(source)) Copy(file, Paths.Child(destination, Paths.Relative(source, file)));
        }
        try
        {
            var hosts = new JsonObject();
            foreach (var name in new[] { "CreatorEditor", "Player", "AssetCooker", "AssetPacker" })
            {
                var source = name == "Player" && shipping ? Path.Combine(repository, $"Bin/x64-{config}-Shipping") : binarySource;
                var recordPath = Path.Combine(source, $"Runtime/Manifests/{name}.json");
                var record = Metadata.Read(recordPath); var abi = record["abi"]!;
                var entries = Metadata.ParseEntries(record.Array("entries"));
                if (record.Int("schemaVersion") != 1 || record.Text("host") != name || record.Text("configuration") != config || Metadata.Digest(entries) != record.Text("digest"))
                    throw new BuildException($"Invalid native build record: {name}");
                Metadata.Verify(source, entries, context.Cancellation);
                if (abi.Text("version") != version || abi.Text("productName") != metadata.Text("productName") ||
                    abi.Text("featureRelease") != metadata.Text("featureRelease") || abi.Bool("localDevelopment") != metadata.Bool("localDevelopment") ||
                    (abi.Int("debug") != 0) != (config == "Debug") || (abi.Int("shipping") != 0) != (name == "Player" && shipping))
                    throw new BuildException($"Native version/configuration differs from the requested distribution: {name}. Rebuild the engine.");
                foreach (var entry in entries)
                {
                    var file = Paths.Child(source, entry.Path);
                    if (entry.Path.EndsWith(".exe") || entry.Path.EndsWith(".runtime.dll"))
                    {
                        var info = FileVersionInfo.GetVersionInfo(file);
                        if (info.FileVersion != version || info.ProductVersion != version) throw new BuildException($"Windows version mismatch: {entry.Path}");
                    }
                    Copy(file, Paths.Child(binaryTarget, entry.Path));
                }
                Copy(recordPath, Paths.Child(binaryTarget, $"Runtime/Manifests/{name}.json")); hosts[name] = abi.DeepClone();
            }
            var api = hosts["Player"]!.Int("scriptApi");
            var expected = Regex.Match(File.ReadAllText(Path.Combine(repository, "ScriptCore/Native.cs")), @"ExpectedVersion\s*=\s*(\d+)");
            if (!expected.Success || int.Parse(expected.Groups[1].Value) != api || hosts.Any(h => h.Value!.Int("scriptApi") != api))
                throw new BuildException("Native and managed script API versions differ.");
            foreach (var name in GameCompiler.CoreFiles) Copy(Path.Combine(binarySource, "Managed", name), Path.Combine(binaryTarget, "Managed", name));
            Tree(Path.Combine(binarySource, "Resources"), Path.Combine(binaryTarget, "Resources"));
            var dotnetRoot = Path.GetDirectoryName(System.Runtime.InteropServices.RuntimeEnvironment.GetRuntimeDirectory().TrimEnd(Path.DirectorySeparatorChar))!;
            dotnetRoot = Directory.GetParent(dotnetRoot)!.Parent!.FullName;
            string Latest(string relative) => Directory.GetDirectories(Path.Combine(dotnetRoot, relative))
                .Where(p => Regex.IsMatch(Path.GetFileName(p), @"^10\.\d+\.\d+$")).OrderByDescending(p => Version.Parse(Path.GetFileName(p))).FirstOrDefault()
                ?? throw new BuildException($"Publishing requires .NET 10 SDK/runtime: {relative}");
            var runtime = Latest("shared/Microsoft.NETCore.App"); var sdk = Latest("sdk"); var references = Latest("packs/Microsoft.NETCore.App.Ref");
            var privateDotnet = Path.Combine(binaryTarget, "Runtime/DotNet");
            Copy(Path.Combine(dotnetRoot, "dotnet.exe"), Path.Combine(privateDotnet, "dotnet.exe"));
            Tree(runtime, Path.Combine(privateDotnet, "shared/Microsoft.NETCore.App", Path.GetFileName(runtime)));
            Tree(Path.Combine(dotnetRoot, "host/fxr", Path.GetFileName(runtime)), Path.Combine(privateDotnet, "host/fxr", Path.GetFileName(runtime)));
            foreach (var name in new[] { "LICENSE.txt", "ThirdPartyNotices.txt" }) Copy(Path.Combine(dotnetRoot, name), Path.Combine(privateDotnet, name));
            Tree(Path.Combine(sdk, "Roslyn/bincore"), Path.Combine(candidate, "Scripting/Compiler"));
            Tree(Path.Combine(references, "ref/net10.0"), Path.Combine(candidate, "Scripting/References"));
            Copy(Path.Combine(repository, $"Build/Tools/Managed/ScriptCore.Generators/{config}/netstandard2.0/ScriptCore.Generators.dll"), Path.Combine(candidate, "Scripting/ScriptCore.Generators.dll"));
            var toolSource = Path.Combine(binarySource, "Tools/CreatorBuildTool");
            foreach (var name in new[] { "CreatorBuildTool.exe", "CreatorBuildTool.dll", "CreatorBuildTool.deps.json", "CreatorBuildTool.runtimeconfig.json" })
            {
                if (name.EndsWith(".exe") || name.EndsWith(".dll"))
                {
                    var componentVersion = FileVersionInfo.GetVersionInfo(Path.Combine(toolSource, name));
                    if (componentVersion.FileVersion != version || componentVersion.ProductVersion != version)
                        throw new BuildException($"Build tool version differs from the engine: {name}. Rebuild CreatorBuildTool.");
                }
                Copy(Path.Combine(toolSource, name), Path.Combine(binaryTarget, "Tools/CreatorBuildTool", name));
            }
            Tree(Path.Combine(repository, "Tools/packaging/templates"), Path.Combine(candidate, "Tools/packaging/templates"));
            foreach (var file in Paths.Files(Path.Combine(repository, "ThirdParty")).Where(p => Regex.IsMatch(Path.GetFileName(p), "^(LICENSE|COPYING|NOTICE|README)", RegexOptions.IgnoreCase) && Path.GetExtension(p) is not (".dll" or ".lib")))
                Copy(file, Paths.Child(candidate, "Licenses/ThirdParty/" + Paths.Relative(Path.Combine(repository, "ThirdParty"), file)));
            foreach (var file in Paths.Files(Path.Combine(repository, "vcpkg_installed")).Where(p => Path.GetFileName(p).Equals("copyright", StringComparison.OrdinalIgnoreCase)))
                Copy(file, Paths.Child(candidate, "Licenses/vcpkg/" + Paths.Relative(Path.Combine(repository, "vcpkg_installed"), file)));
            var entriesAll = Metadata.Entries(candidate); var digest = Metadata.Digest(entriesAll); var buildId = Guid.NewGuid().ToString("D");
            var revision = (await context.Run("git", ["-C", repository, "rev-parse", "HEAD"], echo: false)).Output.Trim();
            var dirty = (await context.Run("git", ["-C", repository, "status", "--porcelain"], echo: false)).Output.Length > 0;
            var manifest = (JsonObject)metadata.DeepClone();
            var payload = Metadata.Object(new { buildId, payloadDigest = digest, platform = "win-x64", configuration = config, shipping,
                binaryRoot = $"Bin/x64-{config}", scriptApi = api, hostAbi = 1, supportedScriptApis = new[] { api },
                source = new { revision, dirty, kind = "build-workspace-observation" },
                toolchain = new { dotnetRuntime = Path.GetFileName(runtime), compilerSdk = Path.GetFileName(sdk), referencePack = Path.GetFileName(references) },
                hosts, files = entriesAll, buildTool = $"Bin/x64-{config}/Tools/CreatorBuildTool/CreatorBuildTool.exe" });
            foreach (var pair in payload) manifest[pair.Key] = pair.Value?.DeepClone();
            Metadata.Write(Path.Combine(candidate, "engine.manifest.json"), manifest);
            File.WriteAllText(Path.Combine(candidate, "engine.info"), Metadata.Info(manifest));
            var namePart = $"{version}-win-x64-{config}" + (shipping ? "-Shipping" : "");
            if (metadata.Bool("localDevelopment")) namePart = $"local-{namePart}-{buildId}";
            var release = Paths.Child(output, namePart);
            if (Directory.Exists(release))
            {
                var old = EngineDistribution.Load(release, context).Manifest;
                if (old.Text("payloadDigest") != digest) throw new BuildException("Immutable engine version collision: assign a new build.");
                if (old.Text("channel") != metadata.Text("channel")) throw new BuildException("Publishing never relabels an existing channel.");
                buildId = old.Text("buildId"); Paths.DeleteTree(candidate, output);
            }
            else { context.Cancellation.ThrowIfCancellationRequested(); Paths.AssertChild(candidate, output); Paths.AssertChild(release, output); Directory.Move(candidate, release); }
            if (!options.Flag("no-pointer"))
            {
                var pointerRoot = shipping ? Path.Combine(repository, $"Bin/x64-{config}-Shipping") : binarySource;
                Metadata.Write(Path.Combine(pointerRoot, "engine.distribution.json"), new { version, buildId, path = release });
                File.WriteAllText(Path.Combine(pointerRoot, "engine.distribution.path"), release + "\n");
            }
            context.Log($"ENGINE_DISTRIBUTION_OK {release}"); context.Result(release); return release;
        }
        catch { context.Error($"Unpublished engine candidate retained: {candidate}"); throw; }
    }
}
