using System.Text.Json;
using System.Text.Json.Nodes;

namespace CreatorBuildTool
{
    internal sealed record BootstrapResult(JsonObject Report, Preflight Preflight, FileEntry[] Entries);

    internal static class RuntimeBootstrap
    {
        internal static string ContentMode(Options options)
        {
            var mode = options.Choice("content-mode", "Legacy", "Legacy", "PrebuiltAssetSets");
            if (mode == "PrebuiltAssetSets")
            {
                _ = options.Required("bootstrap-root");
                _ = options.Required("asset-set-list");
                _ = options.Required("game-scripts-assembly");
                if (options.Choice("input-mode", "Project", "Project", "Workspace", "Tracked") != "Project"
                    || options.Get("asset-list").Length != 0 || options.Get("startup-scene").Length != 0
                    || options.Get("render-backend").Length != 0 || options.Flag("build-native"))
                {
                    throw new BuildException("PrebuiltAssetSets uses its frozen bootstrap settings and prebuilt Player/scripts; input-mode, asset-list, startup/backend overrides and native builds are not supported.");
                }
            }
            else if (options.Get("bootstrap-root").Length != 0)
            {
                throw new BuildException("--bootstrap-root requires --content-mode PrebuiltAssetSets.");
            }
            return mode;
        }

