#pragma once

#include "Render/Core/RenderFeatureContributor.h"

// 에디터 씬 뷰 오버레이(Grid → WireFrame → GizmoIcon → GizmoLine)를 라이브
// 파이프라인에 기여한다(E4-2). RenderCore는 이 패스들을 조립하지 않는다 —
// Player는 이 기여자를 설치하지 않으므로 노드 자체가 서지 않는다.
//
// 상태가 없다. Contribute 호출마다 새 패스 묶음을 만들어 노드 람다에 실으므로
// 파이프라인 둘(DX12·Vulkan)이 공존해도, 리사이즈 재구축이 이어져도 인스턴스가
// 섞이지 않고, 이 객체가 파이프라인보다 먼저 사라져도 안전하다.
class EditorSceneOverlayContributor final : public IRenderFeatureContributor
{
public:
    void Contribute(LivePipelineDesc& pipeline,
        const RenderFeatureContext& context) override;
};
