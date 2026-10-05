#include "SoundAssetCookProducer.h"
#include "AuthoringParseTelemetry.h"

#include <chrono>
#include <cstdio>
#include <fstream>

namespace
{
    unsigned g_checks{};
    unsigned g_failures{};
    void Check(bool passed, const char* label)
    {
        ++g_checks;
        g_failures += passed ? 0u : 1u;
        std::printf("[%s] %s\n", passed ? "PASS" : "FAIL", label);
    }
    experiment::AssetId Id(unsigned suffix)
    {
        auto value = Uuid::Parse("12345678-abcd-4abc-8def-123456789000");
        value.data[15] = static_cast<std::uint8_t>(suffix);
        return { value };
    }
    experiment::cooked::CookedAssetManifestEntry Entry(unsigned suffix,
        experiment::cooked::CookedAssetKind kind)
    {
        experiment::cooked::CookedAssetManifestEntry entry;
        entry.assetId = Id(suffix);
        entry.kind = kind;
        entry.formatVersion = 1u;
        entry.byteSize = 1u;
        entry.contentSha256.fill(1u);
        return entry;
    }
    Authoring::WriteDocument Sound(bool identity = true)
    {
        Authoring::WriteDocument document;
        const auto root = document.Root();
        if (identity)
        {
            root.Child("m_typeUUID").SetString("f2441c9e-234b-42cd-8067-2276a3c985fe");
        }
        root.Child("clipKey").SetString("walk");
        return document;
    }
    std::string Envelope(std::string_view value)
    {
        Authoring::WriteDocument document;
        document.Root().SetString(value);
        std::string encoded;
        std::string error;
        Check(Authoring::EncodeCookedDocumentTextEnvelope(document.Root().Read(), encoded, error), "make override CEDO envelope");
        return encoded;
    }
    std::string Unwrap(std::string_view envelope)
    {
        std::string error;
        auto document = Authoring::DecodeCookedDocumentTextEnvelope(envelope, error);
        return document ? document->Root().Read().AsString() : std::string{};
    }
    bool Rewrite(Authoring::WriteDocument& document,
        const experiment::cooked::CookedAssetManifest& manifest,
        const std::unordered_map<std::string, std::string>& aliases,
        std::vector<experiment::AssetId>& dependencies, std::string& error)
    {
        return sound_cook::RewriteSceneNode(document.Root(), manifest, aliases, dependencies, error);
    }
}

