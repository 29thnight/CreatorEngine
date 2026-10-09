// Unrun source fixture. A dedicated engine host mounts a source-free v3 set
// containing the supplied authored/code and graph roots, runs its normal job
// scheduler, calls Begin once and Poll once per owner tick. No pending wait,
// shader compilation or source fixture creation belongs in this probe.
#include "../../Engine/RenderEngine/DataSystem.h"
#include "../../Engine/RenderEngine/Experiment/MaterialResolver.h"
#include "../../Engine/RenderEngine/Experiment/MaterialInstance.h"
#include "../../Engine/RenderEngine/Experiment/MaterialPropertyBlock.h"
#include "../../Engine/RenderEngine/Experiment/Cooked/CookedCodeMaterial.h"
#include "../../Engine/RenderEngine/LXMaterialPipeline.h"

#include <stdexcept>
#include <array>

namespace code_material_runtime_probe
{
    namespace
    {
        void Require(bool value, const char* message)
        {
            if (!value)
            {
                throw std::runtime_error(message);
            }
        }

        void VerifyAxisIndexedKeywordOverrides()
        {
            auto base = own::make_shared<experiment::Material>();
            base->keywordSelections = { 0u, 0u, 0u };
            base->keywords = { "HIGH" };
            experiment::MaterialInstance instance(base);
            const std::array<std::uint16_t, 3u> selections{ 1u, 0u, 0u };
            instance.SetKeywordSelectionOverrides(selections);
            experiment::Material effective;
            std::string error;
            Require(instance.BuildEffectiveMaterial(effective, error), "Numeric keyword view could not be built");
            const std::vector<ShaderKeywordAxis> axes{
                { "FIRST", { "OFF", "ON" } }, { "SECOND", { "OFF", "ON" } },
                { "QUALITY", { "LOW", "HIGH" } }
            };
            std::vector<std::uint16_t> resolved;
            Require(effective.keywords.empty()
                && experiment::NormalizeMaterialKeywordSelections(effective, axes, resolved, error)
                && std::ranges::equal(resolved, selections), "Axis-index overrides became ambiguous keyword names");
            Require(base->keywordSelections == std::vector<std::uint16_t>{ 0u, 0u, 0u }
                && base->keywords == std::vector<std::string>{ "HIGH" }, "Numeric overrides mutated the shared base");
            Require(instance.ClearKeywordSelectionOverrides()
                && instance.BuildEffectiveMaterial(effective, error)
                && effective.keywordSelections == base->keywordSelections && effective.keywords == base->keywords,
                "Clearing numeric overrides did not restore the authored base");
            instance.SetKeywordSelectionOverrides(selections);
            instance.ClearAllOverrides();
            Require(instance.BuildEffectiveMaterial(effective, error)
                && effective.keywordSelections == base->keywordSelections && effective.keywords == base->keywords,
                "ClearAllOverrides left numeric selections installed");
        }
    }

    class Driver final
    {
    public:
        void Begin(DataSystem& data, experiment::AssetId authoredMaterial,
            experiment::AssetId codeProgram, experiment::AssetId graphMaterial,
            experiment::AssetId graphProgram, experiment::AssetId metadata)
        {
            VerifyAxisIndexedKeywordOverrides();
            m_before = RHIShaderCompiler::GetStats();
            m_materialLink = { { authoredMaterial, {} } };
            m_programLink = { { codeProgram, {} } };
            data.SetCodeMaterialAssetCacheBudgets(0u, 0u);
            data.SetMaterialAssetCacheBudgets(0u, 0u);
            m_authored = data.RequestAsync(m_materialLink);
            auto cancelled = data.RequestAsync(m_materialLink);
            cancelled.Cancel();
            m_facade = data.RequestAsync(AssetDepot::AssetLink<Material>{ m_materialLink.identity });
            m_program = data.RequestAsync(m_programLink);
            m_metadata = data.RequestAsync(AssetDepot::AssetLink<ShaderMeta>{ { metadata, {} } });

            const auto checkWrongView = [](const auto& request)
            {
                const auto result = request.Snapshot();
                Require(result.status == AssetDepot::AssetRequestStatus::Failed
                    && result.error == AssetDepot::AssetRequestError::UnsupportedRepresentation,
                    "Manifest kind was accepted as concrete C++ representation proof");
            };
            checkWrongView(data.RequestAsync(AssetDepot::AssetLink<material_graph::Generation>{ m_programLink.identity }));
            checkWrongView(data.RequestAsync(AssetDepot::AssetLink<LX::Runtime::ShaderGeneration>{ { graphProgram, {} } }));
            checkWrongView(data.RequestAsync(AssetDepot::AssetLink<experiment::Material>{ { graphMaterial, {} } }));
        }