        public static async Task Build(BuildContext context)
        {
            var options = context.Options;
            var engine = EngineDistribution.Load(options.Required("engine-distribution"), context);
            BlueNoiseResource.Validate(Paths.Child(engine.BinaryRoot, BlueNoiseResource.RelativePath));
            var abi = engine.RequireContentAbi(context);
            var project = Paths.Canonical(options.Required("project"), true);
            engine.AssertProject(project);
            var output = Paths.Canonical(options.Required("output"));
            if (Directory.Exists(output) || File.Exists(output)) throw new BuildException("Bootstrap output must be a new immutable directory.");
            foreach (var input in new[] { engine.Root, Path.Combine(project, "Assets"), Path.Combine(project, "ProjectSetting") })
                Paths.Disjoint(output, Paths.Canonical(input, true));
            _ = options.Required("asset-set-list");
            var selected = PackageInputs.ReadAssetList(project, options.Get("asset-list"), options.Get("startup-scene"));
            var parent = Path.GetDirectoryName(output)!;
            Directory.CreateDirectory(parent);
            var candidate = Paths.Child(parent, ".bootstrap-" + Guid.NewGuid().ToString("N") + ".candidate");
            var work = Paths.Child(candidate, ".inputs");
            var source = Paths.Child(work, "Source");
            var generated = Paths.Child(work, "Generated/Assets");
            var activated = Paths.Child(work, "Sets/Assets");
            Directory.CreateDirectory(candidate);
            try
            {
                var renderFeatures = RenderFeatureSettings.FromProject(Path.Combine(project, "ProjectSetting/EngineSettings.asset"));
                var copied = PackageInputs.CopyBootstrapProject(project, source, context.Cancellation, selected);
                var sourceEntries = Metadata.Entries(source);
                var cooker = Paths.Child(engine.BinaryRoot, "Tools/AssetCooker/AssetCooker.exe");
                var sets = await AssetSetPackaging.CopyConfiguredSets(context, activated,
                    async (from, to) => { await context.Run(cooker, ["--copy-asset-set", "--asset-root", from, "--output", to], engine.Root); }, abi);
                await context.Run(cooker, ["--validate-asset-set-activation", "--asset-root", activated], engine.Root);
                Directory.CreateDirectory(Path.GetDirectoryName(generated)!);
                var textureIdentityRoot = Path.Combine(project, "Assets");
                var textureDirectory = Path.Combine(textureIdentityRoot, "Textures");
                string[] TextureSidecars() => Directory.Exists(textureDirectory)
                    ? Paths.Files(textureDirectory).Where(path => path.EndsWith(".meta", StringComparison.OrdinalIgnoreCase))
                        .Select(path => Paths.Relative(textureIdentityRoot, path)).ToArray() : [];
                var textureIdentityEntries = Metadata.Entries(textureIdentityRoot, TextureSidecars());
                var cook = await AssetCooking.Cook(context, cooker, Path.Combine(source, "Assets"), generated, "", activated, textureIdentityRoot);
                Metadata.Verify(textureIdentityRoot, textureIdentityEntries, context.Cancellation);
                if (Metadata.Digest(Metadata.Entries(textureIdentityRoot, TextureSidecars())) != Metadata.Digest(textureIdentityEntries))
                {
                    throw new BuildException("Texture identity sidecars changed during bootstrap compilation.");
                }
                foreach (var mount in new[] { "Assets", "ProjectSetting" })
                    Paths.CopyTree(Path.Combine(source, mount), Path.Combine(candidate, mount), context.Cancellation);
                Paths.CopyTree(generated, Path.Combine(candidate, "Assets"), context.Cancellation);
                var template = Paths.Child(engine.Root, "Tools/packaging/templates/EngineSettings.runtime.yml");
                var settings = Paths.Child(candidate, "ProjectSetting/EngineSettings.asset");
                PackageInputs.Materialize(template, settings, options.Get("startup-scene"), options.Get("render-backend"), renderFeatures);
                var preflight = PackageInputs.Validate(candidate, settings);
                PackageInputs.RemoveGeometrySources(candidate);
                PackageInputs.RemoveAudioSources(candidate);
                var documents = await AssetCooking.Documents(context, cooker, candidate, Path.Combine(project, "Assets/Terrain"));
                // Sidecars and editor pins were inputs, never runtime documents.
                foreach (var mount in new[] { "Assets", "ProjectSetting" })
                    foreach (var file in Paths.Files(Path.Combine(candidate, mount)).Where(PackageInputs.Excluded).ToArray()) File.Delete(file);
                Metadata.Verify(source, sourceEntries, context.Cancellation);
                var entries = Metadata.Entries(candidate, new[] { "Assets", "ProjectSetting" }
                    .SelectMany(mount => Paths.Files(Path.Combine(candidate, mount))).Select(path => Paths.Relative(candidate, path)));
                ValidateBoundary(entries);
                // The native validator sees the full set group during this check;
                // sets remain independent and are not duplicated in the bootstrap.
                Paths.CopyTree(Path.Combine(activated, "AssetSets"), Path.Combine(candidate, "Assets/AssetSets"), context.Cancellation);
                Paths.Copy(Path.Combine(activated, "Derived/asset-set-activation.ceas"), Path.Combine(candidate, "Assets/Derived/asset-set-activation.ceas"));
                await context.Run(cooker, ["--validate-asset-set-activation", "--asset-root", Path.Combine(candidate, "Assets")], engine.Root);
                Paths.DeleteTree(Path.Combine(candidate, "Assets/AssetSets"), candidate);
                File.Delete(Path.Combine(candidate, "Assets/Derived/asset-set-activation.ceas"));
                var report = Metadata.Object(new { schemaVersion = 1, format = "CEBOOT1", contentAbi = abi,
                    contentAbiVersion = engine.Manifest.Int("contentAbiVersion"), engineBuildId = engine.Manifest.Text("buildId"),
                    enginePayloadDigest = engine.Manifest.Text("payloadDigest"), assetSetManifests = sets,
                    sourceInputDigest = Metadata.Digest(sourceEntries), sourceEntries, baseFileCount = copied,
                    textureIdentityInputDigest = Metadata.Digest(textureIdentityEntries), textureIdentityEntries,
                    contentDigest = Metadata.Digest(entries), entries, preflight, documents,
                    settingsTemplateSha256 = Metadata.Hash(template), runtimeSettingsSha256 = Metadata.Hash(settings),
                    startupSceneSha256 = Metadata.Hash(Path.Combine(candidate, "Assets/Scenes/" + preflight.StartupScene)),
                    producer = "RuntimeBootstrapDocumentCompiler", cook });
                Metadata.Write(Path.Combine(candidate, "bootstrap-report.json"), report);
                Paths.DeleteTree(work, candidate);
                _ = Validate(candidate, engine, context);
                context.Cancellation.ThrowIfCancellationRequested();
                Directory.Move(candidate, output);
                context.Result(output);
            }
            catch
            {
                context.Error("Bootstrap failed; immutable output unchanged. Candidate: " + candidate);
                throw;
            }
        }

