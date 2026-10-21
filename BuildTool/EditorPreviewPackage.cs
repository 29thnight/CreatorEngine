using System.Text.Json.Nodes;

namespace CreatorBuildTool;

internal static class EditorPreviewPackage
{
    private const string DirectoryName = "Editor/EditorResources/Preview";
    private const string ProductName = "a5033a2b-a423-8a02-9245-8b968e8e707b.slang.scene-cache";

    // Optional for existing distributions. If supplied, publish only the sealed
    // product/manifest pair and reject a stale or damaged build artifact.
    internal static FileEntry[] Entries(string binaryRoot)
    {
        var manifestPath = Paths.Child(binaryRoot, DirectoryName + "/package.json");
        if (!File.Exists(manifestPath)) return [];
        var manifest = Metadata.Read(manifestPath);
        if (manifest.Int("schemaVersion") != 1 || manifest.Text("backend") != "dxil" ||
            manifest.Text("recipe") != "editor-preview-minimal-checker-v3" || manifest.Text("file") != ProductName)
            throw new BuildException("Unsupported editor preview package. Regenerate it with Build-EditorPreviewPackage.ps1.");
        var runtime = Paths.Child(binaryRoot, "Editor/CreatorEditor.runtime.dll");
        var productRelative = DirectoryName + "/" + ProductName;
        var product = Paths.Child(binaryRoot, productRelative);
        Paths.NoReparseAncestors(product);
        if (!Metadata.Hash(runtime).Equals(manifest.Text("runtimeSha256"), StringComparison.OrdinalIgnoreCase) ||
            new FileInfo(product).Length != manifest["bytes"]!.GetValue<long>() ||
            !Metadata.Hash(product).Equals(manifest.Text("sha256"), StringComparison.OrdinalIgnoreCase))
            throw new BuildException("Editor preview package is stale or damaged. Regenerate it before publishing.");
        return Metadata.Entries(binaryRoot, [productRelative, DirectoryName + "/package.json"]);
    }
}
