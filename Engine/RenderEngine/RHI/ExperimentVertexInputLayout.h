#pragma once

#include "RHIPipelineState.h"
#include "../Experiment/VertexLayout.h"

#include <string>
#include <vector>

// I5-D2 (V4) — experiment 정점 마스크에서 RHI 입력 레이아웃을 유도한다.
//
// 정본은 experiment::kVertexAttributeTable 하나다: 시맨틱·시맨틱 인덱스·포맷·
// 오프셋 전부 표에서 나오고, 사람이 세는 숫자는 여기 없다. VertexLayout.h가
// RHI 타입을 쓰지 않는다는 규약(임포트 계층이 렌더 백엔드를 모르게)이 있어
// 변환은 반드시 이 렌더 경계에서 한 번 한다.
//
// ★ 호출부는 아직 게이트뿐이고 그 사실을 숨기지 않는다(S0 코덱과 같은 선례).
//   입력 레이아웃 5곳의 실전 전환은 정점 버퍼가 experiment로 바뀌는 순간
//   (I5-D4 — DX12MeshCache가 96B legacy를 올리는 동안 레이아웃만 바꾸면
//   버퍼와 어긋난다)과 동시일 수밖에 없음이 D2 실측이다.
namespace ExperimentVertexInput
{
    // 지원 레이아웃 규칙(core 필수·skin all-or-nothing)은
    // experiment::VertexBuffer::IsSupportedLayout이 정본이다 — 여기서 같은
    // 규칙을 다시 쓰지 않고 그대로 위임한다. 위반은 fail-closed.
    [[nodiscard]] bool BuildInputElements(experiment::VertexAttributeMask mask,
        std::vector<RHIInputElement>& outElements, std::string& outError);

    // 표의 VertexFormat → RHIFormat. 대응이 없으면 Unknown — 호출부가
    // fail-closed로 다룬다(표에 포맷을 더하면 여기도 함께 더한다).
    [[nodiscard]] RHIFormat ToRhiFormat(
        experiment::VertexFormat format) noexcept;
}
