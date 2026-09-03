#pragma once

#include "RHIPipelineState.h"
#include "../Assets/ModelVertexLayout.h"
#include "RHIShaderPermutation.h"

#include <string>
#include <vector>

// PHASE 3.75 MBC6 — ModelAssetGeneration 정점 마스크에서 RHI 입력 레이아웃과
// shader permutation을 함께 유도하는 제품 경계.
//
// 정본은 assets::kVertexAttributeTable 하나다: 시맨틱·시맨틱 인덱스·포맷·
// 오프셋 전부 표에서 나오고, 사람이 세는 숫자는 여기 없다. VertexLayout.h가
// RHI 타입을 쓰지 않는다는 규약(임포트 계층이 렌더 백엔드를 모르게)이 있어
// 변환은 반드시 이 렌더 경계에서 한 번 한다.
//
// 전체 레이아웃과 pass가 실제로 읽는 속성을 분리한다. Shadow처럼 POSITION과
// skin만 읽는 pass도 오프셋은 전체 model mask에서 계산해야 COLOR가 사이에 낀
// 84B 레이아웃의 bone 위치를 정확히 찾는다.
namespace ModelVertexInput
{
    [[nodiscard]] bool BuildInputElements(assets::VertexAttributeMask mask,
        std::vector<RHIInputElement>& outElements, std::string& outError);

    [[nodiscard]] bool BuildInputElements(assets::VertexAttributeMask layoutMask,
        assets::VertexAttributeMask consumedMask,
        std::vector<RHIInputElement>& outElements, std::string& outError);

    // PSO descriptor가 빌리는 배열의 주소는 프로세스 수명 동안 안정적이다.
    [[nodiscard]] const std::vector<RHIInputElement>* ResolveInputElements(
        assets::VertexAttributeMask mask, std::string& outError);

    [[nodiscard]] const std::vector<RHIInputElement>* ResolveInputElements(
        assets::VertexAttributeMask layoutMask,
        assets::VertexAttributeMask consumedMask, std::string& outError);

    // MODEL_VERTEX_LAYOUT과 선택 속성 축을 동일한 기술표에서 켠다. 호출부가
    // skin/color를 bool로 축약할 수 없고 전체 mask가 PSO key에 남는다.
    [[nodiscard]] bool ApplyShaderPermutation(assets::VertexAttributeMask mask,
        RHIShaderPermutation& permutation, std::string& outError);

    // 표의 VertexFormat → RHIFormat. 대응이 없으면 Unknown — 호출부가
    // fail-closed로 다룬다(표에 포맷을 더하면 여기도 함께 더한다).
    [[nodiscard]] RHIFormat ToRhiFormat(
        assets::VertexFormat format) noexcept;
}
