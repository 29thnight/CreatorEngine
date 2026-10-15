# GPU 메시렛·가시성 배선과 향후 DXR 경계

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 구현된 래스터 경로

현재 제품 geometry 경로는 MaterialGraphSceneHost의 METEOR 단계다. geometry 변환 → 첫 카메라 목록 → occluder depth → 현재 깊이 HZB와 복구 컬링 → 최종 visibility → material reconstruction으로 연결된다. Shadow는 camera HZB를 재사용하지 않고 광원 투영별 독립 메시렛 목록을 만든다.

이 경로는 mesh-indirect 지원과 유효한 cooked meshlets를 요구한다. SceneHost::Prepare는 미지원 또는 누락 계약을 오류로 돌려주며 CPU/direct surface 대체를 제공하지 않는다. GpuGeometryVisibility의 범용 helper에 존재하는 optional/null 결과와 fallback 설명을 SceneHost의 지원 정책으로 확대해서는 안 된다.

## 쿠킹과 식별자

현재 CEMC 형식은 15, granular geometry artifact는 4, model descriptor는 5다. meshlet profile과 builder는 2, LOD builder는 3이다. decoder는 기대 버전을 검사하므로 오래된 문서의 CEMC 11~13 또는 geometry artifact 2를 현재 입력으로 사용하지 않는다. 재임포트·재쿠킹이 필요한 경계를 임의 런타임 변환으로 우회하지 않는다.

SceneGeometryIdentity는 64바이트로 geometryKey, objectId, incarnation, instanceId, materialSlot과 LOD/primitive 정보를 가진다. geometry 신원, 배치된 객체 신원, 재질 slot과 GPU buffer handle은 서로 다른 값이다. hierarchy를 사용할 때 lodIndex의 UINT32_MAX 표식은 실제 LOD를 각 descriptor에서 읽는 계약이다.

LOD는 원본 정점을 참조하는 공간별 replacement hierarchy다. cooker는 child-before-parent의 분리된 이진 트리를 만들며 이는 DAG 계약의 제한된 부분집합이다. 부모 topology는 두 자식을 함께 대체한다. patch 경계 원본 정점을 유지하고 error는 부모로 단조롭게 증가한다. error는 meshoptimizer의 단순화 척도이며 보증된 Hausdorff 거리로 설명하지 않는다.

## 프레임 geometry와 가시성

GeometryStage는 현재·이전 pose와 그림자가 공유하는 고유 MeshSurfaceBatch 생산자를 선언하고 프레임 table을 발행한다. 인스턴스 world 변환과 geometry/pose 공유는 구분된다. GPU 목록과 indirect 인자는 해당 recording·descriptor 세대·graph epoch에 묶이며 다른 프레임·뷰가 덮어쓸 수 없다.

Cull.First에서 history를 이용한 후보를 만들고 Occluder가 그 결과를 HW/SW 혼합으로 기록한다. Cull.Final은 occluder의 깊이로 만든 현재 pyramid를 읽는다. 복구 결과로 자기 자신을 컬링하는 순환 입력을 만들지 않는다. 불투명 최종 목록과 반투명 ordered 목록은 별도 출력이다.

반투명은 frustum-only meshlet 목록의 원본 순서를 보존하고 LayerWork가 정렬된 소비 명령을 만든다. camera의 HZB 성과를 반투명이나 그림자의 occlusion 지원으로 해석하지 않는다. ShadowStage는 3개 cascade를 각각 Modify하고 각 cascade의 GPU 시간을 구분할 수 있는 pass를 만든다.

## 셰이딩·후속 소비

SurfaceStage의 visibility와 material tile/argument는 색과 temporal 입력 생산자가 명시적으로 받는다. decal·temporal·layer가 암묵적으로 다른 단계의 명령을 생성하지 않도록 실제 graph handle 계약을 검사한다. HDR 최초 writer는 SceneHost::DeclareHdrTarget와 Color 단계이며 일반 Deferred 보정 패스는 현재 조립에 없다.

준비된 후보 수·논리 caster 명령 수·bucket submission 수·실제 GPU survivor 수는 다른 지표다. CPU PreparedStats만으로 컬링률이나 누락 없음·성능 향상을 주장할 수 없다.

## DXR와 Path Tracing은 별도 미구현 목표

현재 BuildPipelineDesc에는 BLAS/TLAS 빌드, ray dispatch, ray-traced shadow 또는 PathTracing 출력 노드가 없다. 이 문서의 메시렛 구현은 DXR 배선 완료를 의미하지 않는다. 향후 공통 ray-tracing scene은 동일한 geometry/material generation과 제출 수명을 사용해야 하며, native acceleration structure와 shader table을 RHI 안에 둬야 한다.

보존하는 설계 요구는 다음과 같다: instance 갱신과 BLAS 재빌드/refit의 구분, alpha/양면/skin material 의미 일치, graph의 build→trace→denoise 의존성, 장치 미지원 시 정직한 기능 상태, raster와 RT의 출력 비교, 실패·resize·device loss 시 완료 기반 retirement. ReSTIR PT와 tetrahedral cages는 연구·수용 대상으로 남으며 현재 기능으로 표시하지 않는다.

## 검증 경계

이번 문서는 코드 계약 감사다. 실제 GPU topology 누락, 독립 patch LOD 경계, skin·음수/비균일 scale, 급격한 카메라 이동, HZB recovery, alpha와 cascade 정확도는 현행 바이너리로 확인해야 한다. 과거 indexed/meshlet A/B 스위치가 제거된 현재에는 존재하지 않는 옵션을 되살리지 말고 독립 reference·변이·readback oracle을 사용한다.

## 소스 근거

- [Engine/RenderEngine/MaterialGraphSceneHost.cpp](../../Engine/RenderEngine/MaterialGraphSceneHost.cpp)
- [Engine/RenderEngine/MaterialGraphSceneGeometryStage.inl](../../Engine/RenderEngine/MaterialGraphSceneGeometryStage.inl)
- [Engine/RenderEngine/MaterialGraphSceneFinalCullStage.inl](../../Engine/RenderEngine/MaterialGraphSceneFinalCullStage.inl)
- [Engine/RenderEngine/MaterialGraphSceneShadowStage.inl](../../Engine/RenderEngine/MaterialGraphSceneShadowStage.inl)
- [Engine/RenderEngine/Experiment/MeshLodData.h](../../Engine/RenderEngine/Experiment/MeshLodData.h)
- [Engine/RenderEngine/Experiment/Import/MeshLodBuilder.cpp](../../Engine/RenderEngine/Experiment/Import/MeshLodBuilder.cpp)
- [Engine/RenderEngine/Experiment/Cooked/CookedModelFormat.h](../../Engine/RenderEngine/Experiment/Cooked/CookedModelFormat.h)
- [Engine/RenderEngine/Experiment/Cooked/CookedModelSubAssetCodec.h](../../Engine/RenderEngine/Experiment/Cooked/CookedModelSubAssetCodec.h)
- [Engine/RenderEngine/MaterialGraphGeometryContract.h](../../Engine/RenderEngine/MaterialGraphGeometryContract.h)
- [Dynamic_CPP/Assets/Shaders/DefaultPassShader/GeometryVisibility.slang](../../Dynamic_CPP/Assets/Shaders/DefaultPassShader/GeometryVisibility.slang)
