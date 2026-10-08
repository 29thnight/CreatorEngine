#include "MaterialResolver.h"

#include "../ShaderMeta.h"
#include "../StandardMaterialProperty.h"
#include "../AssetDepot/MaterialAssetRuntime.h"
#include "../Texture.h"

#include <algorithm>

namespace experiment
{
    namespace
    {
        [[nodiscard]] FileGuid ToFileGuid(const AssetId& id)
        {
            FileGuid guid{};
            guid.m_guid = id.value;
            return guid;
        }
    }

    bool NormalizeMaterialKeywordSelections(const Material& material,
        const std::vector<ShaderKeywordAxis>& axes,
        std::vector<std::uint16_t>& outSelections, std::string& outError)
    {
        if (material.keywordSelections.size() > axes.size())
        {
            outError = "keyword 선택 수가 ShaderMeta 축 수를 넘는다: "
                + material.name;
            return false;
        }
        std::vector<std::uint16_t> selections(axes.size(), 0);
        for (std::size_t axis = 0; axis < material.keywordSelections.size(); ++axis)
        {
            if (material.keywordSelections[axis] >= axes[axis].values.size())
            {
                outError = "keyword 선택 인덱스가 축 범위를 벗어난다: "
                    + axes[axis].name;
                return false;
            }
            selections[axis] = material.keywordSelections[axis];
        }
        for (const std::string& keyword : material.keywords)
        {
            if (keyword.empty())
            {
                outError = "빈 keyword 문자열이 있다: " + material.name;
                return false;
            }
            std::size_t matchedAxis = axes.size();
            std::uint16_t matchedValue = 0;
            for (std::size_t axis = 0; axis < axes.size(); ++axis)
            {
                const auto& values = axes[axis].values;
                const auto found = std::find(values.begin(), values.end(), keyword);
                if (found == values.end()) continue;
                if (matchedAxis != axes.size())
                {
                    // 같은 값 이름이 두 축에 있으면 어느 축을 저작했는지 알 수
                    // 없다. 짐작해 고르면 화면이 조용히 달라진다.
                    outError = "keyword 값이 여러 축에 있어 모호하다: " + keyword;
                    return false;
                }
                matchedAxis = axis;
                matchedValue = static_cast<std::uint16_t>(found - values.begin());
            }
            if (matchedAxis == axes.size())
            {
                outError = "ShaderMeta 어느 축에도 없는 keyword다: " + keyword;
                return false;
            }
            selections[matchedAxis] = matchedValue;
        }
        outSelections = std::move(selections);
        outError.clear();
        return true;
    }

    bool BindPreparedMaterialTextures(Material& material,
        std::span<const own::shared_owner<const Texture>> textures, std::string& outError)
    {
        if (!material.assetOrigin)
        {
            outError.clear();
            return true;
        }
        own::shared_owner<AssetDepot::MaterialDocumentAssetOrigin> extended;
        for (const auto& property : material.properties)
        {
            const auto* reference = std::get_if<TextureReference>(&property.value);
            if (!reference || !reference->assetId.IsValid()
                || std::ranges::any_of(material.assetOrigin->textures,
                    [&](const auto& pin) { return pin.assetId == reference->assetId; }))
            {
                continue;
            }
            const auto found = std::ranges::find_if(textures, [&](const auto& texture)
            {
                const auto origin = texture ? texture->GetAssetOrigin() : nullptr;
                return origin && origin->resolved.entry.asset.key.assetId == reference->assetId
                    && origin->resolved.resolverRevision == material.assetOrigin->resolved.resolverRevision
                    && origin->variant == AssetDepot::TextureAssetVariant{}
                    && origin->imageKey.recipe.mipPolicy == AssetDepot::TextureMipPolicy::PreserveAuthored;
            });
            if (found == textures.end())
            {
                outError = "Runtime texture override lacks an explicit untransformed descriptor from the captured scene snapshot: " + property.name;
                return false;
            }
            if (!extended)
            {
                extended = own::make_shared<AssetDepot::MaterialDocumentAssetOrigin>(*material.assetOrigin);
            }
            if (std::ranges::none_of(extended->textures, [&](const auto& pin) { return pin.assetId == reference->assetId; }))
            {
                extended->textures.push_back({ reference->assetId, LX::LXColorSpace::Data, *found });
            }
        }
        if (extended)
        {
            material.assetOrigin = std::move(extended);
        }
        outError.clear();
        return true;
    }

