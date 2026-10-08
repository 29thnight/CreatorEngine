#pragma once

#include "Experiment/Cooked/CookedAssetManifest.h"
#include "Experiment/Cooked/CookSupport.h"
#include "Experiment/Cooked/SceneCookProducer.h"
#include "AuthoringCookedDocument.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace font_cook
{
    namespace ck = experiment::cooked;

    inline std::string Normalize(std::string text)
    {
        std::ranges::transform(text, text.begin(), [](unsigned char letter)
        {
            if (letter == '\\')
            {
                return '/';
            }
            return static_cast<char>(letter >= 'A' && letter <= 'Z' ? letter + ('a' - 'A') : letter);
        });
        return text;
    }

    struct References final
    {
        std::filesystem::path assetRoot;
        std::unordered_map<std::string, std::string> paths;
        std::unordered_map<std::string, std::string> ids;
    };

    inline bool Resolve(std::string text, const References& references, std::string& output, std::string& error)
    {
        output.clear();
        if (text.empty())
        {
            return true; // The deployed engine Inter face and Korean fallback.
        }
        if (text.size() == 36u && text[8] == '-' && text[13] == '-'
            && text[18] == '-' && text[23] == '-')
        {
            // The runtime FileGuid parser accepts hexadecimal case; serialize
            // its canonical lowercase form so cooking and direct loading agree.
            Uuid::Uuid16 parsed{};
            if (Uuid::TryParse(text, parsed))
            {
                const auto canonical = Uuid::ToString(parsed);
                if (references.ids.contains(canonical))
                {
                    output = canonical;
                    return true;
                }
            }
            error = "Font reference has no registered TTF/OTF source identity: " + text;
            return false;
        }
        const std::filesystem::path path(std::u8string(text.begin(), text.end()));
        if (path.is_absolute())
        {
            const auto relative = path.lexically_relative(references.assetRoot);
            if (relative.empty() || relative.is_absolute() || !ck::IsContainedPath(references.assetRoot, path))
            {
                error = "Font path is outside the packaged Assets root; select the registered project font again: " + text;
                return false;
            }
            const auto utf8 = relative.generic_u8string();
            text.assign(utf8.begin(), utf8.end());
        }
        else
        {
            for (const auto& component : path)
            {
                if (component == "..")
                {
                    error = "Font path escapes the packaged Assets root: " + text;
                    return false;
                }
            }
            const auto utf8 = path.lexically_normal().generic_u8string();
            text.assign(utf8.begin(), utf8.end());
        }
        const std::string normalized = Normalize(text);
        // A leading ./ is still a qualified Assets-root request at runtime.
        // Preserve that distinction while normalizing its redundant segments.
        const bool basename = !path.is_absolute() && !path.has_parent_path();
        const std::vector<std::string> candidates = basename
            ? std::vector<std::string>{ "fonts/" + normalized, "font/" + normalized }
            : std::vector<std::string>{ normalized };
        for (const auto& candidate : candidates)
        {
            const auto found = references.paths.find(candidate);
            if (found != references.paths.end())
            {
                output = found->second;
                return true;
            }
        }
        // Preserve only known, deployed resource aliases. Never silently replace
        // an unresolved project directory with a same-named runtime font.
        if (normalized == "inter-regular.ttf" || normalized == "fonts/runtime/inter-regular.ttf")
        {
            output = "Fonts/Runtime/Inter-Regular.ttf";
            return true;
        }
        if (normalized == "nanumgothic-regular.ttf" || normalized == "fonts/runtime/nanumgothic-regular.ttf")
        {
            output = "Fonts/Runtime/NanumGothic-Regular.ttf";
            return true;
        }
        error = "Font reference has no registered TTF/OTF source identity: " + text;
        return false;
    }

    inline bool RewriteReference(Authoring::WriteNode node, const References& references, std::string& error)
    {
        if (!node.Read().IsScalar())
        {
            error = "Font reference must be a scalar asset GUID or project font path";
            return false;
        }
        std::string reference;
        if (!Resolve(node.Read().AsString(), references, reference, error))
        {
            return false;
        }
        node.SetString(reference);
        return true;
    }

    inline bool RewriteNode(Authoring::WriteNode node, const References& references,
        std::string& error, std::uint32_t depth = 0u)
    {
        if (depth > 256u)
        {
            error = "Scene font reference nesting exceeds limit";
            return false;
        }
        if (node.Read().IsMap())
        {
            constexpr std::string_view textType = "8b8f7463-847a-4a4d-8362-fc40a2a1d243";
            const auto read = node.Read();
            const auto type = read["m_typeUUID"];
            const bool textComponent = type && !type.Scalar().empty()
                ? type.Scalar() == textType : read["TextComponent"].IsScalar();
            if (textComponent && read["fontPath"])
            {
                if (!RewriteReference(node.Child("fontPath"), references, error))
                {
                    return false;
                }
            }
            const auto current = node.Read();
            if (current["assetTypeID"].Scalar() == "3" && current["assetName"])
            {
                if (!RewriteReference(node.Child("assetName"), references, error))
                {
                    return false;
                }
            }
            const auto overrideNode = node.Read();
            if (overrideNode["m_componentType"].Scalar() == "TextComponent"
                && overrideNode["m_propertyName"].Scalar() == "fontPath"
                && overrideNode["m_valueYaml"].Scalar().starts_with(Authoring::kCookedDocumentTextEnvelopePrefix))
            {
                const auto envelope = overrideNode["m_valueYaml"].AsString();
                auto value = Authoring::DecodeCookedDocumentTextEnvelope(envelope, error);
                if (!value || !RewriteReference(value->Root(), references, error))
                {
                    return false;
                }
                std::string rewritten;
                if (!Authoring::EncodeCookedDocumentTextEnvelope(value->Root().Read(), rewritten, error))
                {
                    return false;
                }
                node.Child("m_valueYaml").SetString(rewritten);
            }
            // Writes may relocate document storage; take names, not ReadNodes.
            std::vector<std::string> names;
            for (const auto item : node.Read().Map())
            {
                names.push_back(item.key.AsString());
            }
            for (const auto& name : names)
            {
                if (!RewriteNode(node.Child(name), references, error, depth + 1u))
                {
                    return false;
                }
            }
        }
        else if (node.Read().IsSequence())
        {
            for (std::size_t index = 0; index < node.Size(); ++index)
            {
                if (!RewriteNode(node.At(index), references, error, depth + 1u))
                {
                    return false;
                }
            }
        }
        return true;
    }

    inline bool RewriteScenes(std::vector<ck::SceneCookProduct>& scenes, ck::CookedAssetManifest& manifest,
        const std::filesystem::path& assetRoot, std::string& error)
    {
        References references{ assetRoot, {}, {} };
        for (const auto& source : manifest.sourceAssets)
        {
            const auto path = Normalize(source.sourcePath);
            if (!path.ends_with(".ttf") && !path.ends_with(".otf"))
            {
                continue;
            }
            const auto guid = Uuid::ToString(source.assetId.value);
            references.paths.emplace(path, guid);
            references.ids.emplace(guid, path);
        }
        for (auto& scene : scenes)
        {
            auto document = Authoring::DecodeCookedDocument(scene.artifactBytes, error);
            if (!document || !RewriteNode(document->Root(), references, error)
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
                error = "Scene font rewrite lost its manifest identity";
                return false;
            }
            // Raw fonts live in CEMF's source identity table. Do not manufacture
            // cooked-artifact dependency edges for these pass-through payloads.
            *entry = scene.manifestEntry;
        }
        return true;
    }
}