        internal static void ValidateBoundary(IEnumerable<FileEntry> entries)
        {
            foreach (var entry in entries)
            {
                var relative = entry.Path;
                var extension = Path.GetExtension(relative).ToLowerInvariant();
                var allowed = relative is "Assets/Derived/asset-manifest.cemf" or "Assets/Derived/bootstrap-asset-references.cebr"
                    || relative.StartsWith("ProjectSetting/", StringComparison.Ordinal) && extension is ".asset" or ".celayers"
                    || relative.StartsWith("Assets/Shaders/", StringComparison.Ordinal) && extension is ".slang" or ".hlsl" or ".hlsli"
                    || relative.StartsWith("Assets/", StringComparison.Ordinal) && !relative.StartsWith("Assets/Derived/", StringComparison.Ordinal)
                        && extension is ".creator" or ".prefab" or ".inputmap" or ".bt" or ".blackboard" or ".renderprofile" or ".terrain" or ".foliage"
                    || relative.StartsWith("Assets/Derived/", StringComparison.Ordinal)
                        && System.Text.RegularExpressions.Regex.IsMatch(relative,
                            @"^Assets/Derived/(Scenes|Prefabs|Audio|SoundGraphs|SoundPresets|CollisionGeometry)/[0-9a-f]{2}/[0-9a-f-]{36}\.(creator|prefab|ceac|cesg|cesp|cepg)$");
                if (!allowed || PackageInputs.Excluded(relative)) throw new BuildException("Non-document source/payload in bootstrap: " + relative);
            }
        }

        internal static BootstrapResult Validate(string root, EngineDistribution engine, BuildContext context)
        {
            root = Paths.Canonical(root, true);
            var reportPath = Paths.Child(root, "bootstrap-report.json");
            if (!File.Exists(reportPath) || new FileInfo(reportPath).Length > 32 * 1024 * 1024)
                throw new BuildException("Bootstrap report missing or oversized.");
            var report = Metadata.Read(reportPath);
            if (report.Int("schemaVersion") != 1 || report.Text("format") != "CEBOOT1"
                || report.Text("contentAbi") != engine.RequireContentAbi(context)
                || report.Int("contentAbiVersion") != engine.Manifest.Int("contentAbiVersion")
                || report.Text("engineBuildId") != engine.Manifest.Text("buildId")
                || report.Text("enginePayloadDigest") != engine.Manifest.Text("payloadDigest"))
                throw new BuildException("Bootstrap producer/installed engine compatibility differs; rebuild the bootstrap.");
            var entries = Metadata.ParseEntries(report.Array("entries"));
            if (entries.Length == 0 || entries.Length > 100000 || Metadata.Digest(entries) != report.Text("contentDigest"))
                throw new BuildException("Invalid bootstrap content inventory.");
            ValidateBoundary(entries);
            var expected = entries.Select(entry => entry.Path).Append("bootstrap-report.json").ToHashSet(Paths.Comparer);
            foreach (var path in Paths.Files(root))
                if (!expected.Contains(Paths.Relative(root, path))) throw new BuildException("Unlisted bootstrap file: " + path);
            Metadata.Verify(root, entries, context.Cancellation);
            foreach (var required in new[] { "Assets/Derived/asset-manifest.cemf", "Assets/Derived/bootstrap-asset-references.cebr", "ProjectSetting/EngineSettings.asset", ProjectLayerAsset.RelativePath })
                if (!entries.Any(entry => entry.Path == required)) throw new BuildException("Bootstrap required file missing: " + required);
            ProjectLayerAsset.Require(root);
            var preflight = report["preflight"]!.Deserialize<Preflight>(Metadata.Format) ?? throw new BuildException("Bootstrap preflight missing.");
            if (Path.GetFileName(preflight.StartupScene) != preflight.StartupScene || !preflight.StartupScene.EndsWith(".creator", StringComparison.OrdinalIgnoreCase)
                || preflight.RuntimeBackend is not ("dx12" or "vulkan")
                || Metadata.Hash(Paths.Child(root, "Assets/Scenes/" + preflight.StartupScene)) != report.Text("startupSceneSha256")
                || Metadata.Hash(Paths.Child(root, "ProjectSetting/EngineSettings.asset")) != report.Text("runtimeSettingsSha256"))
                throw new BuildException("Bootstrap startup/settings identity differs.");
            return new(report, preflight, entries);
        }

        internal static BootstrapResult Copy(string source, string destination, EngineDistribution engine, BuildContext context)
        {
            source = Paths.Canonical(source, true);
            Paths.Disjoint(source, destination);
            var result = Validate(source, engine, context);
            foreach (var entry in result.Entries)
            {
                context.Cancellation.ThrowIfCancellationRequested();
                Paths.Copy(Paths.Child(source, entry.Path), Paths.Child(destination, entry.Path));
            }
            Metadata.Verify(destination, result.Entries, context.Cancellation);
            // Recheck the source/report after copy so a changed producer receipt
            // cannot be combined with bytes captured from a different bootstrap.
            var after = Validate(source, engine, context);
            if (after.Report.ToJsonString() != result.Report.ToJsonString()) throw new BuildException("Bootstrap changed while copying.");
            return result;
        }
    }
}
