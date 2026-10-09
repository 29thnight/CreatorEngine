#include "AudioCatalog.h"
#include "SoundAssetSerialization.h"
#include "EditorAudioClipCache.h"

#include "../../RenderEngine/Assets/AudioClipSourceMetadata.h"
#include "../../RenderEngine/Experiment/Cooked/CookedAssetCatalog.h"
#include "../../RenderEngine/Experiment/Cooked/CookedAudioClipSource.h"
#include "../../Utility_Framework/AuthoringParsedDocument.h"

#include <algorithm>
#include <fstream>
#include <map>

namespace wave
{
    namespace
    {
        namespace ck = experiment::cooked;
        using Graphs = std::unordered_map<ClipKey, std::shared_ptr<const SoundGraphProgram>>;
        using Presets = std::unordered_map<ClipKey, SoundPreset>;

        std::string Lower(std::string text)
        {
            std::ranges::transform(text, text.begin(), [](unsigned char value)
            {
                return static_cast<char>(value >= 'A' && value <= 'Z' ? value + ('a' - 'A') : value);
            });
            return text;
        }

        bool ReadText(const std::filesystem::path& path, std::string& text, std::string& error)
        {
            std::error_code code;
            const auto size = std::filesystem::file_size(path, code);
            if (code || size > 4u * 1024u * 1024u)
            {
                error = "Sound asset is missing or exceeds 4 MiB: " + path.string();
                return false;
            }
            std::ifstream input(path, std::ios::binary);
            text.resize(static_cast<std::size_t>(size));
            input.read(text.data(), static_cast<std::streamsize>(size));
            if (!input)
            {
                error = "Cannot read sound asset: " + path.string();
                return false;
            }
            return true;
        }

        bool ValidatePresets(const Presets& presets, const Graphs& graphs,
            const std::unordered_map<ClipKey, std::string>& clips, std::string& error)
        {
            for (const auto& [id, preset] : presets)
            {
                if (preset.source.kind == SoundSourceKind::Clip)
                {
                    if (!clips.contains(preset.source.asset) || !preset.parameters.empty())
                    {
                        error = "SoundPreset has a missing clip or parameters without a graph: " + id.Text();
                        return false;
                    }
                }
                else if (preset.source.kind == SoundSourceKind::Graph)
                {
                    const auto graph = graphs.find(preset.source.asset);
                    ParameterMap resolved;
                    if (graph == graphs.end() || !ResolveGraphParameters(*graph->second, preset.parameters, resolved, error))
                    {
                        error = "SoundPreset graph/parameters are invalid: " + id.Text() + " " + error;
                        return false;
                    }
                }
                else
                {
                    error = "Nested SoundPresets are unsupported: " + id.Text();
                    return false;
                }
            }
            return true;
        }

        void AddAlias(std::unordered_map<std::string, std::string>& aliases,
            const std::filesystem::path& path, const ClipKey& id)
        {
            const auto utf8 = path.stem().u8string();
            const auto name = Lower(std::string(utf8.begin(), utf8.end()));
            const auto [found, inserted] = aliases.emplace(name, id.Text());
            if (!inserted && found->second != id.Text())
            {
                // Empty denotes a collision; GUID playback remains available.
                found->second.clear();
            }
        }
    }

    AudioCatalog::AudioCatalog(AudioService& audio, PlaybackService& playback)
        : m_audio(audio), m_playback(playback)
    {
    }

    AudioCatalog::~AudioCatalog()
    {
        Clear();
    }

    bool AudioCatalog::LoadEditorAssets(const std::filesystem::path& assetsRoot, std::string& error)
    {
        return LoadEditorAssets(assetsRoot, assetsRoot.parent_path() / "Library/AudioClipImports", error);
    }

