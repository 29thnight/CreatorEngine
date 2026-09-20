using System.IO.Compression;
using System.Text.RegularExpressions;

namespace CreatorBuildTool;

internal sealed record Preflight(string StartupScene, string RuntimeBackend, Dictionary<string, int> SceneCounts, bool RequiresManagedLifecycle);
internal static class PackageInputs
{
    public static readonly string[] BackendPatterns = [@"(?ms)(^render:\s*\r?\n\s+backend:\s*)(dx12|vulkan)(\s*$)", @"(?ms)(^build:\s*\r?\n\s+render:\s*\r?\n\s+backend:\s*)(dx12|vulkan)(\s*$)"];
    private static bool Generated(string relative) => relative.Equals("EngineSettings.asset", StringComparison.OrdinalIgnoreCase) || relative.EndsWith(".runtime.yml", StringComparison.OrdinalIgnoreCase);
    // Script identities belong to the managed assembly. Keeping their sidecars
    // in cook input would create CEMF entries for source files omitted from PAK.
    private static bool ScriptSource(string relative) => relative.EndsWith(".cs", StringComparison.OrdinalIgnoreCase) || relative.EndsWith(".cs.meta", StringComparison.OrdinalIgnoreCase);
    public static int CopyProject(string project, string destination, CancellationToken token)
    {
        var count = 0;
        foreach (var name in new[] { "Assets", "ProjectSetting" })
        {
            var root = Path.Combine(project, name);
            foreach (var file in Paths.Files(root))
            {
                token.ThrowIfCancellationRequested(); var relative = Paths.Relative(root, file);
                if (name == "ProjectSetting" && Generated(relative)) continue;
                if (name == "Assets" && ScriptSource(relative)) continue;
                Paths.Copy(file, Paths.Child(destination, name + "/" + relative)); ++count;
            }
        }
        return count;
    }
    public static async Task<int> CopyWorkspace(BuildContext context, string repository, string project, string destination)
    {
        RequireRepositoryProject(repository, project);
        _ = Paths.Files(Path.Combine(project, "Assets")).Count(); _ = Paths.Files(Path.Combine(project, "ProjectSetting")).Count();
        var result = await context.Run("git", ["-C", repository, "-c", "core.quotepath=false", "ls-files", "--", "Dynamic_CPP/Assets/**", "Dynamic_CPP/ProjectSetting/**"], echo: false);
        var copied = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var path in result.Output.Split('\n', StringSplitOptions.RemoveEmptyEntries).Select(p => p.TrimEnd('\r')))
        {
            context.Cancellation.ThrowIfCancellationRequested();
            if (!path.StartsWith("Dynamic_CPP/", StringComparison.Ordinal)) throw new BuildException($"Unexpected tracked path: {path}");
            var relative = path["Dynamic_CPP/".Length..];
            if (relative.StartsWith("ProjectSetting/") && Generated(relative["ProjectSetting/".Length..])) continue;
            if (relative.StartsWith("Assets/") && ScriptSource(relative)) continue;
            Paths.Copy(Paths.Child(project, relative), Paths.Child(destination, relative)); copied.Add(relative);
        }
        const string scene = "Assets/Scenes/FT_Primitives.creator";
        if (copied.Add(scene)) Paths.Copy(Paths.Child(project, scene), Paths.Child(destination, scene));
        return copied.Count;
    }
    public static void RequireRepositoryProject(string repository, string project)
    {
        if (!Paths.Comparer.Equals(Paths.Canonical(Path.Combine(repository, "Dynamic_CPP"), true), project))
            throw new BuildException("Workspace/Tracked input modes support only this checkout's Dynamic_CPP project.");
    }
    public static async Task<string> Snapshot(BuildContext context, string repository, string project, string destination, string commit)
    {
        RequireRepositoryProject(repository, project);
        foreach (var required in new[] { "Dynamic_CPP/Assets/Scenes/FT_Primitives.creator", "Tools/packaging/templates/EngineSettings.runtime.yml" })
            await context.Run("git", ["-C", repository, "cat-file", "-e", commit + ":" + required], echo: false);
        var archive = destination + ".zip";
        await context.Run("git", ["-C", repository, "archive", "--format=zip", "--output=" + archive, commit, "--", "Dynamic_CPP/Assets", "Dynamic_CPP/ProjectSetting", "Tools/packaging/templates/EngineSettings.runtime.yml"], echo: false);
        try
        {
            using var zip = ZipFile.OpenRead(archive);
            foreach (var entry in zip.Entries)
            {
                context.Cancellation.ThrowIfCancellationRequested();
                var target = Paths.Child(destination, entry.FullName);
                if (((entry.ExternalAttributes >> 16) & 0xF000) == 0xA000) throw new BuildException("Tracked symlink is not allowed in package input.");
                if (entry.FullName.EndsWith('/')) Directory.CreateDirectory(target);
                else { Directory.CreateDirectory(Path.GetDirectoryName(target)!); entry.ExtractToFile(target); }
            }
        }
        finally { Paths.AssertChild(archive, Path.GetDirectoryName(destination)!); File.Delete(archive); }
        return destination;
    }
    public static void Materialize(string template, string destination, string requestedScene, string requestedBackend)
    {
        var text = File.ReadAllText(template);
        if (requestedScene.Length > 0)
        {
            ValidateSceneName(requestedScene);
            const string pattern = @"(?m)^(startupSceneName:\s*)[^\r\n]+?\s*$";
            if (Regex.Matches(text, pattern).Count != 1) throw new BuildException("Runtime template must have exactly one startupSceneName.");
            text = Regex.Replace(text, pattern, m => m.Groups[1].Value + '"' + requestedScene + '"');
        }
        foreach (var pattern in BackendPatterns) if (Regex.Matches(text, pattern).Count != 1) throw new BuildException("Ambiguous runtime/build backend in template.");
        var backend = requestedBackend.Length == 0 ? Regex.Match(text, BackendPatterns[1]).Groups[2].Value : requestedBackend.ToLowerInvariant();
        if (backend is not ("dx12" or "vulkan")) throw new BuildException("Render backend must be dx12 or vulkan.");
        foreach (var pattern in BackendPatterns) text = Regex.Replace(text, pattern, m => m.Groups[1].Value + backend + m.Groups[3].Value);
        Paths.NoReparseAncestors(destination); Directory.CreateDirectory(Path.GetDirectoryName(destination)!); File.WriteAllText(destination, text);
    }
    private static void ValidateSceneName(string name)
    {
        if (Path.GetFileName(name) != name || name.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0 || !name.EndsWith(".creator", StringComparison.OrdinalIgnoreCase))
            throw new BuildException($"Startup scene must be a plain .creator file name: {name}");
    }
    public static Preflight Validate(string merged, string settings)
    {
        var text = File.ReadAllText(settings);
        if (!Regex.IsMatch(text, @"(?ms)^lastWindowSize:\s*\r?\n\s+x:\s*[-+0-9.]+\s*\r?\n\s+y:\s*[-+0-9.]+") || !Regex.IsMatch(text, @"(?m)^renderPassSettings:\s*$"))
            throw new BuildException("Runtime template lacks window/render pass settings.");
        var matches = Regex.Matches(text, @"(?m)^startupSceneName:\s*([^\r\n]+?)\s*$");
        if (matches.Count != 1) throw new BuildException("Runtime settings require exactly one startupSceneName.");
        var scene = matches[0].Groups[1].Value.Trim();
        if (scene.Length >= 2 && (scene[0] == '"' && scene[^1] == '"' || scene[0] == '\'' && scene[^1] == '\'')) scene = scene[1..^1];
        ValidateSceneName(scene);
        var runtimeBackend = Regex.Match(text, BackendPatterns[0]); var buildBackend = Regex.Match(text, BackendPatterns[1]);
        if (!runtimeBackend.Success || !buildBackend.Success || runtimeBackend.Groups[2].Value != buildBackend.Groups[2].Value) throw new BuildException("Runtime/build backend mismatch.");
        var sceneText = File.ReadAllText(Paths.Child(merged, "Assets/Scenes/" + scene));
        var counts = new Dictionary<string, int>();
        foreach (var (name, type, indent) in new[] { ("Entity", "Entity", 2), ("Transform", "Transform", 6), ("Mesh", "MeshRenderer", 6), ("Camera", "CameraComponent", 6), ("Light", "LightComponent", 6), ("Script", "ScriptComponent", 6) })
            counts[name] = Regex.Matches(sceneText, @"(?m)^\s{" + indent + "}- " + type + @":\s+\d+\s*$").Count;
        if (Regex.IsMatch(sceneText, @"(?m)^m_SceneObjects:\s*$|^\s+m_transform:\s*$|^\s+m_gameObjectType:\s*")) throw new BuildException("Startup scene contains legacy GameObject schema.");
        var probe = scene.Equals("FT_Primitives.creator", StringComparison.OrdinalIgnoreCase);
        if (probe && (counts["Entity"] != 11 || counts["Transform"] != 11 || counts["Mesh"] != 8 || counts["Camera"] != 1 || counts["Light"] != 1 || counts["Script"] != 1 ||
            Regex.Matches(sceneText, @"(?m)^\s+m_scriptType:\s*PackageSmokeProbe\s*$").Count != 1)) throw new BuildException("FT_Primitives schema/probe count mismatch.");
        if (!File.Exists(Paths.Child(merged, "Assets/Shaders/DefaultPassShader/WorldSprite.slang"))) throw new BuildException("Shader closure missing WorldSprite.slang.");
        if (File.Exists(Paths.Child(merged, "ProjectSetting/EngineSettings.runtime.yml"))) throw new BuildException("Runtime template leaked into package input.");
        var lifecycleProbe = Regex.IsMatch(sceneText, @"(?m)^\s+m_scriptType:\s*PackageSmokeProbe\s*$");
        return new(scene, runtimeBackend.Groups[2].Value, counts, lifecycleProbe);
    }
    public static bool Excluded(string path) => Path.GetExtension(path).ToLowerInvariant() is ".cpp" or ".h" or ".hpp" or ".cs" or ".meta" or ".json";
}
