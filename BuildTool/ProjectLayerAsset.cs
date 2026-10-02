using System.Buffers.Binary;

namespace CreatorBuildTool;

internal static class ProjectLayerAsset
{
    public const string RelativePath = "ProjectSetting/Layers.celayers";

    public static void Require(string project)
    {
        var path = Paths.Child(project, RelativePath);
        if (!File.Exists(path))
            throw new BuildException("Project layer settings missing. Open the project in the current Editor to migrate layers before packaging.");

        var length = new FileInfo(path).Length;
        if (length < 1071 || length > 64 * 1024)
            throw new BuildException("Project layer settings size is invalid.");

        var bytes = File.ReadAllBytes(path);
        if (bytes.Length != length || BinaryPrimitives.ReadUInt32LittleEndian(bytes) != 0x52594c43 ||
            BinaryPrimitives.ReadUInt32LittleEndian(bytes.AsSpan(4)) != 1)
            throw new BuildException("Project layer settings require CLYR version 1.");

        ulong checksum = 14695981039346656037;
        foreach (var value in bytes.AsSpan(0, bytes.Length - 8))
            checksum = unchecked((checksum ^ value) * 1099511628211);

        if (checksum != BinaryPrimitives.ReadUInt64LittleEndian(bytes.AsSpan(bytes.Length - 8)))
            throw new BuildException("Project layer settings checksum mismatch.");
    }
}