    bool ResolveMaterial(const Material& material,
        const MaterialResolveServices& services,
        ResolvedMaterial& outResolved, std::string& outError)
    {
        outResolved = {};
        if (material.assetOrigin)
        {
            const auto& origin = *material.assetOrigin;
            const auto& program = origin.codeProgram;
            const auto metadata = program && program->assetOrigin ? program->assetOrigin->shaderMetadata : nullptr;
            if (!program || !program->codeProgram || !program->codeHandle.IsValid() || !metadata
                || metadata->guid.m_guid != material.shaderAssetId.value)
            {
                outError = "Cooked authored Material has no exact prepared code generation.";
                return false;
            }
            ResolvedMaterial candidate;
            candidate.assetId = material.assetId;
            candidate.shaderMetaHandle = program->codeHandle;
            candidate.shaderMeta = metadata;
            if (!NormalizeMaterialKeywordSelections(material, metadata->keywords, candidate.keywordSelections, outError))
            {
                return false;
            }
            for (const auto& property : metadata->properties)
            {
                if (property.type != ShaderPropertyType::Texture2D)
                {
                    continue;
                }
                const auto found = std::ranges::find(material.properties, property.name, &MaterialProperty::name);
                AssetId id;
                auto color = property.colorSpace == "srgb" ? TextureColorSpace::Srgb : TextureColorSpace::Linear;
                if (found != material.properties.end())
                {
                    const auto* reference = std::get_if<TextureReference>(&found->value);
                    if (!reference)
                    {
                        outError = "Cooked texture property has an incompatible value: " + property.name;
                        return false;
                    }
                    id = reference->assetId;
                    color = reference->colorSpace;
                }
                else if (const auto* defaultId = std::get_if<FileGuid>(&property.defaultValue))
                {
                    id.value = defaultId->m_guid;
                }
                if (!id.IsValid())
                {
                    continue;
                }
                const auto pin = std::ranges::find(origin.textures, id, &AssetDepot::MaterialAssetTexturePin::assetId);
                if (pin == origin.textures.end() || !pin->owner)
                {
                    outError = "Cooked texture override is outside the pinned material closure: " + property.name;
                    return false;
                }
                auto owner = Texture::WithColorSpace(pin->owner, color == TextureColorSpace::Srgb);
                if (owner)
                {
                    owner = Texture::WithMipChain(owner, outError);
                }
                if (!owner)
                {
                    return false;
                }
                candidate.textures.push_back({ property.name, id, std::move(owner), true, true });
                ++candidate.notes.generationTextures;
            }
            outResolved = std::move(candidate);
            outError.clear();
            return true;
        }
        if (!services.loadShaderMetaOwner
            || !services.loadTexture || !services.resolveSourcePath)
        {
            outError = "MaterialResolveServices가 불완전하다";
            return false;
        }

        // ── 1. shaderAssetId → ShaderMeta generation ─────────────────────
        if (!material.shaderAssetId.IsValid())
        {
            outError = "experiment material shaderAssetId가 nil이다: "
                + material.name;
            return false;
        }
        const FileGuid shaderGuid = ToFileGuid(material.shaderAssetId);
        std::string loadError;
        ShaderMetaHandle handle;
        const auto meta = services.loadShaderMetaOwner(shaderGuid, handle, loadError);
        if (!handle.IsValid())
        {
            outError = "ShaderMeta handle 해석 실패: "
                + (loadError.empty() ? shaderGuid.ToString() : loadError);
            return false;
        }
        if (!meta)
        {
            outError = "ShaderMeta generation resolve 실패 — handle이 낡았다: "
                + shaderGuid.ToString();
            return false;
        }
        if (meta->guid != shaderGuid)
        {
            // 서비스가 엉뚱한 meta를 돌려주면 이후 모든 해석이 조용히 틀린다.
            outError = "resolve된 ShaderMeta GUID가 shaderAssetId와 다르다: "
                + meta->guid.ToString() + " != " + shaderGuid.ToString();
            return false;
        }

        // ── 2. keyword 정규화 — 이름이 정본, 인덱스는 보조 ────────────────
        std::vector<std::uint16_t> selections;
        if (!NormalizeMaterialKeywordSelections(material, meta->keywords,
            selections, outError))
        {
            return false;
        }

        // ── 3. texture GUID → generation owner (cooked 우선, source 폴백) ─
        ResolvedMaterialNotes notes{};
        std::vector<ResolvedMaterialTexture> textures;
        for (const MaterialProperty& property : material.properties)
        {
            const auto* reference = std::get_if<TextureReference>(&property.value);
            if (!reference) continue;
            if (!reference->assetId.IsValid()) continue; // nil = 텍스처 없음

            // MBC7 — 모델 generation closure가 첫 축이다. embedded texture는
            // 파일이 없어 경로 해석이 원리적으로 실패하던 자리였다(Gunner 콜드
            // 로드가 이름 폴백으로 떨어진 결함). closure에 없으면 예전 그대로.
            if (services.resolveEmbeddedTexture)
            {
                if (own::shared_owner<const Texture> owner =
                    services.resolveEmbeddedTexture(reference->assetId))
                {
                    ++notes.generationTextures;
                    textures.push_back({ property.name, reference->assetId,
                        std::move(owner), false, true });
                    continue;
                }
            }

            std::filesystem::path path;
            bool fromCooked = false;
            if (services.resolveCookedArtifactPath)
            {
                path = services.resolveCookedArtifactPath(reference->assetId);
                fromCooked = !path.empty();
            }
            if (path.empty())
                path = services.resolveSourcePath(ToFileGuid(reference->assetId));
            if (path.empty())
            {
                outError = "texture GUID가 cooked/source 어느 쪽으로도 해석되지"
                    " 않는다: " + property.name;
                return false;
            }

            const bool compress = reference->colorSpace == TextureColorSpace::Srgb
                && property.name == standard_material::property::BaseColorMap;
            own::shared_owner<const Texture> owner = services.loadTexture(
                path, compress, reference->colorSpace);
            if (!owner)
            {
                outError = "texture 로드 실패: " + property.name + " ← "
                    + path.string();
                return false;
            }
            if (fromCooked) ++notes.cookedTextures;
            else ++notes.sourceFallbackTextures;
            textures.push_back({ property.name, reference->assetId,
                std::move(owner), fromCooked });
        }

        outResolved.assetId = material.assetId;
        outResolved.shaderMetaHandle = handle;
        outResolved.shaderMeta = std::move(meta);
        outResolved.keywordSelections = std::move(selections);
        outResolved.textures = std::move(textures);
        outResolved.notes = notes;
        outError.clear();
        return true;
    }
}
