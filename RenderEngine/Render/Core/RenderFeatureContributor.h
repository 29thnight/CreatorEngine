#pragma once

#include "../../RHI/RHIFormat.h"

class LivePipelineDesc;
struct EnhancedGizmoSceneData;

// 렌더 기능 기여 경계 — EngineLayerSeparationPlan E4(§4.4)의 기여 지점.
//
// Host(Editor)가 파이프라인 조립 시점에 자기 노드를 끼워 넣는다. RenderCore는
// 구체 Editor 패스를 조립하지 않고, 기여자가 노드와 패스 인스턴스 수명을
// 소유한다. Player는 기여자를 설치하지 않으므로 노드 자체가 서지 않는다.
//
// 파이프라인은 백엔드별로 따로 서고 리사이즈마다 다시 서므로 Contribute도
// 그때마다 다시 불린다. 기여자는 호출마다 새 패스 묶음을 만들어 노드 람다에
// shared_ptr로 실어야 한다 — 그래야 파이프라인 둘이 공존해도 인스턴스가
// 섞이지 않고, desc.Clear()가 노드와 함께 묶음을 놓는다(ShutdownAll 뒤).
struct RenderFeatureContext
{
    /// 기여 노드가 그릴 최종 표시 표적의 포맷. 입력 텍스처에 직접 그리는
    /// 구조라 PSO의 RTV 포맷을 여기 맞춰야 한다 — 어긋나면 커맨드 리스트
    /// 무효 → 디바이스 제거다(실측).
    RHIFormat ldrFormat{};

    /// 프레임마다 갱신되는 기즈모 씬 데이터의 안정 주소. 렌더러 상태가
    /// 프로세스 수명이라 파이프라인보다 오래 산다. 소비자가 하나뿐이므로
    /// 범용 사이드밴드 채널은 만들지 않는다 — 둘째가 생기면 그때 일반화한다.
    const EnhancedGizmoSceneData* gizmoScene{ nullptr };
};

struct IRenderFeatureContributor
{
    virtual ~IRenderFeatureContributor() = default;

    /// UI(런타임 화면 UI) 노드 뒤, live_present(표시 복사) 앞에서 불린다.
    /// AddNode 순서가 곧 실행 순서다. 조립은 RenderThread에서 일어나므로
    /// 구현은 공유 가변 상태 없이 호출 지역 상태만 써야 한다.
    virtual void Contribute(LivePipelineDesc& pipeline,
        const RenderFeatureContext& context) = 0;
};
