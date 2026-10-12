using System.Globalization;
using System.Text;
using System.Text.RegularExpressions;

namespace CreatorBuildTool;

// A strict projection of the editor's flat portable mapping, not a general YAML
// loader. Never copy machine-local SDK paths or identities into a game package.
internal static class RenderFeatureSettings
{
    private sealed record Entry(string Value, int Start, int End, int Indent);

    private static Dictionary<string, Entry> Children(string[] lines, int start, int end, int parentIndent)
    {
        var entries = new Dictionary<string, Entry>(StringComparer.Ordinal);
        int? childIndent = null;
        for (var i = start; i < end; ++i)
        {
            var line = lines[i];
            if (string.IsNullOrWhiteSpace(line) || line.TrimStart().StartsWith('#') || line.Trim() == "---")
            {
                continue;
            }
            var indent = line.Length - line.TrimStart(' ').Length;
            if (line.Contains('\t') || indent <= parentIndent)
            {
                throw new BuildException("Render feature settings require space-indented block mappings.");
            }
            childIndent ??= indent;
            if (indent != childIndent)
            {
                throw new BuildException("Ambiguous render feature mapping indentation.");
            }
            var match = Regex.Match(line[indent..], @"^([A-Za-z_][A-Za-z0-9_]*):(?:[ ]*(.*))?$");
            if (!match.Success)
            {
                throw new BuildException("Unsupported render feature mapping syntax.");
            }
            var next = i + 1;
            while (next < end)
            {
                var nested = lines[next];
                if (!string.IsNullOrWhiteSpace(nested) && !nested.TrimStart().StartsWith('#') &&
                    nested.Length - nested.TrimStart(' ').Length <= indent)
                {
                    break;
                }
                ++next;
            }
            var key = match.Groups[1].Value;
            if (!entries.TryAdd(key, new Entry(match.Groups[2].Value.Trim(), i + 1, next, indent)))
            {
                throw new BuildException($"Duplicate settings field: {key}");
            }
            i = next - 1;
        }
        return entries;
    }

    private static Dictionary<string, Entry> Map(string[] lines, Entry entry)
    {
        if (Regex.Replace(entry.Value, @"\s+#.*$", "").Trim() == "{}")
        {
            return new Dictionary<string, Entry>(StringComparer.Ordinal);
        }
        if (entry.Value.Length != 0 && !entry.Value.StartsWith('#'))
        {
            throw new BuildException("Render feature settings must be a block mapping.");
        }
        return Children(lines, entry.Start, entry.End, entry.Indent);
    }

    private static string Scalar(string[] lines, Entry entry)
    {
        for (var i = entry.Start; i < entry.End; ++i)
        {
            if (!string.IsNullOrWhiteSpace(lines[i]) && !lines[i].TrimStart().StartsWith('#'))
            {
                throw new BuildException("Render feature values must be flat scalars.");
            }
        }
        var value = Regex.Replace(entry.Value, @"\s+#.*$", "").Trim();
        if (value.Length >= 2 && (value[0] == '"' && value[^1] == '"' || value[0] == '\'' && value[^1] == '\''))
        {
            value = value[1..^1];
        }
        return value;
    }

    public static string FromProject(string settingsPath)
    {
        var values = new Dictionary<string, string>(StringComparer.Ordinal)
        {
            ["schemaVersion"] = "1", ["enabled"] = "true", ["upscaler"] = "none", ["quality"] = "quality",
            ["frameGenerator"] = "none", ["interpolatedFrameCount"] = "1", ["latencyMode"] = "off",
            ["fallbackAa"] = "true", ["spatialMode"] = "off", ["spatialRenderScale"] = "0.77",
            ["spatialSharpness"] = "0.5", ["digitalVibrance"] = "false", ["vibranceIntensity"] = "0.5",
            ["saturationBoost"] = "0.25"
        };
        if (File.Exists(settingsPath))
        {
            Paths.NoReparseAncestors(settingsPath);
            var lines = File.ReadAllLines(settingsPath);
            var root = Children(lines, 0, lines.Length, -1);
            // Preserve the legacy AA preference when migrating an old project.
            if (root.TryGetValue("renderPassSettings", out var passes))
            {
                var passMap = Map(lines, passes);
                if (passMap.TryGetValue("aa", out var aa) && Map(lines, aa).TryGetValue("isApply", out var enabled))
                {
                    values["fallbackAa"] = Scalar(lines, enabled);
                }
            }
            if (root.TryGetValue("build", out var build) && Map(lines, build).TryGetValue("renderFeatures", out var features))
            {
                foreach (var (key, entry) in Map(lines, features))
                {
                    if (!values.ContainsKey(key))
                    {
                        throw new BuildException($"Nonportable or unknown render feature field: {key}");
                    }
                    values[key] = Scalar(lines, entry);
                }
            }
        }
        foreach (var key in new[] { "enabled", "fallbackAa", "digitalVibrance" })
        {
            if (!bool.TryParse(values[key], out var boolean))
            {
                throw new BuildException($"Invalid render feature boolean: {key}");
            }
            values[key] = boolean ? "true" : "false";
        }
        if (values["schemaVersion"] != "1")
        {
            throw new BuildException("Unsupported render feature schema version.");
        }
        if (!uint.TryParse(values["interpolatedFrameCount"], NumberStyles.None, CultureInfo.InvariantCulture, out var count) || count == 0)
        {
            throw new BuildException("interpolatedFrameCount must be a positive uint32 value.");
        }
        values["interpolatedFrameCount"] = count.ToString(CultureInfo.InvariantCulture);
        foreach (var key in new[] { "upscaler", "frameGenerator" })
        {
            if (values[key] is not ("none" or "fsr" or "dlss" or "xess"))
            {
                throw new BuildException($"Invalid render feature provider: {key}");
            }
        }
        if (values["quality"] is not ("native-aa" or "quality" or "balanced" or "performance" or "ultra-performance") ||
            values["latencyMode"] is not ("off" or "on" or "on-boost") || values["spatialMode"] is not ("off" or "scale" or "sharpen"))
        {
            throw new BuildException("Invalid render feature mode.");
        }
        foreach (var key in new[] { "spatialRenderScale", "spatialSharpness", "vibranceIntensity", "saturationBoost" })
        {
            if (!float.TryParse(values[key], NumberStyles.Float, CultureInfo.InvariantCulture, out var value) ||
                !float.IsFinite(value) || value > 1 || value < (key == "spatialRenderScale" ? 0.5f : 0f))
            {
                throw new BuildException($"Invalid render feature scalar: {key}");
            }
            values[key] = value.ToString("R", CultureInfo.InvariantCulture);
        }
        var output = new StringBuilder("renderFeatures:\n");
        foreach (var (key, value) in values)
        {
            output.Append("  ").Append(key).Append(": ").Append(value).Append('\n');
        }
        return output.ToString();
    }

    public static string Apply(string template, string block)
    {
        // A template may already contain defaults. Replace exactly that root
        // mapping; never copy the editor-only block or its build wrapper.
        const string pattern = @"(?m)^renderFeatures:[^\r\n]*(?:\r?\n(?:[ ]+[^\r\n]*|[ \t]*))*\r?\n?";
        if (Regex.Matches(template, @"(?m)^renderFeatures:").Count > 1)
        {
            throw new BuildException("Duplicate renderFeatures in runtime template.");
        }
        return Regex.Replace(template, pattern, "").TrimEnd() + "\n" + block;
    }
}
