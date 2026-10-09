#include "Audio/AudioCatalog.h"
#include "Audio/AudioRuntime.h"
#include "Audio/EditorAudioClipCache.h"
#include "Experiment/Cooked/CookedAssetCatalog.h"
#include "Audio/NullAudioBackend.h"
#include "Audio/MiniaudioBackend.h"
#include "Audio/SoundAssetSerialization.h"
#include "AuthoringParseTelemetry.h"

#include <cstdio>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <thread>

namespace
{
    unsigned checks = 0u;
    unsigned failures = 0u;

    void Check(bool condition, const char* label)
    {
        ++checks;
        failures += condition ? 0u : 1u;
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label);
    }

    wave::ClipKey Guid(unsigned value)
    {
        auto id = Uuid::Parse("01234567-89ab-4cde-8fab-0123456789ab");
        id.data[15] = static_cast<std::uint8_t>(value);
        return wave::ClipKey::FromGuid(id);
    }

    wave::SoundGraphDefinition Graph()
    {
        wave::SoundGraphDefinition graph;
        wave::SoundNode clip;
        clip.id = 1u;
        clip.clip = Guid(1u);
        wave::SoundNode output;
        output.id = 2u;
        output.kind = wave::SoundNodeKind::Output;
        output.inputs = { 1u };
        graph.nodes = { clip, output };
        graph.output = 2u;
        graph.parameters = { { "boolean", wave::ParameterType::Boolean, true },
            { "integer", wave::ParameterType::Integer, std::int32_t{ -32 } },
            { "float", wave::ParameterType::Float, 0.625f },
            { "empty", wave::ParameterType::String, std::string{} },
            { "nullText", wave::ParameterType::String, std::string("null") },
            { "tilde", wave::ParameterType::String, std::string("~") },
            { "quoted", wave::ParameterType::String, std::string("voice: #1\nnext") } };
        return graph;
    }

    bool SameParameters(const wave::SoundGraphDefinition& left, const wave::SoundGraphDefinition& right)
    {
        if (left.parameters.size() != right.parameters.size())
        {
            return false;
        }
        for (std::size_t index = 0u; index < left.parameters.size(); ++index)
        {
            const auto& a = left.parameters[index];
            const auto& b = right.parameters[index];
            if (a.name != b.name || a.type != b.type || a.defaultValue != b.defaultValue)
            {
                return false;
            }
        }
        return true;
    }

    void GraphRoundtrip()
    {
        const auto original = Graph();
        std::string error;
        const auto text = wave::WriteSoundGraph(original);
        wave::SoundGraphDefinition parsed;
        Check(!text.empty() && wave::ReadSoundGraph(text, parsed, error)
            && SameParameters(original, parsed) && parsed.nodes[0].clip == Guid(1u),
            "Authoring SoundGraph preserves typed values including empty/null/tilde strings");
        Check(wave::WriteSoundGraph(parsed) == text, "Authoring SoundGraph writer is deterministic after roundtrip");
        std::vector<std::byte> cooked;
        Check(wave::CookSoundGraph(original, cooked, error), "Valid SoundGraph compiles to cooked CEDO bytes");
        const auto before = Authoring::GetTextParseTelemetry().calls;
        wave::SoundGraphDefinition runtime;
        Check(wave::ReadCookedSoundGraph(cooked, runtime, error)
            && SameParameters(original, runtime) && runtime.nodes[0].clip == Guid(1u),
            "Cooked SoundGraph reads original typed values");
        Check(Authoring::GetTextParseTelemetry().calls == before,
            "Cooked SoundGraph runtime path performs zero text parses");
        std::vector<std::byte> again;
        Check(wave::CookSoundGraph(runtime, again, error) && again == cooked,
            "Cooked SoundGraph bytes are deterministic across roundtrip");
        for (std::size_t prefix = 0u; prefix < cooked.size(); ++prefix)
        {
            auto unchanged = original;
            Check(!wave::ReadCookedSoundGraph(std::span(cooked.data(), prefix), unchanged, error)
                && SameParameters(unchanged, original), "Every truncated cooked graph prefix fails transactionally");
        }
        auto invalid = original;
        invalid.nodes[0].clip = {};
        Check(!wave::CookSoundGraph(invalid, again, error), "Blank authoring clip cannot become cooked graph");
        invalid = original;
        invalid.nodes[1].inputs = { 99u };
        Check(!wave::CookSoundGraph(invalid, again, error), "Dangling graph cannot become cooked artifact");
        auto unchanged = original;
        Check(!wave::ReadSoundGraph("schemaVersion: 999\nnodes: []\n", unchanged, error)
            && SameParameters(unchanged, original), "Invalid authoring schema fails without overwriting output");
        Check(!wave::ReadSoundGraph(text + "\nUnexpectedField: 1\n", unchanged, error),
            "Unknown authoring field rejected");
        Check(!wave::ReadSoundGraph(text + "\nschemaVersion: 1\n", unchanged, error),
            "Duplicate or unknown schema field rejected");
        Check(!wave::ReadCookedSoundGraph(std::as_bytes(std::span(text.data(), text.size())), unchanged, error),
            "Cooked reader never accepts YAML fallback");
    }

    void PresetRoundtrip()
    {
        wave::SoundPreset preset;
        preset.source = { wave::SoundSourceKind::Graph, Guid(20u) };
        preset.defaults.bus = wave::Buses::Player;
        preset.defaults.volume = 0.4f;
        preset.defaults.pitch = 1.25f;
        preset.defaults.spatialBlend = 0.6f;
        preset.defaults.loop = true;
        preset.defaults.useReverbSend = true;
        preset.defaults.reverbSendDecibels = -6.0f;
        preset.defaults.rolloff = wave::RolloffKind::Custom;
        preset.defaults.customRolloff = { { 0.0f, 1.0f }, { 5.0f, 0.5f }, { 20.0f, 0.0f } };
        preset.parameters = { { "gain", 0.25f }, { "variant", std::int32_t{ 2 } },
            { "enabled", true }, { "tag", std::string("null") } };
        std::string error;
        const auto text = wave::WriteSoundPreset(preset);
        wave::SoundPreset parsed;
        Check(wave::ReadSoundPreset(text, parsed, error) && parsed.source.asset == preset.source.asset
            && parsed.parameters == preset.parameters && parsed.defaults.bus == preset.defaults.bus
            && parsed.defaults.customRolloff.size() == 3u && parsed.defaults.useReverbSend,
            "SoundPreset YAML preserves source, settings and typed parameter overrides");
        Check(wave::WriteSoundPreset(parsed) == text, "SoundPreset YAML roundtrip deterministic");
        std::vector<std::byte> cooked;
        Check(wave::CookSoundPreset(preset, cooked, error), "SoundPreset cooks to CEDO envelope");
        const auto before = Authoring::GetTextParseTelemetry().calls;
        Check(wave::ReadCookedSoundPreset(cooked, parsed, error) && parsed.parameters == preset.parameters,
            "SoundPreset cooked typed values roundtrip");
        Check(Authoring::GetTextParseTelemetry().calls == before, "Cooked preset performs zero text parses");
        for (std::size_t prefix = 0u; prefix < cooked.size(); ++prefix)
        {
            Check(!wave::ReadCookedSoundPreset(std::span(cooked.data(), prefix), parsed, error),
                "Every truncated cooked preset prefix fails closed");
        }
        preset.defaults.pitch = std::numeric_limits<float>::infinity();
        Check(!wave::CookSoundPreset(preset, cooked, error), "Nonfinite preset pitch cannot enter cooked output");
    }

    std::vector<std::byte> ReadBytes(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        const std::vector<char> raw{ std::istreambuf_iterator<char>(input), {} };
        std::vector<std::byte> bytes;
        for (const char value : raw)
        {
            bytes.push_back(std::byte(static_cast<unsigned char>(value)));
        }
        return bytes;
    }

    void WriteBytes(const std::filesystem::path& path, const std::vector<std::byte>& bytes)
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    void WriteText(const std::filesystem::path& path, const std::string& text)
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << text;
    }

    void CacheAndCatalog(const std::filesystem::path& formats, const std::filesystem::path& work)
    {
        namespace fs = std::filesystem;
        namespace ck = experiment::cooked;
        fs::create_directories(work);
        const auto mutableSource = work / "mutable.wav";
        const auto original = ReadBytes(formats / "sine.wav");
        WriteBytes(mutableSource, original);
        std::string error;
        ck::CookedAudioClipSource oldSource;
        Check(wave::ImportEditorAudioClip(mutableSource, work / "cache", Guid(1u),
            "Stream", "PointMono", oldSource, error), "Editor imports mono source into immutable streaming CEAC generation");
        Check(oldSource.Metadata().loadMode == ck::AudioLoadMode::Stream
            && oldSource.Metadata().channels == 1u && oldSource.Metadata().sampleRate == 48000u
            && oldSource.Metadata().frameCount == 4800u, "Editor import preserves requested load/spatial policy and actual PCM metadata");
        std::vector<std::byte> payload(oldSource.PayloadSize());
        Check(oldSource.ReadPayload(0u, payload, error) && payload == original
            && Hash::Sha256::Compute(payload.data(), payload.size()) == oldSource.Metadata().payloadSha256,
            "Editor cached source contains full original encoded payload with matching digest");
        const auto changed = ReadBytes(formats / "impulse.wav");
        WriteBytes(mutableSource, changed);
        ck::CookedAudioClipSource newSource;
        Check(wave::ImportEditorAudioClip(mutableSource, work / "cache", Guid(1u),
            "Stream", "PointMono", newSource, error), "Modified source publishes new content-addressed generation");
        Check(oldSource.ReadPayload(0u, payload, error) && payload == original
            && oldSource.Metadata().payloadSha256 != newSource.Metadata().payloadSha256,
            "Old mounted source still reads original bytes after authoring file changes");
        payload.resize(newSource.PayloadSize());
        Check(newSource.ReadPayload(0u, payload, error) && payload == changed,
            "New mounted source reads newly authored bytes");
        const auto generation = work / "cache" / (Hash::ToHex(newSource.Metadata().payloadSha256)
            + "-Stream-PointMono-v1");
        const experiment::AssetId id{ Uuid::Parse(Guid(1u).Text()) };
        const auto artifact = generation / ck::MakeDerivedAudioClipArtifactPath(id);
        auto corrupt = ReadBytes(artifact);
        if (!corrupt.empty())
        {
            corrupt.back() ^= std::byte{ 1u };
        }
        WriteBytes(artifact, corrupt);
        ck::CookedAudioClipSource untouched = oldSource;
        Check(!wave::ImportEditorAudioClip(mutableSource, work / "cache", Guid(1u),
            "Stream", "PointMono", untouched, error), "Corrupt existing immutable cache artifact fails instead of being overwritten");
        Check(ReadBytes(artifact) == corrupt, "Rejected import leaves corrupt existing target byte-identical");
        const auto oldGeneration = work / "cache" / (Hash::ToHex(oldSource.Metadata().payloadSha256)
            + "-Stream-PointMono-v1");
        fs::rename(oldGeneration, work / "retired-generation");
        const auto substitute = oldGeneration / ck::MakeDerivedAudioClipArtifactPath(id);
        fs::create_directories(substitute.parent_path());
        WriteBytes(substitute, corrupt);
        payload.resize(oldSource.PayloadSize());
        Check(oldSource.ReadPayload(0u, payload, error) && payload == original,
            "Pinned cache file identity survives replacement of its former directory path");
        std::array<bool, 2u> published{};
        std::array<std::thread, 2u> publishers;
        for (std::size_t index = 0u; index < publishers.size(); ++index)
        {
            publishers[index] = std::thread([&, index]
            {
                ck::CookedAudioClipSource result;
                std::string failure;
                published[index] = wave::ImportEditorAudioClip(mutableSource, work / "race-cache", Guid(4u),
                    "Stream", "PointMono", result, failure);
            });
        }
        for (auto& publisher : publishers)
        {
            publisher.join();
        }
        Check(published[0] && published[1], "Concurrent identical cache publishers both verify same no-clobber generation");
        auto stereo = original;
        stereo[22] = std::byte{ 2u };
        const auto put = [&](std::size_t offset, std::uint32_t value, unsigned width)
        {
            for (unsigned index = 0u; index < width; ++index)
            {
                stereo[offset + index] = std::byte(value >> (8u * index));
            }
        };
        put(28u, 48000u * 4u, 4u);
        put(32u, 4u, 2u);
        const auto stereoPath = work / "stereo.wav";
        WriteBytes(stereoPath, stereo);
        Check(!wave::ImportEditorAudioClip(stereoPath, work / "cache", Guid(2u),
            "Resident", "PointMono", untouched, error), "Stereo source cannot silently enter PointMono import");
        Check(wave::ImportEditorAudioClip(stereoPath, work / "cache", Guid(2u),
            "Resident", "NonSpatial", untouched, error) && untouched.Metadata().channels == 2u,
            "Stereo NonSpatial import succeeds with actual channel count");
        {
            wave::MiniaudioBackend physical;
            wave::AudioRuntime runtime(physical, 2u);
            wave::DeviceSettings settings;
            settings.noDevice = true;
            Check(runtime.Start(settings) && runtime.LoadCookedClip(untouched),
                "Stereo imported CEAC enters actual no-device playback backend");
            wave::PlayRequest request;
            request.clip = Guid(2u);
            request.loop = true;
            request.volume = 0.35f;
            const auto voice = runtime.Play(request);
            std::vector<float> pcm(4096u * 2u);
            const auto energy = [&]()
            {
                double sum = 0.0;
                for (const float sample : pcm)
                {
                    sum += static_cast<double>(sample) * sample;
                }
                return sum;
            };
            Check(voice.IsValid() && physical.Render(pcm.data(), 4096u), "Stereo 2D playback renders before rejected update");
            const auto beforeEnergy = energy();
            auto invalidSettings = request;
            invalidSettings.spatialBlend = 1.0f;
            invalidSettings.volume = 0.9f;
            invalidSettings.pitch = 2.0f;
            const auto rejected = runtime.Metrics().rejectedUpdates;
            runtime.SetVoiceSettings(voice, invalidSettings);
            Check(runtime.IsAlive(voice) && !runtime.LastError().empty()
                && runtime.Metrics().rejectedUpdates == rejected + 1u,
                "Live stereo-to-3D update rejects transactionally with error and diagnostic counter");
            (void)runtime.Seek(voice, 0u);
            Check(physical.Render(pcm.data(), 4096u) && beforeEnergy > 0.0
                && std::abs(energy() - beforeEnergy) < beforeEnergy * 0.1,
                "Rejected stereo spatial update preserves prior gain/pitch and audible PCM");
            request.volume = 0.0f;
            runtime.SetVoiceSettings(voice, request);
            (void)physical.Render(pcm.data(), 4096u);
            Check(runtime.LastError().empty() && physical.Render(pcm.data(), 4096u) && energy() < 0.000001,
                "Subsequent valid settings clear error and apply to same live stereo handle");
            runtime.Shutdown();
        }
        bool cleaned = true;
        for (const auto& entry : fs::directory_iterator(work / "cache"))
        {
            cleaned = cleaned && !entry.path().filename().string().starts_with(".import-");
        }
        Check(cleaned, "Successful and failed import paths remove staging directories");
#if !defined(_WIN32)
        const auto outside = work / "outside";
        fs::create_directories(outside);
        const auto symlinkCache = work / "symlink-cache";
        fs::create_directory_symlink(outside, symlinkCache);
        Check(!wave::ImportEditorAudioClip(mutableSource, symlinkCache, Guid(3u),
            "Resident", "PointMono", untouched, error) && fs::is_empty(outside),
            "Symlink cache root rejected before creating any outside artifact");
        const auto nestedCache = work / "nested-cache";
        const auto nestedGeneration = nestedCache / (Hash::ToHex(newSource.Metadata().payloadSha256)
            + "-Resident-PointMono-v1");
        fs::create_directories(nestedGeneration);
        fs::create_directory_symlink(outside, nestedGeneration / "Derived");
        Check(!wave::ImportEditorAudioClip(mutableSource, nestedCache, Guid(3u),
            "Resident", "PointMono", untouched, error) && fs::is_empty(outside),
            "Symlink Derived path rejected before escaping content-addressed cache");
#endif
        const auto assets = work / "Assets";
        fs::create_directories(assets / "A");
        fs::create_directories(assets / "B");
        const auto meta = [](const wave::ClipKey& key)
        {
            return "guid: " + key.Text() + "\naudioClip:\n  schemaVersion: 1\n  loadMode: Resident\n  spatialKind: PointMono\n";
        };
        WriteBytes(assets / "A" / "shared.wav", original);
        WriteText(assets / "A" / "shared.wav.meta", meta(Guid(1u)));
        WriteText(assets / "music.soundgraph", wave::WriteSoundGraph(Graph()));
        WriteText(assets / "music.soundgraph.meta", "guid: " + Guid(20u).Text() + "\n");
        wave::NullAudioBackend backend;
        wave::AudioRuntime audio(backend, 8u);
        Check(audio.Start({}), "Catalog logical backend starts");
        wave::PlaybackService playback(audio, 8u);
        wave::AudioCatalog catalog(audio, playback);
        Check(catalog.LoadEditorAssets(assets, work / "catalog-cache", error),
            "Editor catalog imports GUID clip and compiles graph dependencies");
        Check(catalog.ResolveLegacyClip("SHARED", error) == Guid(1u).Text(),
            "Unique legacy basename resolves case-insensitively to GUID");
        const auto scope = playback.CreateScope(wave::ScopeKind::EditorPreview);
        wave::PlaybackRequest request;
        request.source = { wave::SoundSourceKind::Graph, Guid(20u) };
        const auto current = playback.Play(scope, request);
        Check(current.IsValid() && playback.ChildVoiceCount(current) == 1u,
            "Catalog graph drives same PlaybackService used by normal consumers");
        WriteBytes(assets / "B" / "shared.wav", changed);
        WriteText(assets / "B" / "shared.wav.meta", meta(Guid(2u)));
        Check(catalog.LoadEditorAssets(assets, work / "catalog-cache", error),
            "Editor catalog accepts different GUIDs with same source basename");
        Check(catalog.ResolveLegacyClip("shared", error).empty() && !error.empty(),
            "Ambiguous legacy basename fails closed instead of silently picking clip");
        Check(catalog.ResolveLegacyClip(Guid(1u).Text(), error) == Guid(1u).Text(),
            "GUID lookup remains unambiguous when filenames collide");
        WriteText(assets / "music.soundgraph", "invalid: [broken");
        const bool reloaded = catalog.LoadEditorAssets(assets, work / "catalog-cache", error);
        Check(!reloaded && !error.empty() && playback.HasSource(request.source) && playback.IsAlive(current),
            "Invalid graph edit reports diagnostic and preserves prior playable generation");
        catalog.Clear();
        Check(audio.ListClipKeys().empty() && !playback.HasSource(request.source),
            "Catalog clear removes registered graph and loaded GUID clips");
        playback.Update();
        Check(!playback.IsAlive(current), "Catalog clip unload retires dependent playback on owner update");
        playback.Shutdown();
        audio.Shutdown();
    }
}

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        std::fprintf(stderr, "usage: sound_asset_contract_probe FORMAT_DIRECTORY OWNED_WORK_DIRECTORY\n");
        return 2;
    }
    GraphRoundtrip();
    PresetRoundtrip();
    CacheAndCatalog(argv[1], argv[2]);
    std::printf("SUMMARY checks=%u passed=%u failed=%u\n", checks, checks - failures, failures);
    return failures ? 1 : 0;
}
