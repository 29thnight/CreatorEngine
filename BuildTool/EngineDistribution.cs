using System.Text.Json.Nodes;

namespace CreatorBuildTool;

internal sealed record EngineDistribution(string Root, JsonObject Manifest)
{
    public string Configuration => Manifest.Text("configuration");
    public string BinaryRoot => Paths.Child(Root, Manifest.Text("binaryRoot"));
    public static EngineDistribution Load(string root, BuildContext context)
    {
        root = Paths.Canonical(root, true);
        var manifest = Metadata.Read(Path.Combine(root, "engine.manifest.json"));
        Metadata.ValidateVersion(manifest);
        if (manifest.Text("platform") != "win-x64" || manifest.Text("configuration") is not ("Debug" or "Release") ||
            manifest.Text("binaryRoot") != $"Bin/x64-{manifest.Text("configuration")}" ||
            !Guid.TryParseExact(manifest.Text("buildId"), "D", out var id) || id == Guid.Empty)
            throw new BuildException("Invalid engine distribution identity.");
        var files = Metadata.ParseEntries(manifest.Array("files"));
        if (Metadata.Digest(files) != manifest.Text("payloadDigest")) throw new BuildException("Engine payload digest mismatch.");
        Metadata.Verify(root, files, context.Cancellation);
        var expected = files.Select(f => f.Path).Append("engine.manifest.json").Append("engine.info").ToHashSet(Paths.Comparer);
        foreach (var file in Paths.Files(root))
            if (!expected.Contains(Paths.Relative(root, file))) throw new BuildException($"Unlisted file in immutable distribution: {file}");
        if (File.ReadAllText(Paths.Child(root, "engine.info")) != Metadata.Info(manifest)) throw new BuildException("Native engine metadata differs from its manifest.");
        return new(root, manifest);
    }
    public static string Resolve(string repository, string configuration, string explicitRoot)
    {
        if (explicitRoot.Length > 0) return explicitRoot;
        if (File.Exists(Path.Combine(repository, "engine.manifest.json"))) return repository;
        var pointer = Path.Combine(repository, $"Bin/x64-{configuration}/engine.distribution.json");
        if (!File.Exists(pointer)) throw new BuildException($"No prebuilt {configuration} engine. Use CreatorBuildTool publish-engine, or --engine-distribution.");
        return Metadata.Read(pointer).Text("path");
    }
    public void AssertProject(string project)
    {
        var pinPath = Paths.Child(project, $"ProjectSetting/Engine.{Configuration}.lock.json");
        if (!File.Exists(pinPath)) throw new BuildException($"Project engine pin missing: {pinPath}. Use select-engine.");
        var pin = Metadata.Read(pinPath);
        if (pin.Int("schemaVersion") != 1 || pin.Text("configuration") != Configuration ||
            pin.Text("version") != Manifest.Text("version") || pin.Text("buildId") != Manifest.Text("buildId"))
            throw new BuildException("Project engine pin does not match the selected distribution.");
    }
    public void SelectProject(string project)
    {
        project = Paths.Canonical(project, true);
        if (!Directory.Exists(Paths.Child(project, "ProjectSetting"))) throw new BuildException("ProjectSetting directory missing.");
        Metadata.Write(Paths.Child(project, $"ProjectSetting/Engine.{Configuration}.lock.json"), new
        { schemaVersion = 1, version = Manifest.Text("version"), buildId = Manifest.Text("buildId"), configuration = Configuration });
    }
}
