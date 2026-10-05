#pragma once

#include "../../Engine/SceneRuntime/Audio/SoundAssetSerialization.h"
#include "Experiment/Cooked/CookedAssetManifest.h"
#include "Experiment/Cooked/SceneCookProducer.h"
#include "Assets/AudioClipSourceMetadata.h"
#include "AuthoringCookedDocument.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace sound_cook
{
    namespace ck = experiment::cooked;

    struct Product final
    {
        ck::CookedAssetManifestEntry entry;
        std::vector<std::byte> bytes;
    };

    inline std::string Lower(std::string value)
    {
        std::ranges::transform(value, value.begin(), [](unsigned char letter)
        {
            return static_cast<char>(letter >= 'A' && letter <= 'Z' ? letter + ('a' - 'A') : letter);
        });
        return value;
    }

    inline bool Build(const std::filesystem::path& root, ck::CookedAssetManifest& manifest,
        std::set<std::string>& artifactPaths, std::vector<Product>& products, std::string& error)
    {
        std::unordered_set<wave::ClipKey> clips;
        std::unordered_map<wave::ClipKey, std::shared_ptr<const wave::SoundGraphProgram>> graphs;
        std::vector<std::pair<experiment::AssetId, wave::SoundPreset>> presets;
        for (const auto& entry : manifest.entries)
        {
            if (entry.kind == ck::CookedAssetKind::AudioClip)
            {
                clips.insert(wave::ClipKey::FromGuid(entry.assetId.value));
            }
        }
        const auto publish = [&](const experiment::AssetId& id, bool graph,
            std::vector<std::byte> bytes, std::vector<experiment::AssetId> dependencies)
        {
            Product product;
            product.entry.assetId = id;
            product.entry.kind = graph ? ck::CookedAssetKind::SoundGraph : ck::CookedAssetKind::SoundPreset;
            product.entry.formatVersion = ck::kSoundAssetArtifactVersion;
            product.entry.artifactPath = graph ? ck::MakeDerivedSoundGraphArtifactPath(id) : ck::MakeDerivedSoundPresetArtifactPath(id);
            product.entry.byteSize = bytes.size();
            product.entry.dependencies = std::move(dependencies);
            product.bytes = std::move(bytes);
            if (product.entry.artifactPath.empty() || !artifactPaths.insert(product.entry.artifactPath).second
                || !ck::ComputeSha256(product.bytes, product.entry.contentSha256, error))
            {
                error = "Invalid or duplicate sound artifact: " + Uuid::ToString(id.value) + " " + error;
                return false;
            }
            manifest.entries.push_back(product.entry);
            products.push_back(std::move(product));
            return true;
        };
        for (const auto& source : manifest.sourceAssets)
        {
            const auto path = root / std::filesystem::path(std::u8string(source.sourcePath.begin(), source.sourcePath.end()));
            const auto extension = Lower(path.extension().string());
            if (extension != ".soundgraph" && extension != ".soundpreset")
            {
                continue;
            }
            std::error_code code;
            const auto size = std::filesystem::file_size(path, code);
            if (code || size > 4u * 1024u * 1024u)
            {
                error = "Sound asset missing/oversized: " + path.string();
                return false;
            }
            std::ifstream input(path, std::ios::binary);
            std::string text(static_cast<std::size_t>(size), '\0');
            if (!input.read(text.data(), static_cast<std::streamsize>(size)))
            {
                error = "Cannot read sound asset: " + path.string();
                return false;
            }
            if (extension == ".soundpreset")
            {
                wave::SoundPreset preset;
                if (!wave::ReadSoundPreset(text, preset, error))
                {
                    error = path.string() + ": " + error;
                    return false;
                }
                presets.emplace_back(source.assetId, std::move(preset));
                continue;
            }
            wave::SoundGraphDefinition definition;
            if (!wave::ReadSoundGraph(text, definition, error))
            {
                error = path.string() + ": " + error;
                return false;
            }
            auto program = wave::CompileSoundGraph(definition,
                [&](const wave::ClipKey& clip) { return clips.contains(clip); }, error);
            if (!program)
            {
                error = path.string() + ": " + error;
                return false;
            }
            std::vector<experiment::AssetId> dependencies;
            for (const auto& node : definition.nodes)
            {
                if (node.kind == wave::SoundNodeKind::Clip)
                {
                    experiment::AssetId id;
                    if (!experiment::TryParseCanonicalAssetId(node.clip.Text(), id))
                    {
                        error = "SoundGraph clip reference is not canonical";
                        return false;
                    }
                    if (std::ranges::find(dependencies, id) == dependencies.end())
                    {
                        dependencies.push_back(id);
                    }
                }
            }
            std::vector<std::byte> bytes;
            if (!wave::CookSoundGraph(definition, bytes, error)
                || !publish(source.assetId, true, std::move(bytes), std::move(dependencies)))
            {
                return false;
            }
            graphs.emplace(wave::ClipKey::FromGuid(source.assetId.value), std::move(program));
        }
        for (const auto& [id, preset] : presets)
        {
            if (preset.source.kind == wave::SoundSourceKind::Clip)
            {
                if (!clips.contains(preset.source.asset) || !preset.parameters.empty())
                {
                    error = "SoundPreset clip is missing or clip preset has graph parameters";
                    return false;
                }
            }
            else
            {
                const auto graph = graphs.find(preset.source.asset);
                wave::ParameterMap resolved;
                if (preset.source.kind != wave::SoundSourceKind::Graph || graph == graphs.end()
                    || !wave::ResolveGraphParameters(*graph->second, preset.parameters, resolved, error))
                {
                    error = "SoundPreset graph/parameters are invalid: " + Uuid::ToString(id.value) + " " + error;
                    return false;
                }
            }
            experiment::AssetId dependency;
            if (!experiment::TryParseCanonicalAssetId(preset.source.asset.Text(), dependency))
            {
                error = "SoundPreset source identity is invalid";
                return false;
            }
            std::vector<std::byte> bytes;
            if (!wave::CookSoundPreset(preset, bytes, error)
                || !publish(id, false, std::move(bytes), { dependency }))
            {
                return false;
            }
        }
        return true;
    }

    inline bool RewriteSceneNode(Authoring::WriteNode node, const ck::CookedAssetManifest& manifest,
        const std::unordered_map<std::string, std::string>& aliases,
        std::vector<experiment::AssetId>& dependencies, std::string& error, std::uint32_t depth = 0u,
        bool soundComponentContext = false)
    {
        if (depth > 256u)
        {
            error = "Scene audio reference nesting exceeds limit";
            return false;
        }
        const auto read = node.Read();
        if (read.IsMap())
        {
            // UUID is authoritative when present. The named header is the
            // legacy component identity; matching field names alone are not.
            constexpr std::string_view soundType = "f2441c9e-234b-42cd-8067-2276a3c985fe";
            const auto type = read["m_typeUUID"];
            const bool soundComponent = soundComponentContext || (type && !type.Scalar().empty()
                ? type.Scalar() == soundType : read["SoundComponent"].IsScalar());
            const auto property = read["m_propertyName"].AsString();
            if (read["m_componentType"].Scalar() == "SoundComponent"
                && (property == "clipKey" || property == "soundPresetKey" || property == "soundGraphKey" || property == "sourceKind")
                && read["m_valueYaml"].Scalar().starts_with(Authoring::kCookedDocumentTextEnvelopePrefix))
            {
                auto value = Authoring::DecodeCookedDocumentTextEnvelope(read["m_valueYaml"].Scalar(), error);
                if (!value || !value->Root().Read().IsScalar())
                {
                    error = "SoundComponent prefab override must contain a scalar asset reference";
                    return false;
                }
                Authoring::WriteDocument reference;
                reference.Root().Child(property).SetString(value->Root().Read().Scalar());
                if (!RewriteSceneNode(reference.Root(), manifest, aliases, dependencies, error, depth + 1u, true))
                {
                    return false;
                }
                value->Root().SetString(reference.Root().Read()[property.c_str()].Scalar());
                std::string envelope;
                if (!Authoring::EncodeCookedDocumentTextEnvelope(value->Root().Read(), envelope, error))
                {
                    return false;
                }
                node.Child("m_valueYaml").SetString(envelope);
            }
            if (soundComponent)
            {
                const auto sourceKind = read["sourceKind"];
                if (sourceKind && sourceKind.Scalar() != "Clip" && sourceKind.Scalar() != "Preset"
                    && sourceKind.Scalar() != "Graph" && sourceKind.Scalar() != "0"
                    && sourceKind.Scalar() != "1" && sourceKind.Scalar() != "2")
                {
                    error = "SoundComponent sourceKind is invalid";
                    return false;
                }
                constexpr const char* keys[]{ "clipKey", "soundPresetKey", "soundGraphKey" };
                constexpr ck::CookedAssetKind kinds[]{ ck::CookedAssetKind::AudioClip,
                    ck::CookedAssetKind::SoundPreset, ck::CookedAssetKind::SoundGraph };
                for (std::size_t index = 0; index < 3u; ++index)
                {
                    const auto value = read[keys[index]];
                    if (!value)
                    {
                        continue;
                    }
                    if (!value.IsScalar())
                    {
                        error = "SoundComponent asset reference must be a scalar GUID";
                        return false;
                    }
                    if (value.Scalar().empty())
                    {
                        continue;
                    }
                    std::string text = value.AsString();
                    experiment::AssetId id;
                    if (!experiment::TryParseCanonicalAssetId(text, id) && index == 0u)
                    {
                        const auto alias = aliases.find(Lower(text));
                        if (alias == aliases.end() || alias->second.empty())
                        {
                            error = "Missing or ambiguous legacy audio basename: " + text;
                            return false;
                        }
                        text = alias->second;
                    }
                    if (!experiment::TryParseCanonicalAssetId(text, id))
                    {
                        error = "SoundComponent reference must be a canonical GUID: " + text;
                        return false;
                    }
                    const auto entry = manifest.Find(id);
                    if (!entry || entry->kind != kinds[index])
                    {
                        error = "SoundComponent reference is missing or has wrong asset kind: " + text;
                        return false;
                    }
                    node.Child(keys[index]).SetScalar(text);
                    if (std::ranges::find(dependencies, id) == dependencies.end())
                    {
                        dependencies.push_back(id);
                    }
                }
            }
            // Gather names before edits; WriteNode::Child may relocate ryml storage.
            std::vector<std::string> names;
            for (const auto item : node.Read().Map())
            {
                names.push_back(item.key.AsString());
            }
            for (const auto& name : names)
            {
                if (!RewriteSceneNode(node.Child(name), manifest, aliases, dependencies, error, depth + 1u))
                {
                    return false;
                }
            }
        }
        else if (read.IsSequence())
        {
            for (std::size_t index = 0; index < node.Size(); ++index)
            {
                if (!RewriteSceneNode(node.At(index), manifest, aliases, dependencies, error, depth + 1u))
                {
                    return false;
                }
            }
        }
        return true;
    }

    inline bool RewriteScenes(std::vector<ck::SceneCookProduct>& scenes,
        ck::CookedAssetManifest& manifest, std::string& error)
    {
        std::ranges::sort(manifest.entries, {}, &ck::CookedAssetManifestEntry::assetId);
        std::unordered_map<std::string, std::string> aliases;
        for (const auto& source : manifest.sourceAssets)
        {
            const auto entry = manifest.Find(source.assetId);
            if (!entry || entry->kind != ck::CookedAssetKind::AudioClip)
            {
                continue;
            }
            const auto stemUtf8 = std::filesystem::path(
                std::u8string(source.sourcePath.begin(), source.sourcePath.end())).stem().u8string();
            const auto stem = Lower(std::string(stemUtf8.begin(), stemUtf8.end()));
            const auto guid = Uuid::ToString(source.assetId.value);
            const auto [found, inserted] = aliases.emplace(stem, guid);
            if (!inserted && found->second != guid)
            {
                found->second.clear();
            }
        }
        for (auto& scene : scenes)
        {
            auto document = Authoring::DecodeCookedDocument(scene.artifactBytes, error);
            if (!document || !RewriteSceneNode(document->Root(), manifest, aliases, scene.manifestEntry.dependencies, error)
                || !Authoring::EncodeCookedDocument(document->Root().Read(), scene.artifactBytes, error)
                || !ck::ComputeSha256(scene.artifactBytes, scene.manifestEntry.contentSha256, error))
            {
                error = scene.artifactPath + ": " + error;
                return false;
            }
            scene.manifestEntry.byteSize = scene.artifactBytes.size();
            const auto entry = std::ranges::find_if(manifest.entries, [&](const auto& value)
            {
                return value.assetId == scene.sceneAssetId;
            });
            if (entry == manifest.entries.end())
            {
                error = "Scene audio rewrite lost its manifest identity";
                return false;
            }
            *entry = scene.manifestEntry;
        }
        return true;
    }
}
