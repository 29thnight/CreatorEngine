namespace CreatorBuildTool;

internal static class RuntimeFonts
{
    public const string RelativeRoot = "Resources/Fonts/Runtime";
    public static readonly string[] RequiredFiles =
    [
        "Inter-Regular.ttf", "LICENSE-Inter.txt",
        "NanumGothic-Regular.ttf", "LICENSE-NanumGothic.txt",
        "README.md", "provenance.json"
    ];

    public static string Require(string binaryRoot)
    {
        var root = Paths.Child(binaryRoot, RelativeRoot);
        foreach (var name in RequiredFiles)
        {
            var path = Paths.Child(root, name);
            if (!File.Exists(path) || new FileInfo(path).Length == 0)
            {
                throw new BuildException($"Runtime font resource or license is missing: {path}. Rebuild and publish the engine resources.");
            }
        }
        return root;
    }
}