    bool AudioCatalog::LoadEditorAssets(const std::filesystem::path& assetsRoot,
        const std::filesystem::path& cacheRoot, std::string& error)
    {
        struct ImportSettings final
        {
            std::string loadMode;
            std::string spatialKind;
        };
        std::unordered_map<ClipKey, ImportSettings> importSettings;
        std::unordered_map<ClipKey, std::filesystem::path> clipPaths;
        std::unordered_map<ClipKey, std::string> clips;
        std::unordered_map<ClipKey, SoundGraphDefinition> definitions;
        Graphs graphs;
        Presets presets;
        std::unordered_map<std::string, std::string> aliases;
        std::unordered_set<ClipKey> identities;
        std::unordered_set<ClipKey> invalidAssets;
        std::string diagnostics;
        const auto invalid = [&](const ClipKey& id, const std::string& detail)
        {
            invalidAssets.insert(id);
            diagnostics += id.Text() + ": " + detail + "\n";
        };
        std::error_code code;
        std::filesystem::recursive_directory_iterator iterator(assetsRoot, code);
        const std::filesystem::recursive_directory_iterator end;
        for (; !code && iterator != end; iterator.increment(code))
        {
            if (!iterator->is_regular_file(code))
            {
                continue;
            }
            const auto& path = iterator->path();
            const auto extension = Lower(path.extension().string());
            const bool clip = assets::IsAudioClipSource(path);
            if (!clip && extension != ".soundgraph" && extension != ".soundpreset")
            {
                continue;
            }
            std::string text;
            auto metaPath = path;
            metaPath += ".meta";
            if (!ReadText(metaPath, text, error))
            {
                return false;
            }
            const auto meta = Authoring::ParsedDocument::ParseText(text, error);
            experiment::AssetId assetId;
            if (!meta || !experiment::TryParseCanonicalAssetId(meta.Root()["guid"].Scalar(), assetId))
            {
                error = "Audio asset has invalid GUID sidecar: " + path.string();
                return false;
            }
            const auto id = ClipKey::FromGuid(assetId.value);
            if (!identities.insert(id).second)
            {
                error = "Duplicate audio GUID: " + id.Text();
                return false;
            }
            if (clip)
            {
                const auto settings = meta.Root()["audioClip"];
                if (settings.HasChild("loopStartFrame") || settings.HasChild("loopEndFrame")
                    || meta.Root().HasChild("loopStartFrame") || meta.Root().HasChild("loopEndFrame"))
                {
                    error = "Audio loop markers require a newer CEAC schema; only whole-clip looping is supported: " + path.string();
                    return false;
                }
                const auto mode = settings["loadMode"].AsString();
                const auto spatial = settings["spatialKind"].AsString();
                if (settings["schemaVersion"].Scalar() != "1" || !assets::IsAudioLoadMode(mode)
                    || !assets::IsAudioSpatialKind(spatial))
                {
                    error = "Audio source import policy is invalid: " + path.string();
                    return false;
                }
                importSettings.emplace(id, ImportSettings{ mode, spatial });
                const auto stamp = std::filesystem::last_write_time(path, code);
                const auto size = std::filesystem::file_size(path, code);
                if (code)
                {
                    break;
                }
                const auto utf8 = path.generic_u8string();
                clips.emplace(id, std::string(utf8.begin(), utf8.end()) + ":"
                    + std::to_string(stamp.time_since_epoch().count()) + ":" + std::to_string(size)
                    + ":" + mode + ":" + spatial + ":" + settings["sourceContentHash"].AsString());
                clipPaths.emplace(id, path);
                AddAlias(aliases, path, id);
            }
            else
            {
                if (!ReadText(path, text, error))
                {
                    return false;
                }
                if (extension == ".soundgraph")
                {
                    SoundGraphDefinition graph;
                    if (!ReadSoundGraph(text, graph, error))
                    {
                        invalid(id, path.string() + ": " + error);
                        continue;
                    }
                    definitions.emplace(id, std::move(graph));
                }
                else
                {
                    SoundPreset preset;
                    if (!ReadSoundPreset(text, preset, error))
                    {
                        invalid(id, path.string() + ": " + error);
                        continue;
                    }
                    presets.emplace(id, std::move(preset));
                }
            }
        }
        if (code)
        {
            error = "Cannot scan editor audio assets: " + code.message();
            return false;
        }
        for (const auto& [id, definition] : definitions)
        {
            auto program = CompileSoundGraph(definition, [&](const ClipKey& clip) { return clips.contains(clip); }, error);
            if (!program)
            {
                invalid(id, error);
                continue;
            }
            graphs.emplace(id, std::move(program));
        }
        for (const auto& id : invalidAssets)
        {
            if (const auto previous = m_editorGraphs.find(id); previous != m_editorGraphs.end())
            {
                graphs.emplace(id, previous->second);
            }
        }
        for (auto preset = presets.begin(); preset != presets.end();)
        {
            const Presets candidate{ *preset };
            if (!ValidatePresets(candidate, graphs, clips, error))
            {
                invalid(preset->first, error);
                preset = presets.erase(preset);
            }
            else
            {
                ++preset;
            }
        }
        // Compilation is transactional. Invalid edits keep the last good
        // registrations; unchanged clip bytes do not interrupt live voices.
        for (const auto& [id, stamp] : clips)
        {
            const auto old = m_clipStamps.find(id);
            if (old == m_clipStamps.end() || old->second != stamp)
            {
                ck::CookedAudioClipSource source;
                const auto& settings = importSettings.at(id);
                if (!ImportEditorAudioClip(clipPaths.at(id), cacheRoot, id, settings.loadMode, settings.spatialKind, source, error)
                    || !m_audio.LoadCookedClip(source))
                {
                    error = "Editor audio import failed: " + clipPaths.at(id).string() + " " + error;
                    return false;
                }
                m_clipStamps[id] = stamp;
            }
        }
        for (const auto& [id, stamp] : m_clipStamps)
        {
            if (!clips.contains(id))
            {
                m_audio.UnloadClip(id);
            }
        }
        for (const auto& id : m_assets)
        {
            if (!graphs.contains(id) && !presets.contains(id) && !invalidAssets.contains(id))
            {
                m_playback.UnregisterAsset(id);
            }
        }
        std::erase_if(m_assets, [&](const ClipKey& id) { return !invalidAssets.contains(id); });
        for (const auto& [id, graph] : graphs)
        {
            m_playback.UnregisterAsset(id);
            if (!m_playback.RegisterGraph(id, graph))
            {
                error = m_playback.LastError();
                return false;
            }
            m_assets.insert(id);
        }
        for (const auto& [id, preset] : presets)
        {
            m_playback.UnregisterAsset(id);
            if (!m_playback.RegisterPreset(id, preset))
            {
                error = m_playback.LastError();
                return false;
            }
            m_assets.insert(id);
        }
        m_clipStamps = std::move(clips);
        m_legacyAliases = std::move(aliases);
        m_editorGraphs = std::move(graphs);
        error = std::move(diagnostics);
        return error.empty();
    }

