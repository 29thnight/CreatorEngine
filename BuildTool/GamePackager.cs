using System.Text.RegularExpressions;

namespace CreatorBuildTool;

internal static class GamePackager
{
    public static async Task Build(BuildContext context)
    {
        var options = context.Options; var config = options.Choice("config", "Debug", "Debug", "Release");
        _ = options.Choice("target", "Game", "Game");
        var mode = options.Choice("input-mode", "Project", "Project", "Workspace", "Tracked"); var shipping = options.Flag("shipping");
        var frames = options.Number("smoke-frames", 120, 1, 1000000); var timeout = options.Number("smoke-timeout-sec", 180, 10, 3600);
        var repository = Paths.Repository(options.Get("repository"));
        var project = Paths.Canonical(options.Get("project", Path.Combine(repository, "Dynamic_CPP")), true);
        var stageDefault = File.Exists(Path.Combine(repository, "engine.manifest.json")) ? Path.Combine(project, "Build/Staging") : Path.Combine(repository, "Build/Staging");
        var stage = Paths.Canonical(options.Get("stage-root", stageDefault));
        var assets = Paths.Canonical(Path.Combine(project, "Assets"), true); var settings = Paths.Canonical(Path.Combine(project, "ProjectSetting"), true);
        Paths.Disjoint(stage, assets); Paths.Disjoint(stage, settings);
        var distributionRoot = options.Flag("build-native") ? await EnginePublisher.Publish(context, buildNative: true) :
            EngineDistribution.Resolve(repository, config + (shipping ? "-Shipping" : ""), options.Get("engine-distribution"));
        var engine = EngineDistribution.Load(distributionRoot, context);
        if (engine.Configuration != config || engine.Manifest.Bool("shipping") != shipping) throw new BuildException("Requested game configuration differs from selected engine.");
        engine.AssertProject(project); Paths.Disjoint(stage, engine.Root);
        var template = Paths.Child(engine.Root, "Tools/packaging/templates/EngineSettings.runtime.yml");
        var gitCommit = engine.Manifest["source"]!.Text("revision"); var dirty = engine.Manifest["source"]!.Bool("dirty");
        if (mode == "Tracked") gitCommit = (await context.Run("git", ["-C", repository, "rev-parse", "HEAD"], echo: false)).Output.Trim();
        var name = Regex.Replace(Path.GetFileName(project), "[<>:\"/\\\\|?*\\x00-\\x1F]", "_");
        if (name.Length == 0) throw new BuildException("Project name is empty.");
        Directory.CreateDirectory(stage);
        if (!Paths.Comparer.Equals(stage, Paths.Canonical(stage, true))) throw new BuildException("Stage root identity changed after creation.");
        using var buildLock = new FileStream(Paths.Child(stage, $".{name}.build.lock"), FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None);
        var id = Guid.NewGuid().ToString("N");
        // Repeating a long project name inside its own directory can exceed native
        // DLL loader limits. Keep the payload directory bounded; the pointer keeps
        // the user-facing project name.
        var candidate = Paths.Child(stage, $".{id}.candidate"); var release = Paths.Child(stage, $"Game-{id}");
        var pointer = Paths.Child(stage, $"{name}.current.json"); var generations = Paths.Child(stage, $".{id[..8]}.gen"); var verifyTemp = Paths.Child(stage, $".{id[..8]}.vt");
        Directory.CreateDirectory(candidate);
        context.Log($"Candidate: {candidate}", "stage");
        try
        {
            var cooker = Paths.Child(engine.BinaryRoot, "Tools/AssetCooker/AssetCooker.exe"); var packer = Paths.Child(engine.BinaryRoot, "Tools/AssetPacker/AssetPacker.exe");
            context.Log($"[1/6 Engine] {engine.Manifest.Text("version")} / {engine.Manifest.Text("buildId")}", "stage");
            var work = Paths.Child(candidate, ".package-input"); var managed = Path.Combine(work, "Managed");
            context.Log("[2/6 BuildManaged]", "stage");
            await GameCompiler.Compile(context, engine, project, managed, config, options.Get("game-scripts-assembly"));
            context.Log("[3/6 Cook]", "stage");
            var baseRoot = Path.Combine(work, "Base"); var generated = Path.Combine(work, "Generated"); var merged = Path.Combine(work, "Merged");
            foreach (var directory in new[] { baseRoot, generated, merged }) Directory.CreateDirectory(directory);
            var packageRevision = "WORKTREE"; int baseCount;
            if (mode == "Tracked")
            {
                var snapshot = await PackageInputs.Snapshot(context, repository, project, Path.Combine(work, "TrackedSnapshot"), gitCommit);
                template = Path.Combine(snapshot, "Tools/packaging/templates/EngineSettings.runtime.yml"); packageRevision = gitCommit;
                baseCount = PackageInputs.CopyProject(Path.Combine(snapshot, "Dynamic_CPP"), baseRoot, context.Cancellation);
            }
            else if (mode == "Workspace") baseCount = await PackageInputs.CopyWorkspace(context, repository, project, baseRoot);
            else baseCount = PackageInputs.CopyProject(project, baseRoot, context.Cancellation);
            var packageAssets = Path.Combine(baseRoot, "Assets");
            if (Directory.Exists(Path.Combine(packageAssets, "Derived"))) throw new BuildException("Package source contains a stale/authored Derived tree.");
            var generation = await AssetCooking.Generations(context, cooker, packageAssets, generations, mode == "Tracked" ? "" : Path.Combine(project, "Library/ModelAssetGenerations"));
            var cook = await AssetCooking.Cook(context, cooker, packageAssets, Path.Combine(generated, "Assets"), generations);
            context.Log($"Cooked {cook.ModelCount} models, {cook.ArtifactCount} artifacts; legacy texture references={cook.LegacyTextureNameRefs}, legacy model caches={cook.LegacyModelCookCaches}");
            context.Log("[4/6 Stage]", "stage");
            var runtimeRecord = Metadata.Read(Path.Combine(engine.BinaryRoot, "Runtime/Manifests/Player.json"));
            var runtimeSources = Metadata.ParseEntries(runtimeRecord.Array("entries")); Metadata.Verify(engine.BinaryRoot, runtimeSources, context.Cancellation);
            var rootFiles = new List<string>();
            foreach (var source in runtimeSources)
            {
                var relative = source.Path.StartsWith("Player/", StringComparison.Ordinal) ? source.Path[7..] : source.Path;
                Paths.Copy(Paths.Child(engine.BinaryRoot, source.Path), Paths.Child(candidate, relative)); rootFiles.Add(relative);
            }
            foreach (var source in Paths.Files(Path.Combine(engine.BinaryRoot, "Runtime/DotNet")))
            {
                context.Cancellation.ThrowIfCancellationRequested();
                var relative = Paths.Relative(engine.BinaryRoot, source); Paths.Copy(source, Paths.Child(candidate, relative)); rootFiles.Add(relative);
            }
            var runtimeMetadata = Metadata.Object(new { schemaVersion = 1, version = engine.Manifest.Text("version"), buildId = engine.Manifest.Text("buildId"),
                productName = engine.Manifest.Text("productName"), featureRelease = engine.Manifest.Text("featureRelease"), channel = engine.Manifest.Text("channel"),
                localDevelopment = engine.Manifest.Bool("localDevelopment"), payloadDigest = engine.Manifest.Text("payloadDigest"), hostAbi = engine.Manifest.Int("hostAbi"), scriptApi = engine.Manifest.Int("scriptApi"), configuration = config });
            Metadata.Write(Path.Combine(candidate, "engine.runtime.json"), runtimeMetadata); rootFiles.Add("engine.runtime.json");
            File.WriteAllText(Path.Combine(candidate, "engine.runtime.info"), Metadata.Info(engine.Manifest)); rootFiles.Add("engine.runtime.info");
            foreach (var relative in GameCompiler.ManagedFiles) Paths.Copy(Paths.Child(managed, relative), Paths.Child(candidate, "Managed/" + relative));
            var runtimePaths = rootFiles.Concat(Paths.Files(Path.Combine(candidate, "Managed")).Select(p => Paths.Relative(candidate, p))).ToArray();
            var runtimeEntries = Metadata.Entries(candidate, runtimePaths); var runtimeDigest = Metadata.Digest(runtimeEntries);
            context.Log("[5/6 Pak]", "stage");
            PackageInputs.Materialize(template, Path.Combine(generated, "ProjectSetting/EngineSettings.asset"), options.Get("startup-scene"), options.Get("render-backend"));
            Paths.CopyTree(baseRoot, merged, context.Cancellation); Paths.CopyTree(generated, merged, context.Cancellation);
            var mergedCook = AssetCooking.Validate(Path.Combine(merged, "Assets"), cook.ArtifactCount);
            if (mergedCook.ManifestSha256 != cook.ManifestSha256 || mergedCook.ArtifactBytes != cook.ArtifactBytes) throw new BuildException("Merged cook output changed.");
            var settingsFile = Path.Combine(merged, "ProjectSetting/EngineSettings.asset"); var settingsHash = Metadata.Hash(settingsFile);
            var preflight = PackageInputs.Validate(merged, settingsFile);
            var documents = await AssetCooking.Documents(context, cooker, merged);
            var entries = Metadata.Entries(merged, Paths.Files(merged).Where(p => !PackageInputs.Excluded(p)).Select(p => Paths.Relative(merged, p)));
            if (entries.Length == 0) throw new BuildException("Package content is empty.");
            var contentDigest = Metadata.Digest(entries); var distributionDigest = Metadata.Digest(runtimeEntries.Append(new("GameAssets.logical", 0, contentDigest)));
            var manifest = Metadata.Object(new { schemaVersion = 2, workspaceHead = gitCommit, workspaceDirty = dirty, packageInputRevision = packageRevision,
                nativeSource = "ENGINE_DISTRIBUTION", engineVersion = engine.Manifest.Text("version"), engineBuildId = engine.Manifest.Text("buildId"), nativeBuildRequested = options.Flag("build-native"),
                config, inputMode = mode, baseFileCount = baseCount, generatedFileCount = 1 + cook.DerivedFileCount, entryCount = entries.Length, contentDigest,
                settingsTemplateSha256 = Metadata.Hash(template), runtimeSettingsSha256 = Metadata.Hash(settingsFile), authoringRuntimeSettingsSha256 = settingsHash,
                startupScene = preflight.StartupScene, renderBackend = preflight.RuntimeBackend, startupSceneSha256 = Metadata.Hash(Path.Combine(merged, "Assets/Scenes/" + preflight.StartupScene)),
                startupSceneScriptComponentCount = preflight.SceneCounts["Script"], managedLifecycleRequired = preflight.RequiresManagedLifecycle,
                distributionPolicy = config == "Release" ? "bundled-runtime" : "development-only",
                runtimePrerequisites = new[] { new { name = ".NET 10 x64 runtime", bundled = true }, new { name = config == "Release" ? "Microsoft Visual C++ Redistributable x64" : "Microsoft Visual C++ Debug Runtime x64", bundled = true } },
                runtimeEntryCount = runtimeEntries.Length, runtimeDigest, distributionDigest, runtimeEntries,
                cook = new { schemaVersion = 1, producer = "AssetCooker", source = "package-base/Assets", artifactRoot = "Assets/Derived", cook.ModelCount, cook.ArtifactCount, cook.ArtifactBytes,
                    cook.ManifestBytes, cook.ManifestSha256, cook.DerivedFileCount, byFolder = cook.ByFolder, cook.SourceCounts, cook.LegacyTextureNameRefs, cook.LegacyModelCookCaches,
                    modelGenerationsCopied = generation.Copied, modelGenerationsAuthored = generation.Authored,
                    runtimeDocumentCount = documents.DocumentCount, runtimeDocumentBytes = documents.DocumentBytes, runtimeDocumentFormat = documents.Format },
                verification = "pending", entries });
            var manifestPath = Path.Combine(candidate, "package-manifest.json"); Metadata.Write(manifestPath, manifest);
            var pak = Path.Combine(candidate, "GameAssets.pak");
            var pack = await context.Run(packer, ["--assets", Path.Combine(merged, "Assets"), "--settings", Path.Combine(merged, "ProjectSetting"), "--output", pak, "--list-entries"], echo: false);
            ValidatePack(pack.Output, entries);
            if (!File.Exists(pak) || new FileInfo(pak).Length == 0) throw new BuildException("AssetPacker did not create a nonempty package.");
            manifest["pakFileSha256"] = Metadata.Hash(pak); Metadata.Write(manifestPath, manifest);
            context.Log("[6/6 Verify]", "stage");
            if (options.Flag("skip-verify")) manifest["verification"] = "skipped";
            else
            {
                manifest["smoke"] = await PlayerVerification.Verify(context, candidate, verifyTemp, stage, preflight, entries, shipping, frames, timeout);
                manifest["verification"] = "passed";
            }
            Metadata.Write(manifestPath, manifest);
            Paths.DeleteTree(work, candidate); Paths.DeleteTree(generations, stage);
            Paths.DeleteTree(Path.Combine(candidate, "Log"), candidate);
            foreach (var artifact in new[] { "imgui.ini", "verify.stdout.log", "verify.stderr.log" }) { var file = Paths.Child(candidate, artifact); if (File.Exists(file)) File.Delete(file); }
            if (Metadata.Digest(Metadata.Entries(candidate, runtimePaths)) != runtimeDigest || Metadata.Hash(pak) != manifest.Text("pakFileSha256")) throw new BuildException("Verification modified the staged payload.");
            ValidateClosure(candidate, runtimePaths.Concat(["GameAssets.pak", "package-manifest.json"]));
            if (options.Flag("skip-verify")) { context.Log("Unverified candidate retained; current package unchanged."); context.Result(candidate); return; }
            var publish = Metadata.Object(new { schemaVersion = 2, releaseDirectory = Path.GetFileName(release), buildId = id, workspaceHead = gitCommit,
                workspaceDirty = dirty, packageInputRevision = packageRevision, config, contentDigest, runtimeDigest, distributionDigest,
                pakFileSha256 = manifest.Text("pakFileSha256"), verification = "passed" });
            await Publish(context, candidate, release, pointer, stage, publish);
            context.Log($"[BUILD] current pointer: {pointer}"); context.Result(release);
        }
        catch { context.Error($"Build failed; current package preserved. Candidate: {candidate}; Player logs: {verifyTemp}"); throw; }
        finally { Paths.DeleteTree(generations, stage); }
    }
    public static void ValidatePack(string output, FileEntry[] entries)
    {
        var actual = output.Split('\n').Select(p => p.TrimEnd('\r')).Where(p => p.StartsWith("[PAK-ENTRY] ")).Select(p => p[12..]).ToArray();
        var unique = actual.ToHashSet(StringComparer.Ordinal);
        if (actual.Length != entries.Length || unique.Count != actual.Length || !unique.SetEquals(entries.Select(e => e.Path))) throw new BuildException("Reopened PAK entry list differs from the content manifest.");
    }
    public static void ValidateClosure(string stage, IEnumerable<string> expected)
    {
        var paths = expected.ToArray(); var actual = Paths.Files(stage).Select(p => Paths.Relative(stage, p)).ToArray();
        if (paths.Length != paths.Distinct(Paths.Comparer).Count() || !paths.ToHashSet(Paths.Comparer).SetEquals(actual)) throw new BuildException("Unexpected or missing file in distribution stage.");
        foreach (var directory in Directory.EnumerateDirectories(stage, "*", SearchOption.AllDirectories))
        {
            var prefix = Paths.Relative(stage, directory) + "/";
            if (!paths.Any(p => p.StartsWith(prefix, StringComparison.OrdinalIgnoreCase))) throw new BuildException($"Unexpected stage directory: {directory}");
        }
    }
    public static async Task Publish(BuildContext context, string candidate, string release, string pointer, string owner, object metadata)
    {
        Paths.AssertChild(candidate, owner); Paths.AssertChild(release, owner); Paths.AssertChild(pointer, owner);
        if (Directory.Exists(release)) throw new BuildException("Immutable package release already exists.");
        for (var attempt = 1; ; ++attempt)
        {
            context.Cancellation.ThrowIfCancellationRequested();
            try { Directory.Move(candidate, release); break; }
            catch (IOException) when (attempt < 20) { await Task.Delay(Math.Min(1000, attempt * 100), context.Cancellation); }
        }
        context.Cancellation.ThrowIfCancellationRequested(); Metadata.Write(pointer, metadata);
    }
}
