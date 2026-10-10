using System.Diagnostics;
using System.Text.Json.Nodes;
using CreatorBuildTool;

if (args.FirstOrDefault() == "--child") { await Task.Delay(60000); return 0; }
if (args.FirstOrDefault() == "--parent")
{
    using var child = Process.Start(new ProcessStartInfo(Environment.ProcessPath!) { ArgumentList = { "--child" }, UseShellExecute = false, CreateNoWindow = true })!;
    File.WriteAllText(args[1], child.Id.ToString()); await Task.Delay(60000); return 0;
}

var root = Path.Combine(Path.GetTempPath(), "CreatorBuildToolTests-" + Guid.NewGuid().ToString("N"));
Directory.CreateDirectory(root);
var checks = 0;
var succeeded = false;
void Check(bool value, string message) { if (!value) throw new Exception(message); ++checks; }
void Reject(Action action, string message)
{
    var rejected = false; try { action(); } catch (Exception) { rejected = true; } Check(rejected, message);
}
async Task RejectAsync(Func<Task> action, string message)
{
    var rejected = false; try { await action(); } catch (Exception) { rejected = true; } Check(rejected, message);
}
using var context = new BuildContext(new Options(["help"]), CancellationToken.None);
try
{
    foreach (var retired in new[] { "Runtime/Common/fmod.dll", "Runtime/Common/FMODL.DLL", "fmodstudio.dll", "fmodstudioL.dll", "miniaudio.dll" })
    {
        Reject(() => Metadata.AssertSourceOnlyAudio([retired]), "Retired audio dependency accepted: " + retired);
    }
    Metadata.AssertSourceOnlyAudio(["Runtime/Common/normal.dll", "Licenses/ThirdParty/miniaudio/LICENSE"]);
    ++checks;
    var runtimeRoot = Path.Combine(root, "font-runtime");
    var runtimeFonts = Paths.Child(runtimeRoot, RuntimeFonts.RelativeRoot);
    Directory.CreateDirectory(runtimeFonts);
    foreach (var required in RuntimeFonts.RequiredFiles)
    {
        File.WriteAllText(Paths.Child(runtimeFonts, required), "fixture");
    }
    Check(RuntimeFonts.Require(runtimeRoot) == runtimeFonts, "Complete runtime fonts rejected");
    foreach (var required in RuntimeFonts.RequiredFiles)
    {
        var resource = Paths.Child(runtimeFonts, required);
        File.Delete(resource);
        Reject(() => RuntimeFonts.Require(runtimeRoot), "Missing font/license/provenance accepted: " + required);
        File.WriteAllText(resource, "");
        Reject(() => RuntimeFonts.Require(runtimeRoot), "Empty font/license/provenance accepted: " + required);
        File.WriteAllText(resource, "fixture");
    }
    // Unrun source fixture for independent AssetSet packaging. Native CEMF
    // semantic/closure validation is covered by asset_set_activation_probe.cpp.
    var assetSet = Path.Combine(root, "standalone-set");
    var setManifest = Paths.Child(assetSet, "Derived/asset-set-manifest.cemf");
    Directory.CreateDirectory(Path.GetDirectoryName(setManifest)!);
    File.WriteAllBytes(setManifest, [67, 69, 77, 70, 3, 0]);
    var setKeys = Paths.Child(assetSet, "build-keys.txt");
    File.WriteAllText(setKeys, "fixture");
    var blobBytes = new byte[] { 12, 34, 56 };
    var blobHash = Convert.ToHexStringLower(System.Security.Cryptography.SHA256.HashData(blobBytes));
    var setBlob = Paths.Child(assetSet, "Derived/AssetBlobs/" + new string('0', 64) + "/" + blobHash + ".cetex");
    Directory.CreateDirectory(Path.GetDirectoryName(setBlob)!);
    File.WriteAllBytes(setBlob, blobBytes);
    File.WriteAllText(Paths.Child(assetSet, "build-report.txt"), "format=CEMF3\nassets=1\nblobs=1\nmanifestSha256="
        + Metadata.Hash(setManifest) + "\nbuildKeysSha256=" + Metadata.Hash(setKeys) + "\n");
    Check(AssetSetBuilding.ValidateOutput(assetSet) == (1, 1), "Cooked texture CAS envelope rejected");
    var rawSetBlob = Path.ChangeExtension(setBlob, ".png");
    File.Move(setBlob, rawSetBlob);
    try
    {
        Reject(() => AssetSetBuilding.ValidateOutput(assetSet), "Raw PNG CAS blob accepted");
    }
    finally
    {
        File.Move(rawSetBlob, setBlob);
    }
    var setList = Path.Combine(root, "sets.txt");
    File.WriteAllText(setList, "standalone-set\n");
    var packagedAssets = Path.Combine(root, "asset-set-package/Assets");
    using (var setContext = new BuildContext(new Options(["package-game", "--asset-set-list", setList,
        "--asset-set-abi", "fixture-v1"]), CancellationToken.None))
    {
        // Unit seam only: production invokes the native source-lease copy mode.
        Task CopyFixture(string source, string destination)
        {
            Paths.CopyTree(source, destination);
            return Task.CompletedTask;
        }
        var hashes = await AssetSetPackaging.CopyConfiguredSets(setContext, packagedAssets, CopyFixture, "fixture-v1");
        Check(hashes.SequenceEqual(new[] { Metadata.Hash(setManifest) }), "Package changed the immutable manifest identity");
        var activationText = File.ReadAllText(Paths.Child(packagedAssets, "Derived/asset-set-activation.ceas"));
        Check(activationText == "CEAS1\nwin-x64\nfixture-v1\n" + hashes[0] + "\n", "Activation policy was not canonical");
        Check(Metadata.Hash(Paths.Child(packagedAssets, "AssetSets/" + hashes[0] + "/Derived/asset-set-manifest.cemf"))
            == hashes[0], "Copied manifest is not the validated content");
        await RejectAsync(() => AssetSetPackaging.CopyConfiguredSets(setContext, packagedAssets, CopyFixture, "fixture-v1"), "Existing package activation was replaced");
        var interruptedDestination = Path.Combine(root, "interrupted-set-package/Assets");
        await RejectAsync(() => AssetSetPackaging.CopyConfiguredSets(setContext, interruptedDestination,
            (source, destination) =>
            {
                Directory.CreateDirectory(destination);
                File.WriteAllText(Path.Combine(destination, "partial"), "incomplete");
                return Task.FromException(new IOException("Injected copy interruption"));
            }, "fixture-v1"), "Interrupted copy was accepted");
        Check(!File.Exists(Paths.Child(interruptedDestination, "Derived/asset-set-activation.ceas")),
            "Interrupted copy published activation policy");
        var corruptDestination = Path.Combine(root, "corrupt-set-package/Assets");
        await RejectAsync(() => AssetSetPackaging.CopyConfiguredSets(setContext, corruptDestination,
            async (source, destination) =>
            {
                await CopyFixture(source, destination);
                File.WriteAllBytes(Paths.Child(destination, Paths.Relative(source, setBlob)), [99]);
            }, "fixture-v1"), "Corruption during copying was accepted");
        Check(!File.Exists(Paths.Child(corruptDestination, "Derived/asset-set-activation.ceas")),
            "Post-copy validation failure published activation policy");
        File.WriteAllBytes(setBlob, [99]);
        await RejectAsync(() => AssetSetPackaging.CopyConfiguredSets(setContext, Path.Combine(root, "damaged-set-package/Assets"), CopyFixture, "fixture-v1"),
            "Damaged source CAS blob was packaged");
    }

    // Unrun source fixtures: content mode rejects accidental cook/override paths.
    Check(RuntimeBootstrap.ContentMode(new Options(["package-game"])) == "Legacy", "Legacy default changed");
    var prebuiltOptions = new[] { "package-game", "--content-mode", "PrebuiltAssetSets", "--bootstrap-root", "bootstrap",
        "--asset-set-list", "sets.txt", "--game-scripts-assembly", "GameScripts.dll" };
    Check(RuntimeBootstrap.ContentMode(new Options(prebuiltOptions)) == "PrebuiltAssetSets", "Prebuilt content mode rejected");
    foreach (var forbidden in new[] { new[] { "--input-mode", "Tracked" }, new[] { "--startup-scene", "Other.creator" },
        new[] { "--render-backend", "vulkan" }, new[] { "--asset-list", "sources.txt" }, new[] { "--build-native" } })
        Reject(() => RuntimeBootstrap.ContentMode(new Options(prebuiltOptions.Concat(forbidden).ToArray())), "Prebuilt content accepted a source/override option");
    Reject(() => RuntimeBootstrap.ContentMode(new Options(["package-game", "--content-mode", "PrebuiltAssetSets"])), "Incomplete prebuilt mode accepted");
    Reject(() => RuntimeBootstrap.ContentMode(new Options(["package-game", "--bootstrap-root", "bootstrap"])), "Legacy mode accepted bootstrap");
    foreach (var path in new[] { "Assets/Models/model.glb", "Assets/Textures/image.png", "Assets/Materials/cloth.asset",
        "Assets/Materials/cloth.shadergraph", "Assets/Audio/raw.wav", "Assets/Scenes/Start.creator.meta", "Assets/Derived/Models/model.cemc",
        "Assets/AssetSets/extra/Derived/asset-set-manifest.cemf" })
        Reject(() => RuntimeBootstrap.ValidateBoundary([new(path, 1, new string('0', 64))]), "Bootstrap source boundary accepted " + path);
    RuntimeBootstrap.ValidateBoundary([new("Assets/Scenes/Start.creator", 1, new string('0', 64)),
        new("ProjectSetting/Layers.celayers", 1, new string('0', 64)),
        new("Assets/Derived/bootstrap-asset-references.cebr", 1, new string('0', 64))]); ++checks;

    var textureCook = Path.Combine(root, "texture-cook");
    var textureDerived = Path.Combine(textureCook, "Derived");
    var modelTextures = Path.Combine(textureDerived, "Models/11/11111111-1111-4111-8111-111111111111/1/textures");
    Directory.CreateDirectory(modelTextures);
    File.WriteAllText(Path.Combine(textureDerived, "asset-manifest.cemf"), "fixture");

    foreach (var extension in new[] { "cetex" })
    {
        var texture = Path.Combine(modelTextures, "22222222-2222-8222-8222-222222222222." + extension);
        File.WriteAllText(texture, "payload");
        Check(AssetCooking.Validate(textureCook, 1).ArtifactCount == 1, "Model texture extension rejected: " + extension);
        File.Delete(texture);
    }

    foreach (var relative in new[] { "Models/11/11111111-1111-4111-8111-111111111111/1/textures/name.cetex",
        "Models/ff/11111111-1111-4111-8111-111111111111/1/textures/22222222-2222-8222-8222-222222222222.cetex",
        "Models/11/11111111-1111-4111-8111-111111111111/1/textures/22222222-2222-8222-8222-222222222222.png",
        "Models/11/11111111-1111-4111-8111-111111111111/1/textures/22222222-2222-8222-8222-222222222222.jpg",
        "Models/11/11111111-1111-4111-8111-111111111111/1/textures/22222222-2222-8222-8222-222222222222.exe" })
    {
        var texture = Paths.Child(textureDerived, relative);
        Directory.CreateDirectory(Path.GetDirectoryName(texture)!);
        File.WriteAllText(texture, "payload");
        Reject(() => AssetCooking.Validate(textureCook, 1), "Invalid model texture path accepted: " + relative);
        File.Delete(texture);
    }

    Reject(() => new Options(["compile-game", "--project", "x", "--project", "y"]), "Duplicate CLI option accepted");
    Reject(() => new Options(["package-game", "--skpi-verify"]), "Unknown CLI option accepted");
    foreach (var value in new[] { @"\\?\C:\test", @"\\server\share", @"C:\test\a:stream", @"C:\test\CON.txt", @"C:\test\trailing.", @"C:\test\trailing " })
        Reject(() => Paths.Normal(value), "Ambiguous path accepted: " + value);
    Reject(() => Paths.Child(root, "../outside"), "Traversal accepted");
    Reject(() => Paths.DeleteTree(root, root), "Owner root removal accepted");
    Reject(() => Paths.Disjoint(root, Path.Combine(root, "Assets")), "Overlapping trees accepted");
    var junctionTarget = Path.Combine(root, "outside-target"); var link = Path.Combine(root, "symbolic"); Directory.CreateDirectory(junctionTarget);
    Directory.CreateSymbolicLink(link, junctionTarget);
    Reject(() => Paths.Canonical(Path.Combine(link, "output")), "Symlink output accepted");
    Reject(() => Paths.Files(root).ToArray(), "Recursive traversal followed symlink"); Directory.Delete(link);

    var engineRoot = Path.Combine(root, "engine"); Directory.CreateDirectory(engineRoot);
    File.WriteAllText(Path.Combine(engineRoot, "payload.txt"), "original payload");
    var files = Metadata.Entries(engineRoot);
    var version = Metadata.Object(new { schemaVersion = 1, productName = "CreatorEngine 2", featureRelease = "", version = "0.0.0.0", channel = "preview", localDevelopment = true });
    Metadata.ValidateVersion(version);
    foreach (var invalid in new[] { "1.2.3", "1.2.65536.0", "01.2.3.4", "1.2.3.4-preview" })
    { var bad = version.DeepClone(); bad["version"] = invalid; Reject(() => Metadata.ValidateVersion(bad), "Invalid version accepted"); }
    var manifest = Metadata.Object(new { schemaVersion = 1, productName = "CreatorEngine 2", featureRelease = "", version = "0.0.0.0", channel = "preview", localDevelopment = true,
        buildId = Guid.NewGuid().ToString("D"), payloadDigest = Metadata.Digest(files), platform = "win-x64", configuration = "Debug", shipping = false,
        binaryRoot = "Bin/x64-Debug", scriptApi = 24, hostAbi = 1, files });
    Metadata.Write(Path.Combine(engineRoot, "engine.manifest.json"), manifest); File.WriteAllText(Path.Combine(engineRoot, "engine.info"), Metadata.Info(manifest));
    var engine = EngineDistribution.Load(engineRoot, context); Check(engine.Configuration == "Debug", "Valid engine rejected");
    File.WriteAllText(Path.Combine(engineRoot, "extra.dll"), "extra"); Reject(() => EngineDistribution.Load(engineRoot, context), "Unlisted DLL accepted"); File.Delete(Path.Combine(engineRoot, "extra.dll"));
    File.WriteAllText(Path.Combine(engineRoot, "payload.txt"), "tampered payload"); Reject(() => EngineDistribution.Load(engineRoot, context), "Modified payload accepted");
    File.WriteAllText(Path.Combine(engineRoot, "payload.txt"), "original payload");
    Reject(() => Metadata.Digest([files[0], files[0] with { Path = files[0].Path.ToUpperInvariant() }]), "Case-duplicate payload accepted");
    // Unrun source fixture: prebuilt-copy checks pinned source and receipt bytes,
    // never recompiles, and never generates a receipt for unapproved replacement bytes.
    var prebuiltRoot = Path.Combine(root, "prebuilt-copy"); Directory.CreateDirectory(prebuiltRoot);
    var prebuiltSource = Path.Combine(prebuiltRoot, "GameScripts.dll");
    File.WriteAllBytes(prebuiltSource, [1, 2, 3, 4]);
    var approvedAssemblyHash = Metadata.Hash(prebuiltSource);
    var prebuiltReceipt = prebuiltSource + ".engine.json";
    Metadata.Write(prebuiltReceipt, new { schemaVersion = 1, engineBuildId = manifest.Text("buildId"),
        scriptApi = manifest.Int("scriptApi"), sha256 = approvedAssemblyHash });
    var approvedReceiptBytes = File.ReadAllBytes(prebuiltReceipt);
    var copiedAssembly = Path.Combine(prebuiltRoot, "Copied.dll");
    Check(GameCompiler.CopyPrebuiltAssembly(engine, prebuiltSource, copiedAssembly) == approvedAssemblyHash
        && Metadata.Hash(copiedAssembly) == approvedAssemblyHash, "Prebuilt copy did not preserve the captured expected hash");
    var rejectedAssembly = Path.Combine(prebuiltRoot, "Rejected.dll");
    using (var writer = new FileStream(prebuiltSource, FileMode.Open, FileAccess.ReadWrite, FileShare.ReadWrite))
        Reject(() => GameCompiler.CopyPrebuiltAssembly(engine, prebuiltSource, rejectedAssembly), "Prebuilt source was copied with an outstanding writable handle");
    using (var writer = new FileStream(prebuiltReceipt, FileMode.Open, FileAccess.ReadWrite, FileShare.ReadWrite))
        Reject(() => GameCompiler.CopyPrebuiltAssembly(engine, prebuiltSource, rejectedAssembly), "Prebuilt receipt was accepted with an outstanding writable handle");
    File.WriteAllBytes(prebuiltSource, [5, 6, 7, 8]);
    Reject(() => GameCompiler.CopyPrebuiltAssembly(engine, prebuiltSource, rejectedAssembly), "Replacement assembly bytes were blessed by a fresh receipt");
    Check(!File.Exists(rejectedAssembly) && Metadata.Hash(copiedAssembly) == approvedAssemblyHash, "Rejected prebuilt copy altered published output");
    File.WriteAllBytes(prebuiltSource, [1, 2, 3, 4]);
    File.WriteAllBytes(prebuiltReceipt, new byte[16385]);
    Reject(() => GameCompiler.CopyPrebuiltAssembly(engine, prebuiltSource, rejectedAssembly), "Oversized prebuilt receipt accepted");
    File.WriteAllBytes(prebuiltReceipt, approvedReceiptBytes);

    var project = Path.Combine(root, "game 한글 & spaces"); Directory.CreateDirectory(Path.Combine(project, "ProjectSetting")); Directory.CreateDirectory(Path.Combine(project, "Assets/Script"));
    File.WriteAllText(Path.Combine(project, "Assets/Script/Example.cs"), "public class Example {}");
    File.WriteAllText(Path.Combine(project, "Assets/Script/Example.cs.meta"), "guid: 11111111-1111-4111-8111-111111111111");
    File.WriteAllText(Path.Combine(project, "Assets/Shader.hlsl"), "shader");
    File.WriteAllText(Path.Combine(project, "Assets/Shader.hlsl.meta"), "guid: 22222222-2222-4222-8222-222222222222");
    File.Copy(Path.Combine(Directory.GetCurrentDirectory(), "Dynamic_CPP", ProjectLayerAsset.RelativePath), Paths.Child(project, ProjectLayerAsset.RelativePath));
    Reject(() => engine.RequireContentAbi(context), "Legacy distribution silently asserted content ABI support");
    foreach (var relative in new[] { "Scenes/Bootstrap.creator", "Scenes/Bootstrap.creator.meta", "Prefabs/Item.prefab", "Prefabs/Item.prefab.meta",
        "Models/Robot.glb", "Models/Robot.glb.meta", "Textures/cloth.png", "Materials/cloth.asset", "Materials/cloth.shadergraph",
        "Audio/voice.wav", "Audio/voice.wav.meta", "Shaders/DefaultPassShader/WorldSprite.slang" })
    {
        var file = Paths.Child(project, "Assets/" + relative); Directory.CreateDirectory(Path.GetDirectoryName(file)!); File.WriteAllText(file, "fixture");
    }
    var bootstrapInput = Path.Combine(root, "bootstrap-input");
    _ = PackageInputs.CopyBootstrapProject(project, bootstrapInput, CancellationToken.None);
    foreach (var retained in new[] { "Scenes/Bootstrap.creator", "Scenes/Bootstrap.creator.meta", "Prefabs/Item.prefab", "Audio/voice.wav", "Shaders/DefaultPassShader/WorldSprite.slang" })
        Check(File.Exists(Paths.Child(bootstrapInput, "Assets/" + retained)), "Bootstrap omitted document input " + retained);
    foreach (var omitted in new[] { "Models/Robot.glb", "Models/Robot.glb.meta", "Textures/cloth.png", "Materials/cloth.asset", "Materials/cloth.shadergraph", "Script/Example.cs" })
        Check(!File.Exists(Paths.Child(bootstrapInput, "Assets/" + omitted)), "Bootstrap staged an asset importer input " + omitted);
    Reject(() => PackageInputs.CopyBootstrapProject(project, Path.Combine(root, "bad-bootstrap"), CancellationToken.None,
        new HashSet<string>(["Models/Robot.glb"], Paths.Comparer)), "Explicit bootstrap model source was silently included");

    var cookInput = Path.Combine(root, "cook-input"); PackageInputs.CopyProject(project, cookInput, CancellationToken.None);
    var geometrySource = Paths.Child(cookInput, "Assets/Geometry/shape.cegeometry");
    var geometryHistory = Paths.Child(cookInput, "Assets/Derived/CollisionGeometry/id/1.cegeometry");
    var geometryArtifact = Paths.Child(cookInput, "Assets/Derived/CollisionGeometry/ab/asset.cepg");
    foreach (var path in new[] { geometrySource, geometryHistory, geometryArtifact }) { Directory.CreateDirectory(Path.GetDirectoryName(path)!); File.WriteAllText(path, "payload"); }
    File.WriteAllText(geometrySource + ".meta", "identity");
    PackageInputs.RemoveGeometrySources(cookInput);
    Check(!File.Exists(geometrySource) && !File.Exists(geometrySource + ".meta") && !File.Exists(geometryHistory), "Geometry source/history leaked into runtime package");
    Check(File.Exists(geometryArtifact) && File.Exists(Path.Combine(project, "Assets/Shader.hlsl")), "Cooked geometry or original project altered by source cleanup");
    foreach (var extension in new[] { "wav", "mp3", "flac", "ogg", "soundgraph", "soundpreset" })
    {
        var source = Paths.Child(cookInput, "Assets/Audio/source." + extension);
        Directory.CreateDirectory(Path.GetDirectoryName(source)!);
        File.WriteAllText(source, "authoring");
        File.WriteAllText(source + ".meta", "identity");
        Check(PackageInputs.Excluded(source), "Raw audio source not excluded: " + extension);
    }
    var cookedAudio = new[] { "Audio/ce/test.ceac", "SoundGraphs/ce/test.cesg", "SoundPresets/ce/test.cesp" };
    foreach (var relative in cookedAudio)
    {
        var artifact = Paths.Child(cookInput, "Assets/Derived/" + relative);
        Directory.CreateDirectory(Path.GetDirectoryName(artifact)!);
        File.WriteAllText(artifact, "cooked");
        Check(!PackageInputs.Excluded(artifact), "Cooked audio artifact was excluded: " + relative);
    }
    PackageInputs.RemoveAudioSources(cookInput);
    Check(!Directory.EnumerateFiles(Paths.Child(cookInput, "Assets/Audio")).Any(), "Audio source or meta leaked into package");
    Check(cookedAudio.All(relative => File.Exists(Paths.Child(cookInput, "Assets/Derived/" + relative))), "Audio cleanup removed cooked artifacts");
    // Source-only InputGraph fixture: current LX and retired maps are excluded,
    // while CEIG payloads survive. Uppercase exercises the shared case policy.
    foreach (var extension in new[] { "inputgraph", "INPUTGRAPH", "inputmap", "INPUTMAP" })
    {
        var source = Paths.Child(cookInput, "Assets/InputGraph/source." + extension);
        Directory.CreateDirectory(Path.GetDirectoryName(source)!);
        File.WriteAllText(source, "input authoring fixture");
        Check(PackageInputs.Excluded(source), "Input authoring source not excluded: " + extension);
        Check(PackageInputs.Excluded(source + ".meta"), "Input authoring sidecar exclusion changed");
    }
    foreach (var extension in new[] { "ceig", "CEIG" })
    {
        var artifact = Paths.Child(cookInput, "Assets/Derived/InputGraph/ce/probe." + extension);
        Directory.CreateDirectory(Path.GetDirectoryName(artifact)!);
        File.WriteAllText(artifact, "synthetic cooked InputGraph payload");
        Check(!PackageInputs.Excluded(artifact), "Cooked InputGraph artifact was excluded: " + extension);
    }
    Check(!Directory.Exists(Path.Combine(cookInput, "Assets/Script")), "C# source identities leaked into native cook input");
    foreach (var relative in new[] { "Fonts/Latin.ttf", "Fonts/Korean.otf", "Fonts/LICENSE.txt" })
    {
        var source = Paths.Child(project, "Assets/" + relative);
        Directory.CreateDirectory(Path.GetDirectoryName(source)!);
        File.WriteAllText(source, "font or redistribution notice");
        Check(!PackageInputs.Excluded(source), "Font/license excluded from Assets payload: " + relative);
    }
    var fontInput = Path.Combine(root, "font-input");
    PackageInputs.CopyProject(project, fontInput, CancellationToken.None);
    foreach (var relative in new[] { "Fonts/Latin.ttf", "Fonts/Korean.otf", "Fonts/LICENSE.txt" })
    {
        Check(File.Exists(Paths.Child(fontInput, "Assets/" + relative)), "Font/license omitted from project copy: " + relative);
    }
    Check(File.Exists(Path.Combine(cookInput, "Assets/Shader.hlsl.meta")) && File.Exists(Path.Combine(cookInput, "Assets/Shader.hlsl")), "Runtime source identity was removed from cook input");
    Check(!PackageInputs.Excluded(Path.Combine(cookInput, "Assets/Derived/Models/ab/id/1/sidecar.meta")), "Cooked model sidecar was excluded from the package");
    Check(PackageInputs.Excluded(Path.Combine(cookInput, "Assets/Models/Robot.glb.meta")), "Authoring model sidecar entered the package");
    Reject(() => engine.AssertProject(project), "Missing project pin accepted"); engine.SelectProject(project); engine.AssertProject(project); ++checks;
    var wrong = manifest.DeepClone(); wrong["buildId"] = Guid.NewGuid().ToString("D"); Reject(() => new EngineDistribution(engineRoot, wrong.AsObject()).AssertProject(project), "Wrong project pin accepted");
    GamePackager.ValidatePack("[PAK-ENTRY] payload.txt\n", files); ++checks;
    Reject(() => GamePackager.ValidatePack("[PAK-ENTRY] wrong.txt\n", files), "Mismatched PAK listing accepted");
    Reject(() => GamePackager.ValidatePack("[PAK-ENTRY] payload.txt\n[PAK-ENTRY] payload.txt\n", files), "Duplicate PAK entry accepted");

    var shaderClosure = Path.Combine(root, "shader-closure");
    Directory.CreateDirectory(Path.Combine(shaderClosure, "Assets/Scenes"));
    Directory.CreateDirectory(Path.Combine(shaderClosure, "Assets/Shaders/DefaultPassShader"));
    File.WriteAllText(Path.Combine(shaderClosure, "Assets/Scenes/Demo.creator"), "m_Entities: []\n");
    var closureSettings = Path.Combine(shaderClosure, "runtime.asset");
    File.WriteAllText(closureSettings, "lastWindowSize:\n  x: 1280\n  y: 720\nrenderPassSettings:\nstartupSceneName: Demo.creator\nrender:\n  backend: dx12\nbuild:\n  render:\n    backend: dx12\n");
    var spriteShader = Path.Combine(shaderClosure, "Assets/Shaders/DefaultPassShader/WorldSprite.slang");
    File.WriteAllText(spriteShader, "// current renderer shader");
    var playerShader = Path.Combine(shaderClosure, "Assets/Shaders/DefaultPassShader/PlayerPresentation.slang");
    Reject(() => PackageInputs.Validate(shaderClosure, closureSettings), "Missing Player presentation shader accepted");
    File.WriteAllText(playerShader, "// native Player presentation shader");
    Check(PackageInputs.Validate(shaderClosure, closureSettings).StartupScene == "Demo.creator", "Slang shader closure rejected");
    File.Move(spriteShader, Path.ChangeExtension(spriteShader, ".hlsl"));
    Reject(() => PackageInputs.Validate(shaderClosure, closureSettings), "Legacy-only shader closure accepted");

    var stage = Path.Combine(root, "stage"); Directory.CreateDirectory(stage); var pointer = Path.Combine(stage, "game.current.json");
    Metadata.Write(pointer, new { releaseDirectory = "old", verification = "passed" }); var pointerHash = Metadata.Hash(pointer);
    var candidate = Path.Combine(stage, "candidate"); var release = Path.Combine(stage, "release"); Directory.CreateDirectory(candidate); Directory.CreateDirectory(release);
    await RejectAsync(() => GamePackager.Publish(context, candidate, release, pointer, stage, new { releaseDirectory = "new" }), "Existing release overwritten");
    Check(Metadata.Hash(pointer) == pointerHash && Directory.Exists(candidate), "Failed publish altered current/candidate"); Directory.Delete(release);
    using var cancelled = new CancellationTokenSource(); cancelled.Cancel(); using var cancelContext = new BuildContext(new Options(["help"]), cancelled.Token);
    await RejectAsync(() => GamePackager.Publish(cancelContext, candidate, release, pointer, stage, new { releaseDirectory = "new" }), "Cancelled package published");
    Check(Metadata.Hash(pointer) == pointerHash, "Cancellation altered current pointer");
    await GamePackager.Publish(context, candidate, release, pointer, stage, new { releaseDirectory = "release", verification = "passed" });
    Check(Directory.Exists(release) && !Directory.Exists(candidate) && Metadata.Read(pointer).Text("releaseDirectory") == "release", "Atomic package publish failed");

    const string smoke = "[asset.catalog] source=cemf identities=1 metaParsed=0\n[cooked.catalog] mount test entries=2 sources=1 stale=0\n" +
        "[scene.document] source=cooked guid=11111111-1111-4111-8111-111111111111\n[player.service] compiled=yes enabled=no\n" +
        "[runtime.text-parser] calls=0\nScene loaded: Demo.creator\n[player.smoke] {\"schemaVersion\":1,\"ready\":true,\"registeredScriptTypes\":1,\"submittedGameFrames\":90,\"submittedGameFrameId\":100}\n" +
        "[SMOKE] frame limit reached (120 GT frames, display frame 100, promotions 90)\n";
    var preflight = new Preflight("Demo.creator", "dx12", new(), false);
    PlayerVerification.ValidateMarkers(smoke, smoke, preflight, false, 120); ++checks;
    Reject(() => PlayerVerification.ValidateMarkers(smoke, smoke, preflight, true, 120), "Wrong Shipping binary accepted");
    Reject(() => PlayerVerification.ValidateMarkers(smoke.Replace(",\"submittedGameFrames\":90,\"submittedGameFrameId\":100", ""), smoke,
        preflight, false, 120), "Stale Player without native presentation metrics accepted");
    Reject(() => PlayerVerification.ValidateMarkers(smoke.Replace("\"submittedGameFrames\":90", "\"submittedGameFrames\":0"), smoke,
        preflight, false, 120), "Player without native game submissions accepted");
    Reject(() => PlayerVerification.ValidateMarkers(smoke.Replace("\"submittedGameFrameId\":100", "\"submittedGameFrameId\":0"), smoke,
        preflight, false, 120), "Player without a submitted game frame accepted");
    Reject(() => PlayerVerification.ValidateMarkers(smoke.Replace("calls=0", "calls=1"), smoke, preflight, false, 120), "Text parser use accepted");
    Reject(() => PlayerVerification.ValidateMarkers(smoke, smoke + "[model.generation] 게시 전 검증 실패", preflight, false, 120), "Failed model generation accepted");
    Reject(() => PlayerVerification.ValidateMarkers(smoke.Replace("\"ready\":true", "\"ready\":false"), smoke, preflight, false, 120), "Unready CLR accepted");

    var pidFile = Path.Combine(root, "child.pid"); using var timeoutContext = new BuildContext(new Options(["help"]), CancellationToken.None);
    await RejectAsync(() => timeoutContext.Run(Environment.ProcessPath!, ["--parent", pidFile], timeoutSeconds: 2, echo: false), "Timed out process was accepted");
    Check(File.Exists(pidFile), "Timeout fixture did not start a child"); var childId = int.Parse(File.ReadAllText(pidFile));
    var alive = false; try { using var child = Process.GetProcessById(childId); alive = !child.HasExited; } catch (ArgumentException) { }
    Check(!alive, "Timeout left a child process running");

    if (args.Length == 2 && args[0] == "--engine")
    {
        var real = EngineDistribution.Load(args[1], context); real.SelectProject(project);
        var trace = Path.Combine(root, "private-runtime.trace");
        var installed = await context.Run(Paths.Child(real.Root, real.Manifest.Text("buildTool")),
            ["verify-engine", "--engine-distribution", real.Root, "--json"], project,
            new() { ["PATH"] = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Windows), "System32"),
                ["DOTNET_ROOT"] = Path.Combine(root, "NoDotnet"), ["DOTNET_MULTILEVEL_LOOKUP"] = "0",
                ["DOTNET_HOST_TRACE"] = "1", ["DOTNET_HOST_TRACEFILE"] = trace }, echo: false);
        Check(JsonNode.Parse(installed.Output)!.Text("type") == "result", "Installed tool did not produce a JSON result");
        var traceText = File.ReadAllText(trace);
        Check(traceText.Contains("Using app-relative location") && traceText.Contains(real.BinaryRoot) &&
            !traceText.Contains("Using global install location"), "Installed apphost used the system .NET instead of its bundled runtime");
        var source = Path.Combine(project, "Assets/Script/Example.cs"); File.WriteAllText(source, "public static class Example { public static int Value => 42; }");
        var output = Path.Combine(project, "Intermediate/Managed");
        await GameCompiler.Compile(context, real, project, output, real.Configuration, "");
        var assembly = Path.Combine(output, "Scripts/GameScripts.dll"); var hash = Metadata.Hash(assembly); var identityHash = Metadata.Hash(assembly + ".engine.json");
        Check(Metadata.Read(assembly + ".engine.json").Text("engineBuildId") == real.Manifest.Text("buildId"), "Compiler pin not recorded");
        File.WriteAllText(source, "this is invalid C#");
        await RejectAsync(() => GameCompiler.Compile(context, real, project, output, real.Configuration, ""), "Compiler failure accepted");
        Check(Metadata.Hash(assembly) == hash && Metadata.Hash(assembly + ".engine.json") == identityHash, "Failed compile replaced last working assembly");
        await GameCompiler.Compile(context, real, project, Path.Combine(project, "Intermediate/Prebuilt"), real.Configuration, assembly); ++checks;
        File.AppendAllText(assembly, "corrupt");
        await RejectAsync(() => GameCompiler.Compile(context, real, project, Path.Combine(project, "Intermediate/Rejected"), real.Configuration, assembly), "Tampered prebuilt assembly accepted");
    }
    succeeded = true; Console.WriteLine($"BUILD_TOOL_TESTS_OK checks={checks}"); return 0;
}
catch (Exception ex) { Console.Error.WriteLine(ex); Console.Error.WriteLine($"Test fixture retained: {root}"); return 1; }
finally
{
    if (succeeded) Paths.DeleteTree(root, Path.GetTempPath());
}
