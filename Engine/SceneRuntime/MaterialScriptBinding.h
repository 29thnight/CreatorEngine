#pragma once

#include "TypeTrait.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

#include <mathematics/color.hpp>

class Material;
struct ShaderMeta;

// I5-M5 S3 — CLR property API의 논리 값 경로.
//
// legacy TrySetValue는 RuntimeSchema 설치(ConfigureShaderProperties)와 CB 이름
// 일치에 기댔다. 여기는 둘 다 기대지 않는다 — 검증 기준은 ShaderMeta 선언
// (shaderMetaGuid → desc.type)이고, 갱신 대상은 이름 기반 논리 값이다. M4 이후
// sealing이 매 프레임 논리 값에서 CB bytes를 다시 pack하므로 논리 값 갱신이
// 곧 화면 갱신이다. 오타/타입 불일치는 false로 드러난다(조용히 삼키지 않는다).
//
// C# ABI(P/Invoke 시그니처)는 ClrHost가 유지하고, 이 경계는 그 구현만 바꾼다.
namespace MaterialScriptBinding
{
    // 검증 코어 — meta를 명시로 받는다(합성 meta로 검사 가능).
    [[nodiscard]] bool SetFloat(Material& material, const ShaderMeta& meta,
        std::string_view name, float value);
    [[nodiscard]] bool SetInt(Material& material, const ShaderMeta& meta,
        std::string_view name, std::int32_t value);

    // Float/Float2/Float3/Float4 공통 — 성분 수가 desc.type과 정확히 맞아야
    // 한다(Inspector 동적 편집기의 단일 진입점).
    [[nodiscard]] bool SetFloatVector(Material& material, const ShaderMeta& meta,
        std::string_view name, std::span<const float> values);

    // 읽기 — 논리 값 우선, 없으면 fallback(legacy 스칼라 등 호출자 몫).
    [[nodiscard]] float GetFloat(const Material& material,
        std::string_view name, float fallback);

    // texture GUID 논리 값 갱신 — Inspector 드롭 슬롯용. legacy
    // TrySetTextureGuid와 달리 RuntimeSchema가 필요 없다. nil은 "텍스처 없음"
    // 저작이다. 이름은 호출부의 표준 슬롯 상수라 meta 검증을 두지 않는다.
    void SetTexture(Material& material, std::string_view name,
        const FileGuid& guid);

    // 제품 표면 — material의 shaderMetaGuid로 meta를 해석해 코어에 위임한다.
    // meta 해석 실패(빈 guid 포함)는 false다.
    [[nodiscard]] bool SetFloat(Material& material, std::string_view name,
        float value);
    [[nodiscard]] bool SetInt(Material& material, std::string_view name,
        std::int32_t value);

    // baseColor는 논리 값이 정본이고 m_materialInfo는 legacy 스칼라 소비자용
    // 사본이다(Forward snapshot 호환 필드) — Set이 둘을 함께 갱신하고, Get은
    // 논리 값 우선·사본 폴백이다.
    [[nodiscard]] math::color GetBaseColor(const Material& material);
    void SetBaseColor(Material& material, const math::color& color);

    // legacy InstantiateShared 계약 비승계: 클론을 DataSystem::Materials에
    // 등록하지 않는다 — 인스턴스는 자산 캐시의 시민이 아니고, 수명은
    // MeshRenderer의 shared_ptr가 진다. 이름 유니킹도 없다(캐시 키가 아니다).
    //
    // ★ m_fileGuid는 아직 승계한다 — MeshRenderer::OnDeserialized가 mesh 해석에
    //   material의 m_fileGuid를 재사용하는 족쇄(S2c에서 분리) 때문에, 지금
    //   지우면 씬 재저장 후 재로드에서 메시가 사라진다.
    [[nodiscard]] std::shared_ptr<Material> InstantiateOwned(
        const Material& origin, std::string_view newName);
}