    bool AudioCatalog::LoadCookedAssets(const ck::CookedAssetCatalog& catalog,
        std::shared_ptr<const ck::ArtifactByteSource> bytes, std::string& error)
    {
        if (!bytes)
        {
            error = "Audio catalog requires a mounted artifact byte source";
            return false;
        }
        std::unordered_map<ClipKey, std::string> clips;
        std::vector<ck::CookedAudioClipSource> clipSources;
        std::unordered_map<ClipKey, SoundGraphDefinition> definitions;
        Graphs graphs;
        Presets presets;
        for (const auto& entry : catalog.Entries())
        {
            const auto id = ClipKey::FromGuid(entry.assetId.value);
            if (entry.kind == ck::CookedAssetKind::AudioClip)
            {
                ck::CookedAudioClipSource clip;
                if (!catalog.OpenAudioClip(entry.assetId, bytes, clip, error))
                {
                    return false;
                }
                clips.emplace(id, entry.artifactPath);
                clipSources.push_back(std::move(clip));
            }
            else if (entry.kind == ck::CookedAssetKind::SoundGraph || entry.kind == ck::CookedAssetKind::SoundPreset)
            {
                std::uint64_t size{};
                const auto expectedPath = entry.kind == ck::CookedAssetKind::SoundGraph
                    ? ck::MakeDerivedSoundGraphArtifactPath(entry.assetId) : ck::MakeDerivedSoundPresetArtifactPath(entry.assetId);
                if (entry.formatVersion != ck::kSoundAssetArtifactVersion || entry.artifactPath != expectedPath)
                {
                    error = "Cooked sound asset metadata/size mismatch: " + id.Text();
                    return false;
                }
                own::shared_owner<const ck::ArtifactByteSource> exactBytes;
                if (!bytes->CaptureArtifact(entry.artifactPath, exactBytes, error))
                {
                    return false;
                }
                const auto& source = exactBytes ? *exactBytes : *bytes;
                if (!source.Size(entry.artifactPath, size, error) || size != entry.byteSize || size > 4u * 1024u * 1024u)
                {
                    error = "Cooked sound asset metadata/size mismatch: " + id.Text();
                    return false;
                }
                std::vector<std::byte> payload(static_cast<std::size_t>(size));
                ck::Sha256Digest digest;
                if (!source.ReadAt(entry.artifactPath, 0u, payload, error)
                    || !ck::ComputeSha256(payload, digest, error) || digest != entry.contentSha256)
                {
                    error = "Cooked sound asset read/hash mismatch: " + id.Text();
                    return false;
                }
                if (entry.kind == ck::CookedAssetKind::SoundGraph)
                {
                    SoundGraphDefinition graph;
                    if (!ReadCookedSoundGraph(payload, graph, error))
                    {
                        return false;
                    }
                    definitions.emplace(id, std::move(graph));
                }
                else
                {
                    SoundPreset preset;
                    if (!ReadCookedSoundPreset(payload, preset, error))
                    {
                        return false;
                    }
                    presets.emplace(id, std::move(preset));
                }
            }
        }
        for (const auto& [id, definition] : definitions)
        {
            auto program = CompileSoundGraph(definition, [&](const ClipKey& clip) { return clips.contains(clip); }, error);
            if (!program)
            {
                return false;
            }
            graphs.emplace(id, std::move(program));
        }
        if (!ValidatePresets(presets, graphs, clips, error))
        {
            return false;
        }
        const auto validateDependencies = [&](const ClipKey& key, std::vector<experiment::AssetId> expected)
        {
            experiment::AssetId id;
            if (!experiment::TryParseCanonicalAssetId(key.Text(), id))
            {
                error = "Cooked sound asset identity is not canonical";
                return false;
            }
            const auto* entry = catalog.Find(id);
            std::ranges::sort(expected);
            expected.erase(std::unique(expected.begin(), expected.end()), expected.end());
            if (!entry || entry->dependencies != expected)
            {
                error = "Cooked sound dependency table disagrees with its program: " + key.Text();
                return false;
            }
            return true;
        };
        for (const auto& [id, graph] : definitions)
        {
            std::vector<experiment::AssetId> dependencies;
            for (const auto& node : graph.nodes)
            {
                if (node.kind == SoundNodeKind::Clip)
                {
                    experiment::AssetId clip;
                    if (!experiment::TryParseCanonicalAssetId(node.clip.Text(), clip))
                    {
                        error = "Cooked SoundGraph clip identity is invalid";
                        return false;
                    }
                    dependencies.push_back(clip);
                }
            }
            if (!validateDependencies(id, std::move(dependencies)))
            {
                return false;
            }
        }
        for (const auto& [id, preset] : presets)
        {
            experiment::AssetId source;
            if (!experiment::TryParseCanonicalAssetId(preset.source.asset.Text(), source)
                || !validateDependencies(id, { source }))
            {
                return false;
            }
        }
        for (const auto& [id, stamp] : clips)
        {
            if (!validateDependencies(id, {}))
            {
                return false;
            }
        }
        Clear();
        for (const auto& clip : clipSources)
        {
            const auto id = ClipKey::FromGuid(clip.Id().value);
            if (!m_audio.LoadCookedClip(clip))
            {
                error = "Audio backend rejected cooked clip: " + id.Text();
                Clear();
                return false;
            }
            m_clipStamps.emplace(id, clips.at(id));
        }
        for (const auto& [id, graph] : graphs)
        {
            m_playback.UnregisterAsset(id);
            if (!m_playback.RegisterGraph(id, graph))
            {
                error = m_playback.LastError();
                Clear();
                return false;
            }
            m_assets.insert(id);
        }
        for (const auto& [id, preset] : presets)
        {
            m_playback.UnregisterAsset(id);
            if (!m_playback.RegisterPreset(id, preset))
            {
                error = m_playback.LastError();
                Clear();
                return false;
            }
            m_assets.insert(id);
        }
        // Player accepts GUIDs exclusively. Legacy aliases are resolved by
        // AssetCooker in scene/prefab artifacts, never from source paths here.
        error.clear();
        return true;
    }

    std::string AudioCatalog::ResolveLegacyClip(std::string_view name, std::string& error) const
    {
        experiment::AssetId id;
        if (experiment::TryParseCanonicalAssetId(name, id))
        {
            if (m_clipStamps.contains(ClipKey::FromGuid(id.value)))
            {
                error.clear();
                return std::string(name);
            }
            error = "Unknown audio clip GUID: " + std::string(name);
            return {};
        }
        const auto alias = m_legacyAliases.find(Lower(std::string(name)));
        if (alias == m_legacyAliases.end() || alias->second.empty())
        {
            error = alias == m_legacyAliases.end() ? "Unknown legacy audio clip: " : "Ambiguous legacy audio basename; select a GUID: ";
            error += name;
            return {};
        }
        error.clear();
        return alias->second;
    }

    void AudioCatalog::Clear()
    {
        for (const auto& id : m_assets)
        {
            m_playback.UnregisterAsset(id);
        }
        for (const auto& [id, stamp] : m_clipStamps)
        {
            m_audio.UnloadClip(id);
        }
        m_assets.clear();
        m_editorGraphs.clear();
        m_clipStamps.clear();
        m_legacyAliases.clear();
    }
}
