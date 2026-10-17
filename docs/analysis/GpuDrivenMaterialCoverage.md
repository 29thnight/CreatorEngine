# GPU-driven 재질 coverage와 세대별 geometry 계약

> 기준: `493536e300e274cbf59989cdd62b20310c8ae64f`의 코드·호출 연결 정적 검토. 이 문서 갱신에서는 빌드·GPU 실행·성능 측정을 수행하지 않았다.

## 현재 제품과 보조 경로

live Scene 재질은 [Scene host](../design/MaterialGraphSceneHost.md)의 graph-only 입력을 사용한다. 예전 native Code/LX 혼합 draw에 대한 GPU-driven 보고서를 현재 제품의 Code admission 근거로 사용할 수 없다. Code graphics helper와 회귀 fixture는 남아 있지만 arbitrary Code shader가 live Scene에서 허용된다는 뜻은 아니다.

공통 GPU geometry 코드는 실제 shader의 position/instance 의미를 모르는 경우 안전하게 원래 draw를 보존해야 한다. reflection에서 buffer가 보인다는 이유만으로 shader가 compacted visible ID를 올바르게 읽거나 동일한 변형을 계산한다고 가정하지 않는다.

## 보존과 압축의 다른 계약

`GpuGeometryVisibility`는 preserved bin의 개수와 원본 대응을 검증한다. original ID는 유효 범위 안에서 중복 없이 원본을 덮어야 한다. compacted 경로는 별도로 visible instance ID와 indirect argument를 만든다. 두 경우의 instance ID 의미를 섞으면 재질·transform·skin palette가 다른 객체를 참조할 수 있다.

`GraphicsCompileIdentity`는 `visibleInstanceIds`, `geometryVisibility`, 실제 vertex mask와 permutation을 정확한 accepted generation에 보관한다. root-layout ID를 공유한다는 사실은 이 의미 계약을 공유한다는 증거가 아니다. 알 수 없는 native position 의미는 full draw 보존으로 제한하며 임의의 culling opt-in을 만들지 않는다.

## 선택적 skin extent ABI

`GBufferSkinPaletteExtentV1`은 엄격히 반사 검증한 b4의 4개 uint, 16-byte uniform이다. root index는 shader generation에 귀속된다. 이 opt-in은 palette 범위를 전달하는 계약이며, 자체적으로 deformation/culling 동등성을 인증하거나 custom instance bytes를 바꾸지 않는다.

Graph 제품은 immutable topology와 현재 pose를 분리해 봉인하고 object-space geometry를 여러 instance에서 소비한다. [mesh surface](../design/MaterialGraphMeshSurface.md)와 [shared depth](../design/MaterialGraphSharedDepth.md)의 owner·pose·visibility 조건을 함께 확인해야 한다.

## coverage 판정 기준

새 geometry opt-in에는 direct/indirect 결과의 ID·depth·색상 대응, 빈/전부 visible 집합, skin bounds, stale generation 거부, 현재 pose와 multi-instance를 포함한다. 보존 경로의 픽셀 통과와 압축 효율, occlusion의 보수성, 전체 모델 성능은 별도 판정이다.

이전 PR 단계의 fixture 통과는 당시 backend와 입력에 한정된 기록이다. 현재 코드 연결을 확인했지만 GPU culling 정확성·성능 또는 모든 material permutation을 다시 실행하지 않았다.

## 구현 근거

- [`Engine/RenderEngine/GpuGeometryVisibility.cpp`](../../Engine/RenderEngine/GpuGeometryVisibility.cpp)
- [`Engine/RenderEngine/GpuGeometryVisibility.h`](../../Engine/RenderEngine/GpuGeometryVisibility.h)
- [`Engine/RenderEngine/LXMaterialPipeline.h`](../../Engine/RenderEngine/LXMaterialPipeline.h)
- [`Engine/RenderEngine/MaterialGraphSceneHost.cpp`](../../Engine/RenderEngine/MaterialGraphSceneHost.cpp)
- [`Engine/RenderEngine/MaterialGraphMeshSurface.cpp`](../../Engine/RenderEngine/MaterialGraphMeshSurface.cpp)
