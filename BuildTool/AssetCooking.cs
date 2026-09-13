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
        if (models.Length == 0) throw new BuildException($"No model sources in package assets: {assets}");
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
            if (library.Length == 0)
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
    public static CookResult Validate(string output, int expected)
    {
        const string guid = "([0-9a-f]{8}-[0-9a-f]{4}-[48][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12})";
        var rules = new Dictionary<string, string> { ["Models"] = "", ["Textures"] = "png|hdr|dds|jpg", ["ShaderMeta"] = "shadermeta", ["Materials"] = "asset", ["Scenes"] = "creator", ["Prefabs"] = "prefab" };
        var derived = Path.Combine(output, "Derived"); var manifest = Path.Combine(derived, "asset-manifest.cemf");
        if (!File.Exists(manifest)) throw new BuildException("Cooked asset manifest missing.");
        var files = Paths.Files(derived).ToArray(); var byFolder = rules.Keys.ToDictionary(k => k, _ => 0);
        var artifacts = 0; var companions = 0; var bytes = 0L;
        foreach (var file in files.Where(f => !Paths.Comparer.Equals(f, manifest)))
        {
            bytes += new FileInfo(file).Length;
            var relative = Paths.Relative(derived, file); var folder = relative.Split('/')[0];
            if (!rules.TryGetValue(folder, out var extensions)) throw new BuildException($"Unexpected Derived folder: {relative}");
            var pattern = folder == "Models" ? "^Models/([0-9a-f]{2})/" + guid + "/[0-9]+/(generation\\.asset|model\\.cemc|sidecar\\.meta|textures/" + guid + "\\.png)$"
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
    public static async Task<CookResult> Cook(BuildContext context, string cooker, string assets, string output, string generations)
    {
        if (Directory.Exists(output)) throw new BuildException("Cook output must be a new directory.");
        var models = Models(assets); var all = Paths.Files(assets).Order(StringComparer.Ordinal).ToArray();
        var stale = all.Where(p => Path.GetExtension(p).Equals(".asset", StringComparison.OrdinalIgnoreCase) && Paths.Relative(assets, p).StartsWith("Models/", StringComparison.OrdinalIgnoreCase)).ToHashSet(Paths.Comparer);
        var arguments = new List<string> { "--asset-root", assets, "--output", output, "--generation-root", generations };
        var counts = new Dictionary<string, int>();
        foreach (var (option, extensions) in new (string, string[])[] { ("--model", [".fbx", ".glb", ".gltf"]), ("--texture", [".png", ".hdr", ".dds"]), ("--shadermeta", [".shadermeta"]), ("--material", [".asset"]), ("--scene", [".creator", ".prefab"]) })
        {
            var sources = all.Where(p => extensions.Contains(Path.GetExtension(p).ToLowerInvariant()) && !stale.Contains(p)).ToArray(); counts[option] = sources.Length;
            foreach (var source in sources) { arguments.Add(option); arguments.Add(source); }
        }
        var argumentsFile = Path.Combine(Path.GetDirectoryName(output)!, "cook.arguments");
        if (arguments.Any(a => a.IndexOfAny(['\r', '\n', '\0']) >= 0)) throw new BuildException("Cook argument contains an unsupported control character.");
        await File.WriteAllLinesAsync(argumentsFile, arguments, new System.Text.UTF8Encoding(false), context.Cancellation);
        ProcessResult log;
        try { log = await context.Run(cooker, ["--arguments-file", argumentsFile], assets); }
        finally { File.Delete(argumentsFile); }
        var summary = Regex.Match(log.Output, @"(?m)^asset-cooker models=[^\r\n]*");
        int Metric(string name) { var match = Regex.Match(summary.Value, name + @"=(\d+)"); return match.Success ? int.Parse(match.Groups[1].Value) : throw new BuildException($"Cook summary missing {name}."); }
        var result = Validate(output, Metric("artifactPaths"));
        result.ModelCount = models.Length; result.SourceCounts = counts; result.LegacyModelCookCaches = stale.Count; result.LegacyTextureNameRefs = Metric("legacyTextureNameRefs");
        return result;
    }
    public static async Task<DocumentResult> Documents(BuildContext context, string cooker, string root)
    {
        var log = await context.Run(cooker, ["--compile-runtime-documents", "--runtime-root", root]);
        var summary = Regex.Match(log.Output, @"(?m)^asset-cooker runtime-documents=(\d+) bytes=(\d+) format=CEDO1\r?$");
        if (!summary.Success) throw new BuildException("Runtime document cook summary missing.");
        var extensions = new[] { ".inputmap", ".bt", ".blackboard", ".volume", ".terrain", ".foliage" };
        var documents = Paths.Files(Path.Combine(root, "ProjectSetting")).Where(p => Path.GetExtension(p).Equals(".asset", StringComparison.OrdinalIgnoreCase))
            .Concat(Paths.Files(Path.Combine(root, "Assets")).Where(p => extensions.Contains(Path.GetExtension(p).ToLowerInvariant()))).ToArray();
        var count = int.Parse(summary.Groups[1].Value); var bytes = long.Parse(summary.Groups[2].Value);
        if (documents.Length != count || documents.Sum(p => new FileInfo(p).Length) != bytes) throw new BuildException("Runtime document count/size differs from cook output.");
        foreach (var document in documents)
        {
            using var file = File.OpenRead(document); var magic = new byte[4];
            if (file.Read(magic) != 4 || !magic.AsSpan().SequenceEqual("CEDO"u8)) throw new BuildException($"Runtime document is not CEDO: {document}");
        }
        return new(count, bytes, "CEDO1");
    }
}
