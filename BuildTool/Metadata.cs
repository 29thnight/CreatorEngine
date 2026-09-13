using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Text.RegularExpressions;

namespace CreatorBuildTool;

internal sealed record FileEntry(string Path, long Bytes, string Sha256);

internal static class Metadata
{
    public static readonly JsonSerializerOptions Format = new() { WriteIndented = true, PropertyNamingPolicy = JsonNamingPolicy.CamelCase };
    public static JsonObject Object(object value) => JsonSerializer.SerializeToNode(value, Format)!.AsObject();
    public static JsonObject Read(string path) { Paths.NoReparseAncestors(path); return JsonNode.Parse(File.ReadAllText(path))?.AsObject() ?? throw new BuildException($"Invalid JSON object: {path}"); }
    public static string Text(this JsonNode value, string key) => value[key]?.GetValue<string>() ?? throw new BuildException($"Missing string: {key}");
    public static int Int(this JsonNode value, string key) => value[key]?.GetValue<int>() ?? throw new BuildException($"Missing integer: {key}");
    public static bool Bool(this JsonNode value, string key) => value[key]?.GetValue<bool>() ?? throw new BuildException($"Missing boolean: {key}");
    public static JsonArray Array(this JsonNode value, string key) => value[key]?.AsArray() ?? throw new BuildException($"Missing array: {key}");
    public static string Hash(string path) { using var stream = File.OpenRead(path); return Convert.ToHexStringLower(SHA256.HashData(stream)); }
    public static FileEntry Entry(string root, string relative)
    {
        var file = Paths.Child(root, relative); return new(relative.Replace('\\', '/'), new FileInfo(file).Length, Hash(file));
    }
    public static FileEntry[] Entries(string root, IEnumerable<string>? paths = null) =>
        (paths ?? Paths.Files(root).Select(p => Paths.Relative(root, p))).Select(p => Entry(root, p)).OrderBy(e => e.Path, StringComparer.Ordinal).ToArray();
    public static FileEntry[] ParseEntries(JsonArray values) => values.Select(n => n!.Deserialize<FileEntry>(Format) ?? throw new BuildException("Invalid file entry")).ToArray();
    public static string Digest(IEnumerable<FileEntry> entries)
    {
        var unique = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var ordered = entries.OrderBy(e => e.Path, StringComparer.Ordinal).ToArray();
        foreach (var entry in ordered)
            if (!unique.Add(entry.Path) || entry.Path.IndexOfAny(['\r', '\n', '\0']) >= 0 || !Regex.IsMatch(entry.Sha256, "^[0-9a-f]{64}$") || entry.Bytes < 0)
                throw new BuildException($"Duplicate or invalid payload entry: {entry.Path}");
        return Convert.ToHexStringLower(SHA256.HashData(Encoding.UTF8.GetBytes(string.Join('\n', ordered.Select(e => e.Path + '\0' + e.Sha256)))));
    }
    public static void Verify(string root, IEnumerable<FileEntry> entries, CancellationToken token = default)
    {
        foreach (var entry in entries)
        {
            token.ThrowIfCancellationRequested(); var path = Paths.Child(root, entry.Path);
            if (!File.Exists(path) || new FileInfo(path).Length != entry.Bytes || Hash(path) != entry.Sha256) throw new BuildException($"Payload missing or changed: {entry.Path}");
        }
    }
    public static void Write(string path, object value)
    {
        Paths.NoReparseAncestors(path); Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(path))!);
        var temporary = path + "." + Guid.NewGuid().ToString("N") + ".tmp";
        File.WriteAllText(temporary, JsonSerializer.Serialize(value, Format) + "\n", new UTF8Encoding(false));
        try { File.Move(temporary, path, true); } finally { if (File.Exists(temporary)) File.Delete(temporary); }
    }
    public static void ValidateVersion(JsonNode value)
    {
        if (value.Int("schemaVersion") != 1 || value.Text("productName") != "CreatorEngine 2") throw new BuildException("Unsupported engine product metadata.");
        var version = value.Text("version");
        if (!Regex.IsMatch(version, @"^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$") ||
            version.Split('.').Any(p => !ushort.TryParse(p, out _))) throw new BuildException("Engine version requires four integers in 0..65535.");
        var feature = value.Text("featureRelease"); var channel = value.Text("channel");
        if (channel is not ("preview" or "stable") || (feature.Length > 0 && !Regex.IsMatch(feature, "^[0-9]{2}H[12]$"))) throw new BuildException("Invalid engine release/channel.");
        if (value.Bool("localDevelopment") ? channel != "preview" : feature.Length == 0 || version == "0.0.0.0") throw new BuildException("Invalid local/published engine version.");
    }
    public static string Info(JsonNode value) => string.Concat(new[] { "productName", "featureRelease", "version", "channel", "localDevelopment", "buildId", "payloadDigest", "configuration", "shipping" }
        .Select(key => key + "=" + (key is "localDevelopment" or "shipping" ? value.Bool(key).ToString().ToLowerInvariant() : value.Text(key)) + "\n"));
}
