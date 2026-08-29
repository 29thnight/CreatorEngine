#include "MaterialResolver.h"

#include "../ShaderMeta.h"
#include "../StandardMaterialProperty.h"

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

    bool ResolveMaterial(const Material& material,
        const MaterialResolveServices& services,
        ResolvedMaterial& outResolved, std::string& outError)
    {
        outResolved = {};
        if (!services.loadShaderMetaHandle || !services.resolveShaderMeta
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
        const ShaderMetaHandle handle =
            services.loadShaderMetaHandle(shaderGuid, loadError);
        if (!handle.IsValid())
        {
            outError = "ShaderMeta handle 해석 실패: "
                + (loadError.empty() ? shaderGuid.ToString() : loadError);
            return false;
        }
        std::shared_ptr<const ShaderMeta> meta = services.resolveShaderMeta(handle);
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

            // 압축 결정은 legacy 패리티다 — FinalizeMaterialRuntime이
            // baseColorMap만 compress한다. colorSpace(sRGB) 기반 승격은 emissive
            // 압축 여부를 바꾸므로 M4 픽셀 대조와 함께 판정한다.
            const bool compress =
                property.name == standard_material::property::BaseColorMap;
            std::shared_ptr<Texture> owner = services.loadTexture(path, compress);
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
