# 옛 Phase 4.75 항목을 나누는 기준

> 기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스 정적 조사. 이 문서의 재작성에서 빌드·실행·성능 측정은 하지 않았다.

## 분류 결과의 현행 해석

이전 22행·95.5일 표는 분리 전 계획의 snapshot이었다. 현재 작업 목록과 공수는 [RenderPhaseRoadmap](../plans/RenderPhaseRoadmap.md)과 [RenderPhaseEffortEstimate](../plans/RenderPhaseEffortEstimate.md)에서 관리한다. 이 문서는 과거 번호를 현재 구현 상태나 일정으로 재계산하지 않는다.

## 세 종류의 선행

구현 선행은 필요한 데이터·API가 없으면 소비자가 동작할 수 없는 관계다. 예를 들어 visibility indirect buffer의 producer가 같은 recording에 선언되어야 Surface consumer가 읽을 수 있다. 판정 선행은 개발 자체가 아니라 품질 비교 기준을 고정하기 위한 관계다. 재질의 pre-tone 기준선과 display/postprocess 평가가 이에 해당한다. 순서 선호는 인력·운영 결정이며 source 의존성으로 포장하지 않는다.

Scene pipeline의 Shadow는 Geometry를 독립 소비한다. 따라서 reflection 기능의 완료가 Shadow 구현의 보편적인 필수 선행이라고 단정할 수 없다. temporal history와 display lease는 reconstruction 소비자에게 실제 수명 계약을 제공하지만 그 자체가 SDK별 품질 수용은 아니다.

## 독립적으로 남길 완료선

SRP authoring, lightmap bake, reflection/occlusion 품질, shadow 품질, display/postprocess, GPU visibility, ray tracing은 자신의 실패·fallback·quality·성능 gate를 갖는다. graph와 queue/fence 기반은 공유하되 같은 작업을 여러 phase의 기성으로 중복 계산하지 않는다. 기능별 지원 backend는 실제 연결과 실행 증거로 판정한다.

옛 분류의 유효한 의도는 이 독립성이다. “GPU 기능은 전부 설계뿐” 같은 과거 상태는 현재 METEOR 구현과 맞지 않으므로 승계하지 않는다.

## 구현 근거

- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp](../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp)
- [Engine/RenderEngine/GpuGeometryVisibility.h](../../Engine/RenderEngine/GpuGeometryVisibility.h)
- [Engine/RenderEngine/RHI/RHIDisplayFrame.h](../../Engine/RenderEngine/RHI/RHIDisplayFrame.h)
