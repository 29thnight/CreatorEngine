# 변환 출력 재사용과 제출 수명

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 현재 구현

MeshSurfaceCache의 transformed cache는 immutable partition·geometry/pose·layout·object-space 여부와 소유권을 비교한다. output 공유는 world transform을 무시하는 무조건 캐시가 아니다. eviction은 generation age뿐 아니라 참조 수와 GPU completion도 확인한다.

## 확인해야 할 경계

정적·skin pose 변경, instance world만 변경, source generation 교체, 이전 temporal pose와 frames-in-flight를 구분한다. cache hit/CPU bytes와 실제 GPU resident bytes는 별도 지표다. submit 실패 뒤 accepted되지 않은 output을 성공 상태로 게시하지 않는다.

## 정본과 이전 기록

전체 geometry 계약은 [GPU 메시렛 배선](../design/GpuDrivenMeshletDxrWiring.md), 라이브 연결은 [파이프라인 흐름](RenderPipelineFlow.md)을 따른다. 이 날짜 문서는 주제와 경로를 보존한 호환 안내다. 과거 선택 스위치·형식 버전·로컬 실행 수치는 현행 제품 계약으로 재사용하지 않는다. 이번 개정의 확인 범위는 소스와 호출 경로이며 GPU 실행·성능 수용은 미실행이다.

## 소스 근거

- [Engine/RenderEngine/MaterialGraphMeshSurface.cpp](../../Engine/RenderEngine/MaterialGraphMeshSurface.cpp)
- [Engine/RenderEngine/MaterialGraphSceneHost.cpp](../../Engine/RenderEngine/MaterialGraphSceneHost.cpp)
- [Engine/RenderEngine/RHI/IRenderDeviceServices.h](../../Engine/RenderEngine/RHI/IRenderDeviceServices.h)

## 이전 기록

[기준 커밋의 개정 전 원문](https://github.com/29thnight/CreatorEngine/blob/493536e300e274cbf59989cdd62b20310c8ae64f/docs/analysis/LxTransformResidency20261012.md)에 당시 단계·세부 앵커·측정 이력이 남아 있다. 그 실행 결과를 현재 커밋의 수용으로 승계하지 않는다.
