# 메시·스키닝 입력과 GPU 표면 변환

> 기준: `493536e300e274cbf59989cdd62b20310c8ae64f`의 코드·호출 연결 정적 검토. 이 문서 갱신에서는 빌드·GPU 실행·성능 측정을 수행하지 않았다.

## 밀봉된 메시와 pose

`MeshSurfaceInput::Seal`은 `EnhancedDrawItem`에서 immutable model geometry, world transform, bone palette, LOD 표본을 밀봉한다. mutable Mesh/Animator/Material 포인터는 남기지 않는다. model generation과 source vertex/primitive identity, meshlet payload, hierarchy가 geometry 계약의 일부다.

`MeshSurfacePlan::Build`는 stable triangle order로 분할하며 모든 triangle을 정확히 한 번 포함한다. chunk 사이 vertex 중복은 허용하지만 UV/normal seam을 weld하지 않고 원래 vertex ID를 유지한다. `BuildForScene`은 검증된 topology를 재사용하면서 world, eye, revision, pose를 새로 밀봉한다.

## 변환과 공유

Scene 준비는 동일 geometry/pose/pipeline의 posed object-space 출력 공유를 지원한다. 각 instance wrapper는 자기 ObjectToWorld를 보유하며 camera/shadow consumer가 이를 적용한다. 이전 설명처럼 모든 instance의 world-space vertex buffer를 별도로 만들어야 하는 구조가 아니다.

normal은 합성 transform의 inverse-transpose 의미, tangent/bitangent는 선형 transform과 authored handedness를 따른다. singular/degenerate 변환에서는 유한 geometric fallback과 tangent perturbation 비활성화를 사용한다. skinned/current pose와 cached topology를 혼동하지 않는다.

`MeshSurfaceSample`은 triangle과 perspective-correct B/C weight 및 explicit LOD를 가진 16-byte 샘플이다. 정점 변환과 triangle interior sample은 다른 작업이다. `ResolveSurfaceLod`는 한 이미지의 UV derivative와 크기로 isotropic mip을 계산하는 CPU reference 계약이다.

## 예산과 수명

기본 plan 상한은 source vertex 2^20, triangle 2^21, chunk 4096, CPU 256 MiB, GPU 512 MiB다. 일반 chunk point 상한은 `IblBaker::MaxPoints`를 따르지만 Scene host는 별도 budget을 전달한다. 상한 초과 시 geometry를 조용히 잘라내지 않는다.

`MeshSurfaceBatch::DeclareBatch`는 여러 mesh의 transform/readiness/reuse를 묶어 pass 수가 mesh 수에 비례하지 않게 한다. output은 shared producer owner에 속하고 recording마다 declaration owner가 정해진다. `MarkSubmitted`는 공유된 실제 producer까지 completion을 전달한다.

## 검증 경계

준비/record-ready와 completed `ValidateReadback`은 다르다. cache 통계의 uploads/hits/transforms는 CPU bookkeeping이며 GPU-visible triangle 수가 아니다. 실제 Scene visibility 및 LOD는 [Scene host](MaterialGraphSceneHost.md), exact identity는 `SceneGeometryIdentity` 계약을 따른다. 이번 갱신은 실행이나 성능 재측정을 하지 않았다.

## 구현 근거

- [`Engine/RenderEngine/MaterialGraphMeshSurface.h`](../../Engine/RenderEngine/MaterialGraphMeshSurface.h)
- [`Engine/RenderEngine/MaterialGraphMeshSurface.cpp`](../../Engine/RenderEngine/MaterialGraphMeshSurface.cpp)
- [`Engine/RenderEngine/MaterialGraphGeometryContract.h`](../../Engine/RenderEngine/MaterialGraphGeometryContract.h)
- [`Dynamic_CPP/Assets/Shaders/DefaultPassShader/MaterialGraphMeshSurface.slang`](../../Dynamic_CPP/Assets/Shaders/DefaultPassShader/MaterialGraphMeshSurface.slang)
- [`Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/MaterialEvaluation.slang`](../../Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/MaterialEvaluation.slang)
