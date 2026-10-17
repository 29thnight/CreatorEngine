# 렌더링 통합 계획의 책임 분리

> 기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스 정적 조사. 이 문서의 재작성에서 빌드·실행·성능 측정은 하지 않았다.

## 이 경로의 역할

과거 Phase 4 umbrella는 여러 시점의 구현·공수·수용을 한 파일에 누적했다. 현재 순서와 계획의 정본은 [RenderPhaseRoadmap](RenderPhaseRoadmap.md), [RenderPhaseEffortEstimate](RenderPhaseEffortEstimate.md), 목표 구조는 [RenderPipelineTargetArchitecture](../design/RenderPipelineTargetArchitecture.md)다. 이 파일은 옛 링크를 위한 책임 경계 설명이며 중복 완료율 원장이 아니다.

## 실제로 분리된 계약

EnhancedSceneRenderer가 제품 pipeline을 조립하고 LivePipelineDesc가 publish/read/modify 순서를 검증한다. EnhancedRenderGraph는 자원 version·dependency·recording을 다룬다. MaterialGraphScene은 geometry/material admission과 pass 입력을, RHI는 device·queue·resource state를, Player presentation은 표시·소비 수명을 담당한다.

현재 Scene 경로의 METEOR Geometry→Cull.First→Occluder→Cull.Final→Surface와 독립 Shadow 소비를 옛 “향후 GPU 전환” 문구로 덮지 않는다. 동시에 구현된 node가 quality·성능·backend parity까지 완료했다는 뜻은 아니다. exact graph version, submitted completion과 current/history identity가 실제 연결 조건이다.

## 계획을 닫는 증거

저작 SRP, 재질, temporal reconstruction, GPU geometry, DXR, lighting·postprocessing, backend parity는 서로 다른 제품 결과를 가진다. 공통 graph/fence 기반은 한 번 구현하고 각 기능이 소비하지만, 공통 기반의 수용이 모든 소비자의 수용을 대신하지 않는다.

진행 수치와 earned days는 현재 정본에서 관리한다. 오래된 병합·검증 보고의 수치는 해당 revision의 역사로만 남긴다. 이 재작성은 구현을 읽어 경계를 복원했으며 phase 완료 판정이나 GPU 실행을 추가하지 않았다.

## 구현 근거

- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp](../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp)
- [Engine/RenderEngine/Render/Core/EnhancedLivePipelineDesc.cpp](../../Engine/RenderEngine/Render/Core/EnhancedLivePipelineDesc.cpp)
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h](../../Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h)
- [Engine/RenderEngine/MaterialGraphGeometryContract.h](../../Engine/RenderEngine/MaterialGraphGeometryContract.h)
- [Player/PlayerPresentation.h](../../Player/PlayerPresentation.h)
