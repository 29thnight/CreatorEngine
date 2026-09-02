#include "ModelCookIdentity.h"
#include "AuthoringParsedDocument.h"

#include <algorithm>
#include <unordered_set>
#include <utility>

namespace experiment::cooked
{
    namespace
    {
        void AddIssue(std::vector<ModelIdentityIssue>& issues,
            ModelIdentityIssueCode code, std::string context,
            std::string message)
        {
            issues.push_back(ModelIdentityIssue{
                code, std::move(context), std::move(message) });
        }

		[[nodiscard]] bool ReadScalar(const Authoring::ReadNode& parent,
            const char* key, const std::string& context, std::string& out,
            std::vector<ModelIdentityIssue>& issues, bool required = true)
        {
			const Authoring::ReadNode node = parent[key];
            if (!node)
            {
                if (required)
                {
                    AddIssue(issues, ModelIdentityIssueCode::MissingField,
                        context + "." + key, "필수 scalar가 없다.");
                }
                return !required;
            }
            if (!node.IsScalar())
            {
                AddIssue(issues, ModelIdentityIssueCode::InvalidDocument,
                    context + "." + key, "scalar여야 한다.");
                return false;
            }

			out = node.AsString();
			return true;
		}

		[[nodiscard]] bool ReadGuid(const Authoring::ReadNode& parent,
            const char* key, const std::string& context, AssetId& out,
            std::vector<ModelIdentityIssue>& issues)
        {
            std::string text;
            if (!ReadScalar(parent, key, context, text, issues)) return false;
            if (!TryParseCanonicalAssetId(text, out))
            {
                AddIssue(issues, ModelIdentityIssueCode::InvalidAssetId,
                    context + "." + key,
                    "소문자 canonical UUIDv4 asset identity여야 한다.");
                return false;
            }
            return true;
        }

        [[nodiscard]] bool ContainsId(const std::vector<AssetId>& ids,
            const AssetId& id) noexcept
        {
            return std::ranges::find(ids, id) != ids.end();
        }

		[[nodiscard]] bool ReadSubAssets(const Authoring::ReadNode& sequence,
            const std::string& context,
            std::vector<ModelSubAssetIdentity>& out,
            std::vector<AssetId>& usedIds,
            std::unordered_set<std::string>& usedKeys,
            std::vector<ModelIdentityIssue>& issues)
        {
            if (!sequence || !sequence.IsSequence())
            {
                AddIssue(issues, sequence
                    ? ModelIdentityIssueCode::InvalidDocument
                    : ModelIdentityIssueCode::MissingField,
                    context, "subasset sequence가 필요하다.");
                return false;
            }

            bool valid = true;
			out.reserve(sequence.Size());
			for (std::size_t index = 0; index < sequence.Size(); ++index)
			{
				const Authoring::ReadNode node = sequence.At(index);
                const std::string itemContext =
                    context + "[" + std::to_string(index) + "]";
                if (!node.IsMap())
                {
                    AddIssue(issues, ModelIdentityIssueCode::InvalidDocument,
                        itemContext, "subasset entry는 map이어야 한다.");
                    valid = false;
                    continue;
                }

                ModelSubAssetIdentity item;
                bool itemValid = ReadScalar(node, "key", itemContext,
                    item.sourceKey, issues);
                if (item.sourceKey.empty())
                {
                    AddIssue(issues, ModelIdentityIssueCode::MissingField,
                        itemContext + ".key", "빈 source key는 허용하지 않는다.");
                    itemValid = false;
                }
                else if (!usedKeys.emplace(item.sourceKey).second)
                {
                    AddIssue(issues, ModelIdentityIssueCode::DuplicateSourceKey,
                        itemContext + ".key", "source key가 sidecar 안에서 중복됐다.");
                    itemValid = false;
                }

                itemValid = ReadScalar(node, "name", itemContext,
                    item.name, issues, false) && itemValid;
                itemValid = ReadGuid(node, "guid", itemContext,
                    item.assetId, issues) && itemValid;
                if (item.assetId.IsValid())
                {
                    if (ContainsId(usedIds, item.assetId))
                    {
                        AddIssue(issues, ModelIdentityIssueCode::DuplicateAssetId,
                            itemContext + ".guid",
                            "model/subasset UUIDv4가 sidecar 안에서 충돌했다.");
                        itemValid = false;
                    }
                    else
                    {
                        usedIds.push_back(item.assetId);
                    }
                }

                if (itemValid) out.push_back(std::move(item));
                valid = itemValid && valid;
            }
            return valid;
        }