        bool Poll(DataSystem& data)
        {
            const auto authored = m_authored.Snapshot();
            const auto facade = m_facade.Snapshot();
            const auto program = m_program.Snapshot();
            const auto metadata = m_metadata.Snapshot();
            if (authored.status == AssetDepot::AssetRequestStatus::Pending
                || facade.status == AssetDepot::AssetRequestStatus::Pending
                || program.status == AssetDepot::AssetRequestStatus::Pending
                || metadata.status == AssetDepot::AssetRequestStatus::Pending)
            {
                return false;
            }
            Require(authored.status == AssetDepot::AssetRequestStatus::Ready && authored.asset,
                "Source-free authored Material did not become Ready");
            Require(facade.status == AssetDepot::AssetRequestStatus::Ready && facade.asset,
                "Source-free prepared Material facade did not become Ready");
            Require(program.status == AssetDepot::AssetRequestStatus::Ready && program.asset,
                "Source-free code program did not become Ready");
            Require(metadata.status == AssetDepot::AssetRequestStatus::Ready && metadata.asset,
                "Independent CPU metadata descriptor did not become Ready");
            m_pin = authored.asset;
            m_programPin = program.asset;
            Require(m_pin->assetOrigin && m_pin->assetOrigin->codeProgram
                && std::addressof(*m_pin->assetOrigin->codeProgram) == std::addressof(*m_programPin),
                "Authored Material did not keep the deduplicated exact code-program owner");
            const auto instance = facade.asset->GetLXMaterialInstance();
            Require(instance && !facade.asset->HasMaterialGraph() && instance->shader
                && std::addressof(*instance->shader) == std::addressof(*m_programPin),
                "Code facade lost its exact prepared program");
            Require(!metadata.asset->codeProgram, "Metadata descriptor invented shader readiness");
            own::shared_owner<const LX::Runtime::ShaderGeneration> invalid;
            std::string error;
            Require(!LX::Runtime::CreateCodeShader(*metadata.asset, m_programPin->layout,
                m_programPin->codeHandle, invalid, error), "Descriptor-only metadata compiled or prepared a shader");
            VerifyPinnedResolution();
            const auto stats = RHIShaderCompiler::GetStats();
            Require(stats.compiles == m_before.compiles && stats.diskHits == m_before.diskHits
                && stats.memoryHits == m_before.memoryHits,
                "Cooked code preparation accessed the source compiler/cache");
            Require(data.SnapshotMaterialAssetCache().codePrograms.retainedChargeBytes == 0u
                && data.SnapshotMaterialAssetCache().authoredMaterials.retainedChargeBytes == 0u,
                "Zero retained budget kept hidden material/program roots");
            return true;
        }

        // The host logically unmounts/replaces the set before calling this. Old
        // strong owners remain usable; current links cannot resurrect them.
        void VerifyAfterUnmount(DataSystem& data)
        {
            Require(!data.TryAcquire(m_materialLink) && !data.TryAcquire(m_programLink),
                "Current links resurrected an unmounted generation");
            VerifyPinnedResolution();
        }

        // Supply a native-sRGB cooked facade whose texture sampling variant
        // remains Source (including a generated-mip projection of that source).
        void VerifyNativeSrgbRefRebuild(DataSystem& data, const Material& original,
            std::string_view propertyName)
        {
            const auto texture = original.GetTextureMapShared(propertyName);
            const auto origin = texture ? texture->GetAssetOrigin() : nullptr;
            Require(origin && origin->variant.colorSpace == AssetDepot::TextureAssetColorSpace::Source,
                "Native-sRGB fixture must retain its Source sampling variant");
            const auto format = texture->GetImageDescription().Format();
            Require(format == RHIFormat::RGBA8UnormSrgb || format == RHIFormat::BGRA8UnormSrgb
                || format == RHIFormat::BC1UnormSrgb || format == RHIFormat::BC3UnormSrgb,
                "Native-sRGB fixture has a linear descriptor format");
            Material clone(original);
            std::string error;
            Require(data.RebuildCookedMaterialInstance(clone, error), "Cooked ref rebuild failed");
            const auto rebuilt = clone.GetTextureMapShared(propertyName);
            Require(rebuilt && rebuilt->GetImageDescription().Format() == format
                && rebuilt->GetAssetOrigin()->imageKey.recipe.mipColorSpace == origin->imageKey.recipe.mipColorSpace,
                "Ref rebuild converted Source-sRGB sampling or mip filtering to linear");
        }

