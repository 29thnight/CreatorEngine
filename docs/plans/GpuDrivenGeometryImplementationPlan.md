# GPU geometry의 현행 구현과 남은 수용

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 현재 기준선

이전 문서의 CEMC v11, 선택적 LX mesh route, indexed fallback, CPU LOD 및 LOD0 shadow를 현재 제품 계약으로 사용하지 않는다. 현재 SceneHost는 ExplicitVersioned graph·mesh indirect capability·완성된 cooked meshlet topology를 필수로 요구한다. 부족하면 실패를 보고하며 CPU/direct surface로 조용히 되돌리지 않는다. 일반 GpuGeometryVisibility의 호환 분기는 이 제품 호출자의 지원 보장이 아니다.

저작 결과는 CEMC 15, geometry artifact 4, model descriptor 5 계약이다. meshlet profile/builder는 2, LOD builder는 3이다. 이전 세대를 바이트 수정하거나 Player에서 source를 다시 가져와 조용히 고치지 않는다. 모델의 논리 ID와 불변 generation, source primitive remap·material 경계를 함께 유지한다.

## 구현된 수직 경로

- GeometryStage가 현재·이전 pose의 object-space 정점을 선언한다. 같은 geometry/pose는 여러 인스턴스에서 공유할 수 있지만 각 world transform·객체 신원은 유지한다
- 첫 컬링은 history의 보수적 가시성을 사용하고 HW/SW 혼합 Occluder가 현재 깊이를 만든다. 그 HZB로 final opaque recovery를 수행한다
- coarse LOD는 spatial replacement hierarchy에서 GPU가 선택한다. parent가 두 child를 원자적으로 대체하며 acceptable ancestor가 있으면 child를 중복 출력하지 않는다
- Surface가 visible ID·primitive remap·posed vertex·material을 이용해 MRT를 복원한다. blended는 별도 frustum-only 목록과 원본 순서를 유지한다
- ShadowStage는 카메라 HZB와 독립적인 cascade별 목록·간접 명령과 깊이를 만든다. 스킨/변형 정점 생산자와 upload owner는 실제 소비 완료까지 유지한다

세부 계약은 [GPU geometry 정본](../design/GpuDrivenMeshletDxrWiring.md)과 [라이브 흐름](../analysis/RenderPipelineFlow.md)을 따른다.

## 남은 구현·수용 작업

완료선은 파일 존재가 아니라 같은 불변 장면에서 정확한 제출·출력·수명이다. CPU visibility oracle과 GPU 목록을 대조하고 overflow·잘못된 remap·version·stale handle은 명시적 실패 또는 문서화된 보수 처리로 판정한다. 카메라 cut/resize·움직이는 occluder·near plane·두 view·두 in-flight frame, 음수/비균일 scale·skin·custom pose·masked·ordered blended를 각각 검사해야 한다.

LOD의 meshoptimizer 오차는 인증된 Hausdorff 상한이 아니다. 화면 오차·실루엣·경계 균열·child replacement를 독립 reference로 수용한다. topology 선택과 material primitive mapping, shadow silhouette를 함께 확인한다. bind-pose cone을 변형 geometry에 안전 근거 없이 다시 활성화하지 않는다.

성능은 CPU prepare/record·deform·visibility·HW/SW occluder·surface/shadow·upload·VRAM의 전체 비용으로 판단한다. 역사적인 indexed 경로 비교가 현재 바이너리에 존재한다고 가정하지 않는다. reference fixture 또는 명시적으로 구현한 diagnostic 경로가 필요하다. Vulkan 실행 비교는 BackendParityPlan이 소유한다.

DXR/Work Graphs·persistent shadow-depth cache는 이 현행 경로로 구현된 것이 아니다. 다음 RT 작업은 별도의 AS·hit/material·수명 계약부터 시작한다. 이번 소스 감사는 이전 날짜의 이미지·속도·테스트 합계를 재인증하지 않았다.

## 소스 근거

- [Engine/RenderEngine/MaterialGraphSceneHost.cpp](../../Engine/RenderEngine/MaterialGraphSceneHost.cpp)
- [Engine/RenderEngine/MaterialGraphSceneGeometryStage.inl](../../Engine/RenderEngine/MaterialGraphSceneGeometryStage.inl)
- [Engine/RenderEngine/MaterialGraphSceneStages.inl](../../Engine/RenderEngine/MaterialGraphSceneStages.inl)
- [Engine/RenderEngine/GpuGeometryVisibility.cpp](../../Engine/RenderEngine/GpuGeometryVisibility.cpp)
- [Engine/RenderEngine/GpuGeometryOcclusion.cpp](../../Engine/RenderEngine/GpuGeometryOcclusion.cpp)
- [Engine/RenderEngine/MaterialGraphSceneShadowStage.inl](../../Engine/RenderEngine/MaterialGraphSceneShadowStage.inl)
- [Engine/RenderEngine/Experiment/MeshLodData.h](../../Engine/RenderEngine/Experiment/MeshLodData.h)
- [Engine/RenderEngine/Experiment/Cooked/CookedModelCodec.h](../../Engine/RenderEngine/Experiment/Cooked/CookedModelCodec.h)
