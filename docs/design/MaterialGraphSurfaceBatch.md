# 공간 표본의 Material 평가 배치

> 기준: `493536e300e274cbf59989cdd62b20310c8ae64f`의 코드·호출 연결 정적 검토. 이 문서 갱신에서는 빌드·GPU 실행·성능 측정을 수행하지 않았다.

## 입력과 API

`SurfacePoint`는 uv/lod, world position, normal, tangent, bitangent를 가진 80-byte 평가 입력이다. `SurfaceView`는 eye와 scene/view/geometry revision을 가진다. 이 API는 pixel interpolation이나 미분을 만들어내지 않는다. CPU host가 explicit LOD를 주거나 `SurfaceGeometrySource`가 GPU buffer와 exact view를 제공한다.

`BuildSurfaceSource`는 생성된 material에 compute 평가 host를 붙이고 `VerifySurfaceProduct`는 그 target과 reflected layout을 검증한다. `SurfaceEvaluator::Initialize`는 candidate-first 방식으로 compute generation을 준비하며 실패하면 기존 evaluator를 유지한다.

## CPU와 GPU 흐름

`Record`는 CPU point를 upload해 평가한다. `RecordGpu`는 geometry source를 받아 중간 CPU readback 없이 평가한다. `PrepareGpu`는 allocation만 수행하고 `SurfaceBatch::Declare`로 producer 이후 graph에 등록할 수 있다. 출력은 `IblBakePoint` buffer이며 `IblBaker`가 이를 이어받을 수 있다.

GPU source는 device, recording, view와 준비 상태가 맞아야 한다. 다른 recording의 결과를 사용할 때는 completed readback acceptance가 필요하다. tangent의 한 축만 빠진 입력은 모호하므로 정상 tangent frame으로 간주하지 않는다. tangent pair가 없을 때 geometric normal fallback을 쓰는 계약과 malformed 값 거부를 구분한다.

## 검증·게시·재사용

`IsReadyForBake`는 recorded stage와 geometry 준비를 확인한다. `ValidateReadback`은 완료된 evaluated point를 검사하며 invalid evaluation은 `viewTier.w = -1`로 드러난다. 기록 성공을 정상 material 값으로 조용히 승격하지 않는다.

`Matches`는 immutable instance, view/revision, exact point bytes 또는 source identity를 비교한다. 충돌 가능한 hash만으로 cache hit를 결정하지 않는다. texture residency는 다시 touch해야 하며 buffer, material binding, compute generation, source owner를 마지막 submission 완료까지 유지한다.

## 제품과의 관계

이 batch는 평가/IBL bake를 조합하는 bounded helper다. 실제 METEOR Scene의 hybrid visibility → material reconstruction → fragment split-sum 경로 전체와 동일하지 않다. [Scene host](MaterialGraphSceneHost.md)가 제품 entrypoint다. 이 helper의 수치 probe 통과만으로 live Scene의 visibility·shadow·blend를 승인할 수 없다.

## 구현 근거

- [`Engine/RenderEngine/MaterialGraphSurfaceBatch.h`](../../Engine/RenderEngine/MaterialGraphSurfaceBatch.h)
- [`Engine/RenderEngine/MaterialGraphSurfaceBatch.cpp`](../../Engine/RenderEngine/MaterialGraphSurfaceBatch.cpp)
- [`Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/MaterialGraphSurfaceHost.slang`](../../Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/MaterialGraphSurfaceHost.slang)