        template <typename SourceRange, typename Include>
        [[nodiscard]] bool ValidateKeys(const SourceRange& source,
            const std::vector<ModelSubAssetIdentity>& identities,
            const std::string& context,
            std::vector<ModelIdentityIssue>& issues, Include include)
        {
            bool valid = true;
            std::unordered_set<std::string> sourceKeys;
            for (std::size_t index = 0; index < source.size(); ++index)
            {
                const auto& item = source[index];
                if (!include(item)) continue;
                if (item.sourceKey.empty())
                {
                    AddIssue(issues, ModelIdentityIssueCode::SourceMismatch,
                        context + ".source[" + std::to_string(index) + "]",
                        "importer가 source key를 게시하지 않았다.");
                    valid = false;
                    continue;
                }
                if (!sourceKeys.emplace(item.sourceKey).second)
                {
                    AddIssue(issues, ModelIdentityIssueCode::DuplicateSourceKey,
                        context + ".source[" + std::to_string(index) + "]",
                        "import 결과의 source key가 중복됐다.");
                    valid = false;
                }
                const bool found = std::ranges::any_of(identities,
                    [&](const ModelSubAssetIdentity& identity)
                    {
                        return identity.sourceKey == item.sourceKey;
                    });
                if (!found)
                {
                    AddIssue(issues, ModelIdentityIssueCode::SourceMismatch,
                        context + ".source[" + std::to_string(index) + "]",
                        "현재 source key에 대응하는 sidecar UUIDv4가 없다.");
                    valid = false;
                }
            }

            for (std::size_t index = 0; index < identities.size(); ++index)
            {
                if (!sourceKeys.contains(identities[index].sourceKey))
                {
                    AddIssue(issues, ModelIdentityIssueCode::SourceMismatch,
                        context + ".sidecar[" + std::to_string(index) + "]",
                        "source에서 사라진 stale subasset identity다.");
                    valid = false;
                }
            }
            return valid;
        }
    }

    AssetId ModelCookIdentity::FindMaterial(
        std::string_view sourceKey) const noexcept
    {
        const auto found = std::ranges::find(materials, sourceKey,
            &ModelSubAssetIdentity::sourceKey);
        return found == materials.end() ? AssetId{} : found->assetId;
    }

    AssetId ModelCookIdentity::FindEmbeddedTexture(
        std::string_view sourceKey) const noexcept
    {
        const auto found = std::ranges::find(embeddedTextures, sourceKey,
            &ModelSubAssetIdentity::sourceKey);
        return found == embeddedTextures.end() ? AssetId{} : found->assetId;
    }

	bool ReadAssetIdFromMeta(std::string_view yaml, AssetId& outAssetId,
		std::vector<ModelIdentityIssue>& outIssues)
	{
		std::string parseError;
		const Authoring::ParsedDocument document =
			Authoring::ParsedDocument::ParseText(std::string(yaml), parseError);
		if (!document)
		{
			AddIssue(outIssues, ModelIdentityIssueCode::InvalidDocument,
				"meta", parseError);
			return false;
		}
		const Authoring::ReadNode root = document.Root();
		if (!root.IsMap())
		{
			AddIssue(outIssues, ModelIdentityIssueCode::InvalidDocument,
				"meta", "sidecar root는 map이어야 한다.");
			return false;
		}

		AssetId parsed{};
		if (!ReadGuid(root, "guid", "meta", parsed, outIssues)) return false;
		outAssetId = parsed;
		return true;
	}

