using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;

namespace CreatorBuildTool
{
    internal static class AssetSetBuilding
    {
        // Independent content build: does not compile managed scripts, native code, or Player.
        public static async Task BuildAssetSet(BuildContext context)
        {
            var options = context.Options;
            var engine = EngineDistribution.Load(options.Required("engine-distribution"), context);
            var project = Paths.Canonical(options.Required("project"), true);
            var assets = Paths.Canonical(Path.Combine(project, "Assets"), true);
            var definition = Paths.Normal(options.Required("asset-set"));
            var output = Paths.Canonical(options.Required("output"));
            var cache = Paths.Canonical(options.Get("artifact-cache",
                Path.Combine(project, "Library/AssetSetArtifacts")));
            Paths.NoReparseAncestors(definition);
            if (!File.Exists(definition))
            {
                throw new BuildException($"AssetSet source definition missing: {definition}");
            }
            if (Directory.Exists(output) || File.Exists(output))
            {
                throw new BuildException($"AssetSet output must be a new immutable directory: {output}");
            }
            foreach (var owned in new[] { output, cache })
            {
                Paths.Disjoint(owned, assets);
                Paths.Disjoint(owned, engine.Root);
                if (Paths.Within(definition, owned) || Paths.Comparer.Equals(definition, owned))
                {
                    throw new BuildException("AssetSet source definition cannot reside in output or artifact cache.");
                }
            }
            Paths.Disjoint(output, cache);
            var cooker = Paths.Child(engine.BinaryRoot, "Tools/AssetCooker/AssetCooker.exe");
            if (!File.Exists(cooker))
            {
                throw new BuildException("Selected engine distribution has no AssetCooker host.");
            }
            // Includes every verified native importer/decoder dependency and this orchestration
            // implementation. Publication GUIDs and local absolute paths are not build-key inputs.
            var toolFingerprint = Convert.ToHexStringLower(SHA256.HashData(Encoding.UTF8.GetBytes(
                "CreatorBuildTool.BuildAssetSet.v2\n" + engine.Manifest.Text("payloadDigest") + "\n" +
                Metadata.Hash(typeof(AssetSetBuilding).Assembly.Location) + "\n")));
            context.Log($"Building source AssetSet: {definition}", "stage");
            await context.Run(cooker,
                ["--build-asset-set", "--asset-root", assets, "--asset-set", definition,
                 "--output", output, "--artifact-cache", cache, "--tool-fingerprint", toolFingerprint],
                engine.Root);
            context.Cancellation.ThrowIfCancellationRequested();
            var result = ValidateOutput(output);
            context.Log($"AssetSet CEMF3: {result.Assets} assets, {result.Blobs} unique blobs; " +
                "native source/format validation and immutable output readback completed.");
            context.Result(output);
        }

        // The native reader owns CEMF semantics. This checks the native completion receipt
        // and source-free directory boundary without introducing a second CEMF parser.
        internal static (int Assets, int Blobs) ValidateOutput(string output)
        {
            var reportPath = Paths.Child(output, "build-report.txt");
            var keysPath = Paths.Child(output, "build-keys.txt");
            var manifestPath = Paths.Child(output, "Derived/asset-set-manifest.cemf");
            if (!File.Exists(reportPath) || new FileInfo(reportPath).Length > 4096)
            {
                throw new BuildException("AssetSet completion report missing or oversized.");
            }
            var fields = new Dictionary<string, string>(StringComparer.Ordinal);
            foreach (var line in File.ReadAllLines(reportPath))
            {
                var separator = line.IndexOf('=');
                if (separator <= 0 || !fields.TryAdd(line[..separator], line[(separator + 1)..]))
                {
                    throw new BuildException("Invalid or duplicate field in AssetSet completion report.");
                }
            }
            if (fields.Count != 5 || fields.GetValueOrDefault("format") != "CEMF3" ||
                !int.TryParse(fields.GetValueOrDefault("assets"), out var assets) || assets is <= 0 or > 65536 ||
                !int.TryParse(fields.GetValueOrDefault("blobs"), out var blobs) || blobs <= 0 || blobs > assets)
            {
                throw new BuildException("Invalid AssetSet format or completion counts.");
            }
            foreach (var (field, path) in new[] { ("manifestSha256", manifestPath), ("buildKeysSha256", keysPath) })
            {
                if (!File.Exists(path) || !Regex.IsMatch(fields.GetValueOrDefault(field, ""), "^[0-9a-f]{64}$") ||
                    Metadata.Hash(path) != fields[field])
                {
                    throw new BuildException($"AssetSet output verification failed: {field}");
                }
            }
            // This is only the fixed envelope version check, not a legacy-v2 conversion path.
            using (var manifest = File.OpenRead(manifestPath))
            {
                var prefix = new byte[6];
                if (manifest.Read(prefix) != prefix.Length || !prefix.AsSpan(0, 4).SequenceEqual("CEMF"u8) ||
                    prefix[4] != 3 || prefix[5] != 0)
                {
                    throw new BuildException("build-asset-set did not produce a CEMF v3 manifest.");
                }
            }
            var blobCount = 0;
            foreach (var path in Paths.Files(output))
            {
                var relative = Paths.Relative(output, path);
                if (relative is "build-report.txt" or "build-keys.txt" or "Derived/asset-set-manifest.cemf")
                {
                    continue;
                }
                var match = Regex.Match(relative,
                    @"^Derived/AssetBlobs/[0-9a-f]{64}/([0-9a-f]{64})\.(png|jpg|hdr|dds|cemd|cesl|cean|cege|shadermeta|asset|lxmaterial)$");
                if (!match.Success || new FileInfo(path).Length <= 0 || Metadata.Hash(path) != match.Groups[1].Value)
                {
                    throw new BuildException($"Unexpected or damaged AssetSet blob: {relative}");
                }
                ++blobCount;
            }
            if (blobCount != blobs)
            {
                throw new BuildException("AssetSet unique blob count differs from the completion report.");
            }
            return (assets, blobs);
        }
    }
}
