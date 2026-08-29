#pragma once

#include "ModelData.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace experiment
{
    // I5-M3 — base 저작 정본 + 인스턴스별 override. MeshRenderer가 소유하는
    // 비영속 runtime 상태다.
    //
    // ★ legacy `Material::InstantiateShared` 계약을 승계하지 않는다:
    //   - runtime clone을 `DataSystem::Materials` 같은 asset cache에 등록하지
    //     않는다 — 이 타입에는 등록 경로 자체가 없다.
    //   - 원본 asset identity를 인스턴스의 것으로 복사하지 않는다. 효과
    //     머테리얼이 base의 assetId/shaderAssetId를 지니는 것은 shader·texture
    //     **해석**을 위해서지, 인스턴스가 그 자산이라는 뜻이 아니다.
    //   - 독립 `.asset`으로 저장하지 않는다. 새 저작 자산이 필요하면 그것은
    //     `DuplicateMaterialAsset`(catalog가 새 AssetId/.meta 발급)의 몫이다.
    //
    // override는 이름 기반 논리 값이다. texture 교체도 TextureReference 값
    // override로 표현된다 — 별도 texture 슬롯 API를 만들지 않는다. 값의 타입
    // 검증은 packer/resolver가 fail-closed로 담당한다(여기서는 meta를 모른다).
    //
    // keyword override는 base.keywords 뒤에 덧붙는다. resolver가 목록 순서대로
    // 축 선택을 덮으므로 같은 축의 override가 base를 이긴다.
    class MaterialInstance final
    {
    public:
        MaterialInstance() = default;
        explicit MaterialInstance(std::shared_ptr<const Material> base)
            : base_(std::move(base))
        {
        }

        [[nodiscard]] const std::shared_ptr<const Material>& Base() const noexcept
        {
            return base_;
        }

        // 변경 관측용 — M4 sealing이 무변경 인스턴스의 재밀봉을 건너뛸 수 있게
        // Set/Clear마다 증가한다. 0은 "아직 아무 변경 없음"이다.
        [[nodiscard]] std::uint64_t Revision() const noexcept { return revision_; }

        [[nodiscard]] std::span<const MaterialProperty>
            PropertyOverrides() const noexcept
        {
            return propertyOverrides_;
        }

        [[nodiscard]] std::span<const std::string>
            KeywordOverrides() const noexcept
        {
            return keywordOverrides_;
        }

        // 같은 이름은 항목을 갱신한다(중복 축적 금지). 빈 이름은 거부.
        bool SetPropertyOverride(std::string_view name,
            MaterialPropertyValue value);
        bool ClearPropertyOverride(std::string_view name);

        // 같은 값 이름은 축적하지 않고 뒤로 보낸다 — resolver는 나중 항목이
        // 이긴다. 빈 이름은 거부.
        bool AddKeywordOverride(std::string_view keywordValue);
        bool ClearKeywordOverride(std::string_view keywordValue);

        void ClearAllOverrides();

        // base 위에 override를 겹친 **완전 소유 사본**. base는 변형하지 않는다.
        // base가 없으면 false다 — 빈 머테리얼을 지어내지 않는다.
        [[nodiscard]] bool BuildEffectiveMaterial(Material& outMaterial,
            std::string& outError) const;

    private:
        std::shared_ptr<const Material> base_{};
        std::vector<MaterialProperty> propertyOverrides_{};
        std::vector<std::string> keywordOverrides_{};
        std::uint64_t revision_{};
    };
}