	bool ReadModelCookIdentity(std::string_view yaml,
		ModelCookIdentity& outIdentity,
		std::vector<ModelIdentityIssue>& outIssues)
	{
		std::string parseError;
		const Authoring::ParsedDocument document =
			Authoring::ParsedDocument::ParseText(std::string(yaml), parseError);
		if (!document)
		{
			AddIssue(outIssues, ModelIdentityIssueCode::InvalidDocument,
				"meta", parseError);
			return false;
		}
		try
		{
			const Authoring::ReadNode root = document.Root();
            if (!root.IsMap())
            {
                AddIssue(outIssues, ModelIdentityIssueCode::InvalidDocument,
                    "meta", "sidecar root는 map이어야 한다.");
                return false;
            }

            ModelCookIdentity parsed;
            bool valid = ReadGuid(root, "guid", "meta",
                parsed.modelAssetId, outIssues);

			const Authoring::ReadNode subAssets = root["subAssets"];
            if (!subAssets || !subAssets.IsMap())
            {
                AddIssue(outIssues, subAssets
                    ? ModelIdentityIssueCode::InvalidDocument
                    : ModelIdentityIssueCode::MissingField,
                    "meta.subAssets", "subAssets map이 필요하다.");
                return false;
            }

			const Authoring::ReadNode schema = subAssets["schemaVersion"];
            if (!schema || !schema.IsScalar())
            {
                AddIssue(outIssues, schema
                    ? ModelIdentityIssueCode::InvalidDocument
                    : ModelIdentityIssueCode::MissingField,
                    "meta.subAssets.schemaVersion", "schemaVersion scalar가 필요하다.");
                valid = false;
            }
            else
            {
				try
				{
					if (schema.As<std::uint32_t>() != 1u)
					{
						AddIssue(outIssues, ModelIdentityIssueCode::InvalidDocument,
							"meta.subAssets.schemaVersion",
							"지원하는 model subasset schemaVersion은 1이다.");
						valid = false;
					}
				}
				catch (const std::exception& exception)
				{
					AddIssue(outIssues, ModelIdentityIssueCode::InvalidDocument,
						"meta.subAssets.schemaVersion", exception.what());
					valid = false;
				}
            }

            std::vector<AssetId> usedIds;
            if (parsed.modelAssetId.IsValid()) usedIds.push_back(parsed.modelAssetId);
            std::unordered_set<std::string> usedKeys;
            valid = ReadSubAssets(subAssets["materials"],
                "meta.subAssets.materials", parsed.materials,
                usedIds, usedKeys, outIssues) && valid;
            valid = ReadSubAssets(subAssets["embeddedTextures"],
                "meta.subAssets.embeddedTextures", parsed.embeddedTextures,
                usedIds, usedKeys, outIssues) && valid;

            if (!valid) return false;
            outIdentity = std::move(parsed);
            return true;
        }
		catch (const std::exception& exception)
        {
            AddIssue(outIssues, ModelIdentityIssueCode::InvalidDocument,
                "meta", exception.what());
            return false;
        }
    }

    bool ValidateModelCookIdentity(const importer::ImportedScene& scene,
        const ModelCookIdentity& identity,
        std::vector<ModelIdentityIssue>& outIssues)
    {
        bool valid = IsAssetIdV4(identity.modelAssetId);
        if (!valid)
        {
            AddIssue(outIssues, ModelIdentityIssueCode::InvalidAssetId,
                "identity.modelAssetId", "model identity가 UUIDv4가 아니다.");
        }

        valid = ValidateKeys(scene.materials, identity.materials,
            "materials", outIssues,
            [](const importer::ImportedMaterial&) { return true; }) && valid;
        valid = ValidateKeys(scene.textures, identity.embeddedTextures,
            "embeddedTextures", outIssues,
            [](const importer::ImportedTexture& texture)
            {
                return texture.IsEmbedded();
            }) && valid;
        return valid;
    }
}