        void VerifyPreparedTextureOverride(const own::shared_owner<const Texture>& raw,
            std::string_view propertyName)
        {
            Require(m_pin && raw && raw->GetAssetOrigin(), "Missing explicit prepared override source");
            const auto id = raw->GetAssetOrigin()->resolved.entry.asset.key.assetId;
            const auto desc = std::ranges::find(m_programPin->meta.properties, propertyName, &ShaderPropertyDesc::name);
            Require(desc != m_programPin->meta.properties.end() && desc->type == ShaderPropertyType::Texture2D,
                "Override fixture must name a real texture property");
            std::string error;
            auto misleading = Texture::WithColorSpace(raw, true);
            misleading = Texture::WithMipChain(misleading, error);
            const std::array supplied{ misleading, raw };
            for (const auto color : {experiment::TextureColorSpace::Linear, experiment::TextureColorSpace::Srgb})
            {
                experiment::Material value = *m_pin;
                experiment::TextureReference reference;
                reference.assetId = id;
                reference.colorSpace = color;
                const auto existing = std::ranges::find(value.properties, propertyName, &experiment::MaterialProperty::name);
                if (existing == value.properties.end())
                {
                    value.properties.push_back({ std::string(propertyName), reference });
                }
                else
                {
                    existing->value = reference;
                }
                Require(experiment::BindPreparedMaterialTextures(value, supplied, error),
                    "Exact raw source descriptor was not selected from prepared variants");
                experiment::ResolvedMaterial resolved;
                Require(experiment::ResolveMaterial(value, {}, resolved, error), "Typed color override failed to resolve");
                const auto texture = std::ranges::find(resolved.textures, propertyName, &experiment::ResolvedMaterialTexture::propertyName);
                Require(texture != resolved.textures.end() && texture->owner, "Prepared override owner was lost");
                const auto origin = texture->owner->GetAssetOrigin();
                if (origin->imageKey.recipe.mipPolicy == AssetDepot::TextureMipPolicy::GenerateFull)
                {
                    Require(origin->imageKey.recipe.mipColorSpace == (color == experiment::TextureColorSpace::Srgb
                        ? AssetDepot::TextureAssetColorSpace::Srgb : AssetDepot::TextureAssetColorSpace::Linear),
                        "A different material's mip-filtering recipe leaked into this color override");
                }
                // Ref-style callers pass their still-typed overrides to rebuilding;
                // the dedicated host separately exercises serialized scene refs.
                std::vector<std::uint8_t> propertyBytes;
                Require(experiment::BuildMaterialPropertyBlock(value, m_programPin->meta, m_programPin->layout,
                    propertyBytes, error), "Property-only packing rejected a valid cooked program handle");
            }
        }

    private:
        void VerifyPinnedResolution()
        {
            std::size_t sourceCalls{};
            experiment::MaterialResolveServices poison;
            poison.resolveSourcePath = [&](const FileGuid&)
            {
                ++sourceCalls;
                return std::filesystem::path{};
            };
            experiment::ResolvedMaterial resolved;
            std::string error;
            Require(m_pin && experiment::ResolveMaterial(*m_pin, poison, resolved, error),
                "Pinned authored closure requires source/latest lookup");
            Require(sourceCalls == 0u && resolved.shaderMeta && resolved.shaderMeta->codeProgram,
                "Pinned material invoked a source fallback");
            for (const auto& variant : m_programPin->codeProgram->variants)
            {
                if (variant.backend != RHIShaderCompiler::GetOutput()
                    || variant.stages.size() != 2u)
                {
                    continue;
                }
                LX::Runtime::CompiledGraphics graphics;
                Require(LX::Runtime::RestoreCodeGraphics(m_programPin->meta, variant.passIndex,
                    variant.keywordSelections, variant.vertexAttributeMask, variant.referencePath, graphics, error),
                    "Carried graphics variant could not be restored without source");
                Require(graphics.vertex.bytecode.IsValid() && graphics.pixel.bytecode.IsValid()
                    && !graphics.identity.sealedProgramIdentity.empty(), "Carried code stages lost their exact identity");
            }
        }

        RHIShaderCompiler::Stats m_before{};
        AssetDepot::AssetLink<experiment::Material> m_materialLink{};
        AssetDepot::AssetLink<LX::Runtime::ShaderGeneration> m_programLink{};
        AssetDepot::AssetRequest<experiment::Material> m_authored{};
        AssetDepot::AssetRequest<Material> m_facade{};
        AssetDepot::AssetRequest<LX::Runtime::ShaderGeneration> m_program{};
        AssetDepot::AssetRequest<ShaderMeta> m_metadata{};
        own::shared_owner<const experiment::Material> m_pin{};
        own::shared_owner<const LX::Runtime::ShaderGeneration> m_programPin{};
    };
}
