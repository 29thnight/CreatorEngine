#pragma once

#include "ModelData.h"
#include "../ShaderMetaHandle.h"
#include "TypeTrait.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct ShaderMeta;
struct ShaderKeywordAxis;
class Texture;

namespace experiment
{
    // I5-M2 — experiment::Material의 안정 ID를 런타임 generation으로 해석한다.
    //   shaderAssetId(GUID)      → ShaderMetaHandle + ShaderMeta generation
    //   TextureReference(GUID)   → texture generation owner (cooked 우선, source 폴백)
    //   keywords/keywordSelections → ShaderMeta 축 순서의 정규화된 선택
    //
    // ★ Experiment 경계 유지 — DataSystem singleton에 접근하지 않는다. 정본
    //   구현은 서비스로 주입받는다(제품 바인딩: ExperimentMaterialResolveBinding).
    //   게이트는 가짜 서비스를 꽂아 호출 계수로 cooked/source 순서를 잰다.
    //
    // ★ 해석은 fail-closed다. 저작이 가리키는 GUID가 어느 쪽으로도 해석되지
    //   않거나 로드가 실패하면 조용히 건너뛰지 않는다 — nil assetId만
    //   "텍스처 없음"이다. legacy FinalizeMaterialRuntime의 이름 폴백은
    //   승계하지 않는다(D5-c 이주로 GUID가 정본이 됐고, TextureReference의
    //   fallbackPath는 진단용이다).
    struct MaterialResolveServices final
    {
        // GUID → ShaderMeta generation. 정본: DataSystem::LoadShaderMetaHandle.
        std::function<ShaderMetaHandle(const FileGuid&, std::string&)>
            loadShaderMetaHandle{};
        // handle → 불변 스냅샷. 정본: DataSystem::ResolveShaderMeta.
        std::function<std::shared_ptr<const ShaderMeta>(const ShaderMetaHandle&)>
            resolveShaderMeta{};
        // 경로 → texture generation owner. 정본: DataSystem::LoadSharedMaterialTexture.
        std::function<std::shared_ptr<Texture>(const std::filesystem::path&, bool)>
            loadTexture{};
        // GUID → cooked artifact 경로. 정본: CookedAssetCatalog::ResolveArtifactPath.
        // 비어 있으면(catalog 부재) source만 쓴다.
        std::function<std::filesystem::path(const AssetId&)>
            resolveCookedArtifactPath{};
        // GUID → 원본 경로. 정본: AssetMetaRegistry(DataSystem::GetFilePath).
        std::function<std::filesystem::path(const FileGuid&)> resolveSourcePath{};
    };

    struct ResolvedMaterialTexture final
    {
        std::string propertyName{};
        AssetId assetId{};
        std::shared_ptr<Texture> owner{};
        bool fromCookedArtifact{};
    };

    // 폴백은 관측 가능해야 한다 — cooked가 늘 비고 조용히 source로 도는 상태는
    // "느리지만 동작하는" 모습이라 아무도 알아채지 못한다(D5-c-1과 같은 처방).
    struct ResolvedMaterialNotes final
    {
        std::size_t cookedTextures{};
        std::size_t sourceFallbackTextures{};
    };

    struct ResolvedMaterial final
    {
        AssetId assetId{};
        ShaderMetaHandle shaderMetaHandle{};
        std::shared_ptr<const ShaderMeta> shaderMeta{};
        // ShaderMeta 축 순서 기준 정규화 결과. 이름 기반 keywords가 정본이고
        // 인덱스 keywordSelections는 보조다(ModelData.h의 저작 계약 그대로).
        std::vector<std::uint16_t> keywordSelections{};
        std::vector<ResolvedMaterialTexture> textures{};
        ResolvedMaterialNotes notes{};
    };

    [[nodiscard]] bool ResolveMaterial(const Material& material,
        const MaterialResolveServices& services,
        ResolvedMaterial& outResolved, std::string& outError);

    // keyword 정규화 단독 경계 — ResolveMaterial과 M4 sealing이 같은 규칙을
    // 공유한다(두 번째 정규화 구현 금지). 이름 기반 keywords가 정본으로 인덱스
    // 선택을 덮고, 모호/미지/범위 밖은 fail-closed다.
    [[nodiscard]] bool NormalizeMaterialKeywordSelections(const Material& material,
        const std::vector<ShaderKeywordAxis>& axes,
        std::vector<std::uint16_t>& outSelections, std::string& outError);
}