int main()
{
    namespace ck = experiment::cooked;
    const auto clipText = Uuid::ToString(Id(1u).value);
    ck::CookedAssetManifest manifest;
    manifest.entries = { Entry(1u, ck::CookedAssetKind::AudioClip), Entry(2u, ck::CookedAssetKind::SoundGraph),
        Entry(3u, ck::CookedAssetKind::SoundPreset) };
    std::unordered_map<std::string, std::string> aliases{ { "walk", clipText } };
    std::vector<experiment::AssetId> dependencies;
    std::string error;
    const auto beforeParses = Authoring::GetTextParseTelemetry().calls;

    auto component = Sound();
    component.Root().Child("soundGraphKey").SetString(Uuid::ToString(Id(2u).value));
    component.Root().Child("soundPresetKey").SetString(Uuid::ToString(Id(3u).value));
    component.Root().Child("sourceKind").SetString("Clip");
    component.Root().Child("customProperties").Child("clipKey").SetString("unrelated nested value");
    Check(Rewrite(component, manifest, aliases, dependencies, error), "SoundComponent UUID rewrites all audio asset references");
    Check(component.Root().Read()["clipKey"].Scalar() == clipText && dependencies.size() == 3u,
        "legacy direct clip becomes GUID and all three source kinds produce dependencies");
    Check(component.Root().Read()["customProperties"]["clipKey"].Scalar() == "unrelated nested value",
        "SoundComponent identity does not leak into unrelated nested property maps");

    auto ordinary = Sound(false);
    const auto ordinaryBefore = ordinary.Dump();
    dependencies.clear();
    Check(Rewrite(ordinary, manifest, {}, dependencies, error) && ordinary.Dump() == ordinaryBefore && dependencies.empty(),
        "ordinary clipKey map is unchanged even with no matching audio asset");
    ordinary.Root().Child("SomeOtherComponent").SetScalar(123u);
    ordinary.Root().Child("m_typeUUID").SetString("a84ed25a-31de-4289-92a0-546b27cee0ff");
    const auto otherBefore = ordinary.Dump();
    Check(Rewrite(ordinary, manifest, {}, dependencies, error) && ordinary.Dump() == otherBefore,
        "unrelated component sharing audio-like field names is unchanged");
    ordinary.Root().Child("SoundComponent").SetScalar(123u);
    Check(Rewrite(ordinary, manifest, aliases, dependencies, error)
        && ordinary.Root().Read()["clipKey"].Scalar() == "walk", "UUID takes precedence over conflicting legacy type header");
    auto legacy = Sound(false);
    legacy.Root().Child("SoundComponent").SetScalar(123u);
    Check(Rewrite(legacy, manifest, aliases, dependencies, error)
        && legacy.Root().Read()["clipKey"].Scalar() == clipText, "legacy SoundComponent type header remains supported");

    const auto encodedWalk = Envelope("walk");
    Authoring::WriteDocument overrideDocument;
    overrideDocument.Root().Child("m_componentType").SetString("SomeOtherComponent");
    overrideDocument.Root().Child("m_propertyName").SetString("clipKey");
    overrideDocument.Root().Child("m_valueYaml").SetString(encodedWalk);
    const auto overrideBefore = overrideDocument.Dump();
    dependencies.clear();
    Check(Rewrite(overrideDocument, manifest, {}, dependencies, error) && overrideDocument.Dump() == overrideBefore && dependencies.empty(),
        "other component override clipKey is preserved byte-for-byte and never resolved as audio");
    overrideDocument.Root().Child("m_componentType").SetString("SoundComponent");
    Check(Rewrite(overrideDocument, manifest, aliases, dependencies, error)
        && Unwrap(overrideDocument.Root().Read()["m_valueYaml"].Scalar()) == clipText && dependencies.size() == 1u,
        "known SoundComponent scalar prefab override resolves to GUID");
    overrideDocument.Root().Child("m_propertyName").SetString("sourceKind");
    overrideDocument.Root().Child("m_valueYaml").SetString(Envelope("Both"));
    Check(!Rewrite(overrideDocument, manifest, aliases, dependencies, error) && !error.empty(),
        "invalid SoundComponent sourceKind prefab override is rejected");
    overrideDocument.Root().Child("m_componentType").SetString("SomeOtherComponent");
    Check(Rewrite(overrideDocument, manifest, {}, dependencies, error), "other component sourceKind override is not validated as audio");

    auto missing = Sound();
    missing.Root().Child("clipKey").SetString("missing");
    Check(!Rewrite(missing, manifest, aliases, dependencies, error), "missing legacy clip fails with SoundComponent identity");
    auto collision = Sound();
    Check(!Rewrite(collision, manifest, { { "walk", "" } }, dependencies, error), "ambiguous legacy clip fails instead of choosing first basename");
    auto wrongKind = Sound();
    wrongKind.Root().Child("soundGraphKey").SetString(clipText);
    Check(!Rewrite(wrongKind, manifest, aliases, dependencies, error), "audio GUID reference of wrong source kind is rejected");
    auto nonscalar = Sound();
    nonscalar.Root().Child("clipKey").SetMap();
    Check(!Rewrite(nonscalar, manifest, aliases, dependencies, error), "SoundComponent nonscalar asset reference is rejected");
    auto invalidKind = Sound();
    invalidKind.Root().Child("sourceKind").SetString("Unexpected");
    Check(!Rewrite(invalidKind, manifest, aliases, dependencies, error), "unknown SoundComponent source kind is rejected");
    Check(Authoring::GetTextParseTelemetry().calls == beforeParses, "scene/prefab audio rewriting never invokes YAML text parser");

    const auto root = std::filesystem::temp_directory_path()
        / ("phase22-sound-cook-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(root);
    wave::SoundGraphDefinition graph;
    wave::SoundNode clip;
    clip.id = 1u;
    clip.clip = wave::ClipKey::FromGuid(Id(1u).value);
    wave::SoundNode output;
    output.id = 2u;
    output.kind = wave::SoundNodeKind::Output;
    output.inputs = { 1u };
    graph.nodes = { clip, output };
    graph.output = 2u;
    wave::SoundPreset preset;
    preset.source = { wave::SoundSourceKind::Graph, wave::ClipKey::FromGuid(Id(2u).value) };
    { std::ofstream file(root / "loop.soundgraph"); file << wave::WriteSoundGraph(graph); }
    { std::ofstream file(root / "loop.soundpreset"); file << wave::WriteSoundPreset(preset); }
    ck::CookedAssetManifest productsManifest;
    productsManifest.entries = { Entry(1u, ck::CookedAssetKind::AudioClip) };
    productsManifest.sourceAssets = { { Id(1u), "walk.wav" }, { Id(2u), "loop.soundgraph" }, { Id(3u), "loop.soundpreset" } };
    std::set<std::string> paths;
    std::vector<sound_cook::Product> products;
    Check(sound_cook::Build(root, productsManifest, paths, products, error) && products.size() == 2u,
        "graph and preset sources produce first-class CEDO artifacts");
    Check(products.size() == 2u && products[0].entry.dependencies == std::vector{ Id(1u) }
        && products[1].entry.dependencies == std::vector{ Id(2u) }, "cooked graph/preset dependencies are exact GUIDs");

    ck::SceneCookProduct scene;
    scene.sceneAssetId = Id(4u);
    scene.manifestEntry = Entry(4u, ck::CookedAssetKind::Scene);
    scene.artifactPath = ck::MakeDerivedSceneArtifactPath(scene.sceneAssetId);
    auto sceneDocument = Sound();
    Check(Authoring::EncodeCookedDocument(sceneDocument.Root().Read(), scene.artifactBytes, error), "encode scene cook fixture");
    productsManifest.entries.insert(productsManifest.entries.begin(), scene.manifestEntry); // deliberately unsorted
    std::vector<ck::SceneCookProduct> scenes{ scene };
    Check(sound_cook::RewriteScenes(scenes, productsManifest, error), "scene rewrite handles unsorted producer manifest entries");
    auto sceneDecoded = Authoring::DecodeCookedDocument(scenes[0].artifactBytes, error);
    Check(sceneDecoded && sceneDecoded->Root().Read()["clipKey"].Scalar() == clipText,
        "published scene bytes contain GUID rather than authoring basename");
    ck::Sha256Digest digest;
    Check(ck::ComputeSha256(scenes[0].artifactBytes, digest, error)
        && productsManifest.Find(Id(4u))->contentSha256 == digest
        && productsManifest.Find(Id(4u))->dependencies == std::vector{ Id(1u) },
        "rewritten scene manifest digest and dependency table match published bytes");
    constexpr std::u8string_view unicodeNames[]{ u8"\uBC1C\uC790\uAD6D", u8"\U0001F3B5-footstep" };
    for (const auto unicodeName : unicodeNames)
    {
        const std::string legacyName(unicodeName.begin(), unicodeName.end());
        auto unicodeManifest = productsManifest;
        unicodeManifest.sourceAssets[0].sourcePath = "Sounds/" + legacyName + ".wav";
        auto unicodeDocument = Sound();
        unicodeDocument.Root().Child("clipKey").SetString(legacyName);
        auto unicodeScene = scene;
        Check(Authoring::EncodeCookedDocument(unicodeDocument.Root().Read(), unicodeScene.artifactBytes, error),
            "encode Korean/emoji legacy audio basename fixture");
        std::vector<ck::SceneCookProduct> unicodeScenes{ unicodeScene };
        Check(sound_cook::RewriteScenes(unicodeScenes, unicodeManifest, error),
            "Korean/emoji UTF-8 basename resolves without locale/ACP conversion");
        auto unicodeDecoded = Authoring::DecodeCookedDocument(unicodeScenes[0].artifactBytes, error);
        Check(unicodeDecoded && unicodeDecoded->Root().Read()["clipKey"].Scalar() == clipText
            && unicodeManifest.Find(Id(4u))->dependencies == std::vector{ Id(1u) },
            "Korean/emoji migration publishes the correct GUID and dependency");
        unicodeManifest.entries.push_back(Entry(5u, ck::CookedAssetKind::AudioClip));
        unicodeManifest.sourceAssets.push_back({ Id(5u), "Other/" + legacyName + ".wav" });
        unicodeScenes = { unicodeScene };
        Check(!sound_cook::RewriteScenes(unicodeScenes, unicodeManifest, error),
            "colliding Korean/emoji UTF-8 basenames fail without selecting an arbitrary GUID");
    }
    auto malformedGraph = Authoring::WriteDocument::ParseText(wave::WriteSoundGraph(graph), &error);
    malformedGraph->Root().Child("nodes").At(0u).Child("clip").SetMap();
    wave::SoundGraphDefinition graphRead;
    Check(!wave::ReadSoundGraph(malformedGraph->Dump(), graphRead, error),
        "authoring nonscalar clip reference is rejected rather than treated as an empty draft");
    auto malformedPreset = Authoring::WriteDocument::ParseText(wave::WriteSoundPreset(preset), &error);
    malformedPreset->Root().Child("source").Child("asset").SetMap();
    wave::SoundPreset presetRead;
    Check(!wave::ReadSoundPreset(malformedPreset->Dump(), presetRead, error),
        "authoring nonscalar preset reference is rejected rather than treated as an empty draft");
    std::vector<std::byte> oversized;
    const std::string huge(4u * 1024u * 1024u + 1u, 'x');
    graph.parameters.push_back({ "huge", wave::ParameterType::String, huge });
    Check(!wave::CookSoundGraph(graph, oversized, error) && oversized.empty(),
        "graph cooker rejects output larger than runtime reader limit");
    graph.parameters.clear();
    preset.parameters.emplace("huge", huge);
    Check(!wave::CookSoundPreset(preset, oversized, error) && oversized.empty(),
        "preset cooker rejects output larger than runtime reader limit");
    preset.parameters.clear();
    graph.nodes[0].kind = wave::SoundNodeKind::Layer;
    graph.nodes[0].inputs = { 1u };
    { std::ofstream file(root / "loop.soundgraph"); file << wave::WriteSoundGraph(graph); }
    products.clear();
    paths.clear();
    Check(!sound_cook::Build(root, productsManifest, paths, products, error), "cyclic graph is rejected before cook publication");
    std::filesystem::remove_all(root);
    std::printf("sound-cook-contract checks=%u failures=%u\n", g_checks, g_failures);
    return g_failures == 0u ? 0 : 1;
}
