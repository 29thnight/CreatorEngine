using System.Text;
using System.Text.RegularExpressions;

namespace CreatorBuildTool
{
    internal static class AssetSetPackaging
    {
        // Attach independently built immutable outputs. No model import, texture
        // decode, managed compilation or native Player build occurs in this step.
        internal static string[] CopyConfiguredSets(BuildContext context, string runtimeAssets)
        {
            var list = context.Options.Get("asset-set-list");
            var abi = context.Options.Get("asset-set-abi");
            if (list.Length == 0)
            {
                if (abi.Length != 0)
                {
                    throw new BuildException("--asset-set-abi requires --asset-set-list.");
                }
                return [];
            }
            if (!Regex.IsMatch(abi, "^[A-Za-z0-9_.-]{1,128}$"))
            {
                throw new BuildException("Prebuilt AssetSets require an explicit --asset-set-abi host compatibility token.");
            }
            list = Paths.Normal(list);
            var listParent = Paths.Canonical(Path.GetDirectoryName(list)!, true);
            list = Paths.Child(listParent, Path.GetFileName(list));
            Paths.NoReparseAncestors(list);
            string listText;
            using (var input = new FileStream(list, FileMode.Open, FileAccess.Read, FileShare.Read))
            {
                if (input.Length is <= 0 or > 65536)
                {
                    throw new BuildException("AssetSet directory list is empty or oversized.");
                }
                var listBytes = new byte[checked((int)input.Length)];
                input.ReadExactly(listBytes);
                if (input.ReadByte() != -1)
                {
                    throw new BuildException("AssetSet directory list changed while reading.");
                }
                listText = new UTF8Encoding(false, true).GetString(listBytes).TrimStart('\uFEFF');
            }
            var roots = listText.Split('\n').Select(line => line.Trim()).Where(line => line.Length != 0).ToArray();
            if (roots.Length is < 1 or > 64)
            {
                throw new BuildException("AssetSet directory list requires 1..64 immutable outputs.");
            }
            var hashes = new SortedSet<string>(StringComparer.Ordinal);
            foreach (var entry in roots)
            {
                context.Cancellation.ThrowIfCancellationRequested();
                var root = Paths.Canonical(Path.Combine(Path.GetDirectoryName(list)!, entry), true);
                Paths.Disjoint(root, runtimeAssets);
                _ = AssetSetBuilding.ValidateOutput(root);
                var hash = Metadata.Hash(Paths.Child(root, "Derived/asset-set-manifest.cemf"));
                if (!hashes.Add(hash))
                {
                    throw new BuildException("Repeated AssetSet manifest in package activation list: " + hash);
                }
                var destination = Paths.Child(runtimeAssets, "AssetSets/" + hash);
                if (Directory.Exists(destination) || File.Exists(destination))
                {
                    throw new BuildException("Package already contains this AssetSet directory: " + hash);
                }
                // A guard is a sibling OS identity record, not portable content.
                // Only this output directory is copied. Extracted package roots
                // are explicitly unmanaged/noncollectible until freshly enrolled.
                Paths.CopyTree(root, destination, context.Cancellation);
                _ = AssetSetBuilding.ValidateOutput(destination);
                if (Metadata.Hash(Paths.Child(destination, "Derived/asset-set-manifest.cemf")) != hash)
                {
                    throw new BuildException("AssetSet manifest changed while packaging: " + root);
                }
            }
            var activation = Paths.Child(runtimeAssets, "Derived/asset-set-activation.ceas");
            if (File.Exists(activation))
            {
                throw new BuildException("Package source already supplies an AssetSet activation policy.");
            }
            Directory.CreateDirectory(Path.GetDirectoryName(activation)!);
            File.WriteAllText(activation, "CEAS1\nwin-x64\n" + abi + "\n" + string.Join("\n", hashes) + "\n",
                new UTF8Encoding(false));
            return hashes.ToArray();
        }
    }
}
