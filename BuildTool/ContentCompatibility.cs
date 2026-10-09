using System.Globalization;
using System.Text.Json.Nodes;
using System.Text.RegularExpressions;

namespace CreatorBuildTool;

// This is an installed-payload check, not a second authority for the native
// content version. The compiled host records supply the expected identity.
internal static class ContentCompatibility
{
    internal static (uint Version, string Token) Read(JsonNode value, string source)
    {
        if (value["contentAbiVersion"] is not JsonValue versionValue ||
            !uint.TryParse(versionValue.ToJsonString(), NumberStyles.None, CultureInfo.InvariantCulture, out var version) || version == 0 ||
            value["contentAbi"] is not JsonValue tokenValue || !tokenValue.TryGetValue<string>(out var token) ||
            token is null || !Regex.IsMatch(token, @"\A[A-Za-z0-9_.-]{1,128}\z"))
        {
            throw new BuildException($"Missing or invalid content ABI in {source}. Rebuild and publish an engine with AssetDepot support before using AssetSets/bootstrap.");
        }
        return (version, token);
    }

    internal static string RequireInstalled(EngineDistribution engine, BuildContext context)
    {
        var manifest = engine.Manifest;
        var expected = Read(manifest, "engine.manifest.json");
        if (manifest["hosts"] is not JsonObject hosts)
        {
            throw new BuildException("AssetSets/bootstrap require verified installed host identities. Rebuild and publish the engine.");
        }
        foreach (var name in new[] { "CreatorEditor", "Player", "AssetCooker", "AssetPacker" })
        {
            if (hosts[name] is not JsonObject abi || Read(abi, $"installed {name} identity") != expected)
            {
                throw new BuildException($"Installed {name} content ABI differs from the engine manifest. Rebuild and publish the engine.");
            }
            if (abi.Text("version") != manifest.Text("version") || abi.Text("productName") != manifest.Text("productName") ||
                abi.Text("featureRelease") != manifest.Text("featureRelease") || abi.Bool("localDevelopment") != manifest.Bool("localDevelopment") ||
                abi.Int("hostAbi") != manifest.Int("hostAbi") || abi.Int("pointerBits") != 64 ||
                abi.Int("scriptApi") != manifest.Int("scriptApi") ||
                abi.Int("debug") != (engine.Configuration == "Debug" ? 1 : 0) ||
                abi.Int("shipping") != (name == "Player" && manifest.Bool("shipping") ? 1 : 0))
            {
                throw new BuildException($"Installed {name} identity differs from the engine manifest.");
            }
        }

        var files = Metadata.ParseEntries(manifest.Array("files"));
        if (Metadata.Digest(files) != manifest.Text("payloadDigest"))
        {
            throw new BuildException("Engine payload digest mismatch while checking content ABI.");
        }
        var installed = files.ToDictionary(entry => entry.Path, Paths.Comparer);
        foreach (var name in new[] { "Player", "AssetCooker" })
        {
            context.Cancellation.ThrowIfCancellationRequested();
            var recordPath = Paths.Child(engine.BinaryRoot, $"Runtime/Manifests/{name}.json");
            if (!installed.TryGetValue(Paths.Relative(engine.Root, recordPath), out var recordEntry))
            {
                throw new BuildException($"Content ABI provenance is missing the installed {name} build record. Rebuild and publish the engine.");
            }
            // Revalidate the record before reading its ABI or entry list. A loose
            // sidecar outside the distribution's hashed inventory is not evidence.
            Metadata.Verify(engine.Root, [recordEntry], context.Cancellation);
            var record = Metadata.Read(recordPath);
            var entries = Metadata.ParseEntries(record.Array("entries"));
            if (record.Int("schemaVersion") != 1 || record.Text("host") != name ||
                record.Text("configuration") != engine.Configuration || Metadata.Digest(entries) != record.Text("digest") ||
                record["abi"] is not JsonObject abi || !JsonNode.DeepEquals(abi, hosts[name]) ||
                Read(abi, $"installed {name} build record") != expected)
            {
                throw new BuildException($"Installed {name} build record differs from the engine/content identity.");
            }
            var prefix = name == "Player" ? "Player/" : "Tools/AssetCooker/";
            var entryPaths = entries.Select(entry => entry.Path).ToHashSet(StringComparer.Ordinal);
            foreach (var required in new[] { prefix + name + ".exe", prefix + name + ".runtime.dll", "Runtime/layout.version" })
            {
                if (!entryPaths.Contains(required))
                {
                    throw new BuildException($"Content ABI build record is incomplete: {name}, {required}");
                }
            }
            foreach (var entry in entries)
            {
                var relative = Paths.Relative(engine.Root, Paths.Child(engine.BinaryRoot, entry.Path));
                if (!installed.TryGetValue(relative, out var payload) || payload.Bytes != entry.Bytes || payload.Sha256 != entry.Sha256)
                {
                    throw new BuildException($"Content ABI build record is not bound to the installed payload: {name}, {entry.Path}");
                }
            }
            Metadata.Verify(engine.BinaryRoot, entries, context.Cancellation);
        }
        return expected.Token;
    }
}
