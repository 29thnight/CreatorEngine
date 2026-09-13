using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.RegularExpressions;
using Microsoft.Win32.SafeHandles;

namespace CreatorBuildTool;

internal static class Paths
{
    public static readonly StringComparer Comparer = StringComparer.OrdinalIgnoreCase;
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)] private static extern SafeFileHandle CreateFileW(string name, uint access, uint share, nint security, uint creation, uint flags, nint template);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)] private static extern uint GetFinalPathNameByHandleW(SafeFileHandle file, StringBuilder path, uint size, uint flags);
    public static string Normal(string path)
    {
        if (string.IsNullOrWhiteSpace(path) || path.StartsWith(@"\\") || path.StartsWith(@"\??\")) throw new BuildException($"The device/extended path namespace and network paths are not supported: {path}");
        // Windows normalizes trailing dots/spaces; reject them before normalization hides the alias.
        foreach (var part in path[Path.GetPathRoot(path)!.Length..].Split(['\\', '/'], StringSplitOptions.RemoveEmptyEntries))
            if (part is not ("." or "..") && (part.EndsWith(' ') || part.EndsWith('.'))) throw new BuildException($"Ambiguous Windows path component: {part}");
        var full = Path.GetFullPath(path);
        if (!Regex.IsMatch(full, @"^[A-Za-z]:\\")) throw new BuildException($"A local DOS path is required: {path}");
        if (full[2..].Contains(':')) throw new BuildException($"Alternate data streams are not allowed: {path}");
        foreach (var part in full[Path.GetPathRoot(full)!.Length..].Split(['\\', '/'], StringSplitOptions.RemoveEmptyEntries))
            if (part.EndsWith(' ') || part.EndsWith('.') || part.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0 ||
                Regex.IsMatch(part, @"^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\.|$)", RegexOptions.IgnoreCase))
                throw new BuildException($"Ambiguous Windows path component: {part}");
        return Path.TrimEndingDirectorySeparator(full);
    }
    public static void NoReparseAncestors(string path)
    {
        var full = Normal(path);
        for (string? cursor = full; cursor != null; cursor = Path.GetDirectoryName(cursor))
        {
            try { if ((File.GetAttributes(cursor) & FileAttributes.ReparsePoint) != 0) throw new BuildException($"Path crosses a symbolic/reparse point: {cursor}"); }
            catch (FileNotFoundException) { } catch (DirectoryNotFoundException) { }
        }
    }
    public static string Canonical(string path, bool mustExist = false)
    {
        var full = Normal(path); NoReparseAncestors(full);
        if (mustExist && !Directory.Exists(full)) throw new BuildException($"Directory missing: {full}");
        var suffix = new Stack<string>(); var probe = full;
        while (!Directory.Exists(probe))
        {
            if (File.Exists(probe)) throw new BuildException($"Not a directory: {probe}");
            suffix.Push(Path.GetFileName(probe)); probe = Path.GetDirectoryName(probe) ?? throw new BuildException($"No existing parent: {full}");
        }
        using var handle = CreateFileW(probe, 0x80, 7, 0, 3, 0x02000000, 0);
        var buffer = new StringBuilder(32768);
        var length = handle.IsInvalid ? 0 : GetFinalPathNameByHandleW(handle, buffer, (uint)buffer.Capacity, 0);
        if (length == 0 || length >= buffer.Capacity) throw new Win32Exception(Marshal.GetLastWin32Error());
        var resolved = buffer.ToString();
        if (resolved.StartsWith(@"\\?\")) resolved = resolved[4..];
        foreach (var part in suffix) resolved = Path.Combine(resolved, part);
        return Normal(resolved);
    }
    public static bool Within(string path, string root) => Normal(path).StartsWith(Normal(root) + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase);
    public static string Child(string root, string relative)
    {
        if (Path.IsPathRooted(relative)) throw new BuildException($"Expected relative path: {relative}");
        var full = Normal(Path.Combine(root, relative)); AssertChild(full, root); return full;
    }
    public static void AssertChild(string path, string root)
    {
        if (!Within(path, root)) throw new BuildException($"Path escapes owned root: {path} ({root})");
        NoReparseAncestors(path);
    }
    public static void Disjoint(string left, string right)
    {
        if (Comparer.Equals(Normal(left), Normal(right)) || Within(left, right) || Within(right, left))
            throw new BuildException($"Input and output trees overlap: {left} / {right}");
    }
    public static IEnumerable<string> Files(string root)
    {
        NoReparseAncestors(root);
        if (!Directory.Exists(root)) throw new BuildException($"Directory missing: {root}");
        var pending = new Stack<string>(); pending.Push(root);
        while (pending.TryPop(out var directory)) foreach (var entry in Directory.EnumerateFileSystemEntries(directory).Order(StringComparer.Ordinal))
        {
            var attributes = File.GetAttributes(entry);
            if ((attributes & FileAttributes.ReparsePoint) != 0) throw new BuildException($"Reparse input is not allowed: {entry}");
            if ((attributes & FileAttributes.Directory) != 0) pending.Push(entry); else yield return entry;
        }
    }
    public static string Relative(string root, string path) => Path.GetRelativePath(root, path).Replace('\\', '/');
    public static void Copy(string source, string destination, bool overwrite = false)
    {
        NoReparseAncestors(source); NoReparseAncestors(destination);
        if (!File.Exists(source)) throw new BuildException($"Required file missing: {source}");
        Directory.CreateDirectory(Path.GetDirectoryName(destination)!); File.Copy(source, destination, overwrite);
    }
    public static void CopyTree(string source, string destination, CancellationToken token = default)
    {
        Disjoint(Canonical(source, true), Canonical(destination));
        foreach (var file in Files(source)) { token.ThrowIfCancellationRequested(); Copy(file, Child(destination, Relative(source, file))); }
    }
    public static void DeleteTree(string path, string owner)
    {
        AssertChild(path, owner);
        if (!Directory.Exists(path)) return;
        _ = Files(path).Count(); // Validate every descendant before a recursive removal.
        Directory.Delete(path, true);
    }
    public static string Repository(string explicitPath = "")
    {
        if (explicitPath.Length > 0) return Canonical(explicitPath, true);
        foreach (var start in new[] { AppContext.BaseDirectory, Environment.CurrentDirectory })
            for (var probe = new DirectoryInfo(start); probe != null; probe = probe.Parent)
                if (File.Exists(Path.Combine(probe.FullName, "engine.manifest.json")) || File.Exists(Path.Combine(probe.FullName, "CreatorEngine.sln"))) return Canonical(probe.FullName, true);
        throw new BuildException("Specify --repository or --engine-distribution.");
    }
}
