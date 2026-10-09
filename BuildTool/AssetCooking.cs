using System.Text.RegularExpressions;

namespace CreatorBuildTool;

internal sealed record GenerationResult(int Models, int Copied, int Authored);
internal sealed record CookResult(int ArtifactCount, int CompanionCount, long ArtifactBytes, long ManifestBytes, string ManifestSha256,
    int DerivedFileCount, Dictionary<string, int> ByFolder)
{
    public int ModelCount { get; set; }
    public int LegacyTextureNameRefs { get; set; }
    public int LegacyModelCookCaches { get; set; }
    public Dictionary<string, int> SourceCounts { get; set; } = new();
}
internal sealed record DocumentResult(int DocumentCount, long DocumentBytes, string Format);
internal static class AssetCooking
{
    private static string[] Models(string assets)
    {
        var models = Paths.Files(assets).Where(p => Path.GetExtension(p).ToLowerInvariant() is ".fbx" or ".glb" or ".gltf").Order(StringComparer.Ordinal).ToArray();
        return models;
    }
    public static async Task<GenerationResult> Generations(BuildContext context, string cooker, string assets, string output, string library)
    {
        var models = Models(assets); var copied = 0; var authored = 0;
        if (Directory.Exists(output)) throw new BuildException($"Generation output already exists: {output}");
        Directory.CreateDirectory(output);
        foreach (var model in models)
        {
            context.Cancellation.ThrowIfCancellationRequested();
            var sidecar = File.ReadAllText(model + ".meta");
            var modelId = Regex.Match(sidecar, @"(?m)^assetId:\s*([0-9a-f-]{36})\s*$");
            var materials = Regex.Matches(sidecar, @"(?m)^\s*- kind: material\r?\n\s*stableKey:[^\r\n]*\r?\n\s*assetId:\s*([0-9a-f-]{36})\s*$");
            var hasGraphs = modelId.Success && materials.All(material =>
            {
                var graph = Path.Combine(assets, "Materials", "Models", modelId.Groups[1].Value, material.Groups[1].Value + ".shadergraph");
                return File.Exists(graph) && File.Exists(graph + ".meta");
            });
            if (library.Length == 0 || !hasGraphs)
            {
                await context.Run(cooker, ["--author-model-asset", "--asset-root", assets, "--output", output, "--model", model]); ++authored;
            }
            else
            {
                var id = Regex.Match(sidecar, @"(?m)^assetId:\s*([0-9a-f-]{36})\s*$");
                var generation = Regex.Match(sidecar, @"(?m)^generation:\s*(\d+)\s*$");
                if (!id.Success || !Guid.TryParse(id.Groups[1].Value, out _) || !generation.Success) throw new BuildException($"Invalid model sidecar: {model}.meta");
                var relative = id.Groups[1].Value + "/" + generation.Groups[1].Value;
                Paths.CopyTree(Paths.Child(library, relative), Paths.Child(output, relative), context.Cancellation); ++copied;
            }
        }
        return new(models.Length, copied, authored);
    }
    public static CookResult Validate(string output, int expected, bool bootstrap = false)
    {
        const string guid = "([0-9a-f]{8}-[0-9a-f]{4}-[48][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12})";
        var rules = new Dictionary<string, string> { ["Models"] = "", ["Textures"] = "cetex", ["ShaderMeta"] = "shadermeta", ["Materials"] = "asset", ["MaterialPrograms"] = "lxmaterial", ["Scenes"] = "creator", ["Prefabs"] = "prefab", ["CollisionGeometry"] = "cepg", ["Audio"] = "ceac", ["SoundGraphs"] = "cesg", ["SoundPresets"] = "cesp" };
        var derived = Path.Combine(output, "Derived"); var manifest = Path.Combine(derived, "asset-manifest.cemf");
        if (!File.Exists(manifest)) throw new BuildException("Cooked asset manifest missing.");
        var files = Paths.Files(derived).ToArray(); var byFolder = rules.Keys.ToDictionary(k => k, _ => 0);
        var artifacts = 0; var companions = 0; var bytes = 0L;
        foreach (var file in files.Where(f => !Paths.Comparer.Equals(f, manifest)))
        {
            if (bootstrap && Paths.Relative(derived, file) == "bootstrap-asset-references.cebr") continue;
            bytes += new FileInfo(file).Length;
            var relative = Paths.Relative(derived, file); var folder = relative.Split('/')[0];
            if (!rules.TryGetValue(folder, out var extensions)) throw new BuildException($"Unexpected Derived folder: {relative}");
            var pattern = folder == "Models" ? "^Models/([0-9a-f]{2})/" + guid + "/[0-9]+/(generation\\.asset|model\\.cemc|sidecar\\.meta|textures/" + guid + "\\.cetex)$"
                : "^" + folder + "/([0-9a-f]{2})/" + guid + "\\.(" + extensions + ")$";
            var match = Regex.Match(relative, pattern);
            if (!match.Success || match.Groups[1].Value != match.Groups[2].Value[..2]) throw new BuildException($"Cook artifact violates GUID path contract: {relative}");
            if (folder == "Models" && (relative.EndsWith("/model.cemc") || relative.EndsWith("/sidecar.meta"))) { ++companions; continue; }
            if (relative.EndsWith("/generation.asset"))
                foreach (var companion in new[] { "model.cemc", "sidecar.meta" })
                    if (!File.Exists(Path.Combine(Path.GetDirectoryName(file)!, companion))) throw new BuildException($"Missing generation companion: {file}/{companion}");
            ++artifacts; ++byFolder[folder];
        }
        var generations = files.Count(f => Path.GetFileName(f) == "generation.asset");
        if (artifacts != expected || companions != 2 * generations) throw new BuildException($"Cook artifact/companion count mismatch: {artifacts}/{expected}, companions={companions}, models={generations}");
        return new(artifacts, companions, bytes, new FileInfo(manifest).Length, Metadata.Hash(manifest), files.Length, byFolder);
    }
    public static async Task<CookResult> Cook(BuildContext context, string cooker, string assets, string output, string generations, string bootstrapAssetSets = "", string textureIdentityRoot = "")
    {
        if (Directory.Exists(output)) throw new BuildException("Cook output must be a new directory.");
        var models = Models(assets); var all = Paths.Files(assets).Order(StringComparer.Ordinal).ToArray();
        var stale = all.Where(p => Path.GetExtension(p).Equals(".asset", StringComparison.OrdinalIgnoreCase) && Paths.Relative(assets, p).StartsWith("Models/", StringComparison.OrdinalIgnoreCase)).ToHashSet(Paths.Comparer);
        var bootstrap = bootstrapAssetSets.Length != 0;
        var arguments = new List<string> { "--asset-root", assets, "--output", output };
        if (bootstrap) arguments.AddRange(["--build-runtime-bootstrap", "--runtime-root", bootstrapAssetSets]);
        else arguments.AddRange(["--generation-root", generations]);
        if (textureIdentityRoot.Length != 0)
        {
            if (!bootstrap) throw new BuildException("Texture identity lookup is only valid for bootstrap documents.");
            arguments.AddRange(["--texture-identity-root", Paths.Canonical(textureIdentityRoot, true)]);
        }
        var counts = new Dictionary<string, int>();
        foreach (var (option, extensions) in new (string, string[])[] { ("--model", [".fbx", ".glb", ".gltf"]), ("--texture", [".png", ".jpg", ".jpeg", ".hdr", ".dds"]), ("--shadermeta", [".shadermeta"]), ("--shadergraph", [".shadergraph"]), ("--material", [".asset"]), ("--scene", [".creator", ".prefab"]) })
        {
            if (bootstrap && option != "--scene") continue;
            var sources = all.Where(p => extensions.Contains(Path.GetExtension(p).ToLowerInvariant()) && !stale.Contains(p)).ToArray(); counts[option] = sources.Length;
            foreach (var source in sources) { arguments.Add(option); arguments.Add(source); }
        }
        // Audio sources are discovered by the cooker's GUID sidecar scan.
        counts["audio-clips"] = all.Count(p => Path.GetExtension(p).ToLowerInvariant() is ".wav" or ".mp3" or ".flac");
        counts["sound-graphs"] = all.Count(p => Path.GetExtension(p).Equals(".soundgraph", StringComparison.OrdinalIgnoreCase));
        counts["sound-presets"] = all.Count(p => Path.GetExtension(p).Equals(".soundpreset", StringComparison.OrdinalIgnoreCase));
        // Trusted raw font bytes are pass-through Assets payloads. Their GUIDs
        // enter CEMF through sidecars; no synthetic font artifact is generated.
        counts["fonts"] = all.Count(p => Path.GetExtension(p).ToLowerInvariant() is ".ttf" or ".otf");
        var argumentsFile = Path.Combine(Path.GetDirectoryName(output)!, "cook.arguments");
        if (arguments.Any(a => a.IndexOfAny(['\r', '\n', '\0']) >= 0)) throw new BuildException("Cook argument contains an unsupported control character.");
        await File.WriteAllLinesAsync(argumentsFile, arguments, new System.Text.UTF8Encoding(false), context.Cancellation);
        ProcessResult log;
        try { log = await context.Run(cooker, ["--arguments-file", argumentsFile], assets); }
        finally { File.Delete(argumentsFile); }
        var summary = Regex.Match(log.Output, @"(?m)^asset-cooker models=[^\r\n]*");
        int Metric(string name) { var match = Regex.Match(summary.Value, name + @"=(\d+)"); return match.Success ? int.Parse(match.Groups[1].Value) : throw new BuildException($"Cook summary missing {name}."); }
        var result = Validate(output, Metric("artifactPaths"), bootstrap);
        result.ModelCount = models.Length; result.SourceCounts = counts; result.LegacyModelCookCaches = stale.Count; result.LegacyTextureNameRefs = Metric("legacyTextureNameRefs");
        return result;
    }
    public static async Task<DocumentResult> Documents(BuildContext context, string cooker, string root, string terrainSourceRoot = "")
    {
        var arguments = new List<string> { "--compile-runtime-documents", "--runtime-root", root };
        if (terrainSourceRoot.Length != 0) arguments.AddRange(["--terrain-source-root", terrainSourceRoot]);
        var log = await context.Run(cooker, arguments);
        var summary = Regex.Match(log.Output, @"(?m)^asset-cooker runtime-documents=(\d+) bytes=(\d+) format=CEDO1\+TRBN2\r?$");
        if (!summary.Success) throw new BuildException("Runtime document cook summary missing.");
        var extensions = new[] { ".inputmap", ".bt", ".blackboard", ".renderprofile", ".terrain", ".foliage" };
        var documents = Paths.Files(Path.Combine(root, "ProjectSetting")).Where(p => Path.GetExtension(p).Equals(".asset", StringComparison.OrdinalIgnoreCase))
            .Concat(Paths.Files(Path.Combine(root, "Assets")).Where(p => extensions.Contains(Path.GetExtension(p).ToLowerInvariant()))).ToArray();
        var count = int.Parse(summary.Groups[1].Value); var bytes = long.Parse(summary.Groups[2].Value);
        if (documents.Length != count || documents.Sum(p => new FileInfo(p).Length) != bytes) throw new BuildException("Runtime document count/size differs from cook output.");
        foreach (var document in documents)
        {
            using var file = File.OpenRead(document);
            if (Path.GetExtension(document).Equals(".terrain", StringComparison.OrdinalIgnoreCase))
            {
                var header = new byte[48];
                if (file.Read(header) != header.Length
                    || System.Buffers.Binary.BinaryPrimitives.ReadUInt32LittleEndian(header) != 0x5442524e
                    || System.Buffers.Binary.BinaryPrimitives.ReadUInt32LittleEndian(header.AsSpan(4)) != 2
                    || System.Buffers.Binary.BinaryPrimitives.ReadUInt64LittleEndian(header.AsSpan(8)) != (ulong)file.Length)
                    throw new BuildException($"Runtime terrain is not a complete TRBN2 artifact: {document}");
            }
            else
            {
                var magic = new byte[4];
                if (file.Read(magic) != 4 || !magic.AsSpan().SequenceEqual("CEDO"u8))
                    throw new BuildException($"Runtime document is not CEDO: {document}");
            }
        }
        return new(count, bytes, "CEDO1+TRBN2");
    }
}
