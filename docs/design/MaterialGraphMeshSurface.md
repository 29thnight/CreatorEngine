# Material Graph 메시·스키닝 평가 입력

MAT-7 잔여 중 제품 정점 ABI의 공간 입력과 GPU 평가 결과의 게시 경계를 연결했다.
`EnhancedSceneRenderer`의 graph pass 설치 완료는 아니다. 실제 Scene의 LX 거부 경계와
기본 capability는 유지한다.

## 1. 고정하는 입력

`MeshSurfaceInput::Seal`은 `EnhancedDrawItem::modelMeshView`의 정점·삼각형 index,
world matrix, 본 팔레트와 정점별 명시적 LOD를 복사한다. 제품 호출자는 검증된
`BuildRHIModelMeshView`를 제공한다. legacy `Mesh*`로 내려가지 않고, material/animator
포인터나 변경 가능한 본 팔레트를 보존하지 않는다. 객체는 복사·이동을 금지해 내부
RHI view가 자신의 저장소를 가리키는 관계를 유지한다.

정점 mask·stride·layout hash는 `assets::kVertexAttributeTable`에서 검산한다.
Core/Color/Skin과 각 UV1 조합의 8개 형식을 받으며, raw offset을 따로 하드코딩하지 않는다.
UV1·Color는 보존하고 초기 평가 좌표는 UV0를 사용한다. graph의 추가 UV 채널 의미는
별도 작업이다. 256개 이하의 palette를 받는다. 단일 `Seal`은 4096정점까지이며,
큰 메시의 제품 입력은 아래 `MeshSurfacePlan::Build`로 분할한다.

Affine·유한 transform, 정점 속성, authored tangent handedness, index 범위와
skin weight/palette 범위를 검사한다. Skin 형식도 palette가 없거나 weight가 0인 정점은
제품 Forward의 `NO_SKINNING`과 같이 rest geometry를 사용한다.
결과는 candidate를 검증한 뒤 교체하므로 실패 시 이전 입력을 보존한다.
동일성은 모델/메시/generation, world, pose bytes, 정점·index·LOD 및 view/revision을
정확히 비교한다.

## 2. GPU 변환과 평가

`MaterialGraphMeshSurface.slang::LXTransformMesh`는 packed product 정점과 기존 48B
`PackedBoneMatrix` 규약을 소비한다. `skin × world`의 row-vector 변환으로 위치를 만들고,
공용 `TransformPbrFrame`의 역전치 normal 및 linear tangent/bitangent 변환을 적용한다.
비균일 scale, shear, mirrored transform과 tangent sign을 유지한다.

80B `SurfacePoint` 정의를 `MaterialGraphSurfacePoint.slang`으로 공유했다.
`SurfaceEvaluator::RecordGpu`가 GPU 월드 frame buffer를 직접 읽어 graph를 평가하고,
그 결과를 `IblBaker::RecordGpu`에 전달한다. 세 단계 사이에 CPU readback/wait가 없다.
GPU 변환·평가·bake는 RenderGraph 이전의 immediate preparation용이다.
split graph Record callback 안에서 allocation/전이를 수행하는 API가 아니다.

할당 중 recording 또는 descriptor generation이 바뀌면 GPU 변환/bake는 기록을 거부하고
이전 결과를 보존한다. host가 새 구간에서 다시 준비해야 하며, 서로 다른 구간의 upload와
table을 한 dispatch에 섞지 않는다.

## 3. 완료 검증과 Scene packet

`MeshSurfaceBatch::ValidateReadback`은 해당 제출의 완료된 월드 frame을 검사한다.
비유한 위치, 무너진 tangent frame, eye와 같은 위치 등의 거부 진단은 vertex index를 포함한다.
GPU source를 가진 `SurfaceBatch`는 이 geometry 검증도 통과해야 재질 결과를 수용한다.
취소되거나 검증하지 않은 source는 다음 recording에서 재사용할 수 없다.

`BuildSceneSurfaceEvaluation`이 immutable instance와 정확한 scene/view/geometry revision을
GPU batch에서 가져온다. `SceneMaterialSlot::Prepare`는 CPU point 또는 GPU batch 중 하나만
받으며, 다른 instance/revision을 덧씌우면 거부한다. GPU bake와 source를 packet이 함께 소유한다.
IBL root register는 host가 지정할 수 있고 기본은 t0다. 이 검증 host는 spatial t0/points t2와
겹치지 않도록 IBL t1을 사용한다.

GPU 후보의 `PublishSubmitted`는 native 제출 성공, 정확한 recording/fence, GPU 완료,
geometry/재질 readback 검증을 요구한다. 대기나 실패는 마지막 정상 `Active`를 보존한다.
완료 후 잘못된 후보를 `RejectSubmitted`하면 제출 owner를 회수한다.
제품 host의 비동기 readback polling·submission ticket 배선은 별도로 연결해야 한다.
batch 자체를 기록 성공만으로 Scene에 게시하지 않는다.

## 4. 검증 범위

```powershell
& Tools/regression/verify-material-mesh-surface.ps1
```

실제 RenderEngine/Utility_Framework 및 native DX12 device/root/PSO/texture cache를 사용한다.
제품 ABI의 synthetic indexed mesh fixture 8형식 × 3포즈를 검사한다. 정점 37개로
32-thread dispatch의 끝 경계도 포함한다.

- 독립 CPU double scalar의 world/skin/inverse-transpose frame 대조.
- source를 밀봉한 뒤 producer 정점·본을 변경해도 GPU 입력 유지.
- UV/LOD·SRGB texture·IOR/Specular Level·Core/Layered coat/sheen/aniso/thin film 평가와 IBL 대조.
- 동일 GPU 위치 버퍼로 실제 indexed 삼각형을 그린 뒤 raster 위치 대조.
- Scene packet의 native binding과 bounded point lookup 소비 draw 대조.
- 제출만 성공한 후보의 게시 보류, 완료 검증 후 게시, 실패 시 마지막 정상 유지.
- 잘못된 stride/LOD/index/skin 입력, collapsed GPU frame, 취소 source 재사용 거부.
- 실제 native prefix 제출을 할당 중 주입해 recording 혼합 거부 및 tail abort 검사.

픽셀별 한 지점의 lookup draw와 indexed geometry 진단은 별도 검사다.
이 결과를 임의 삼각형의 물리 IBL 보간 품질이나 제품 Scene 렌더 완료로 세지 않는다.
DXIL/SPIR-V 컴파일은 둘 다 검사하며 GPU 실행은 D3D12다. Vulkan runtime은 미검증이다.

아래 분할·삼각형 샘플 검증을 포함해 Debug/Release 각각 **621,160개 검사·286,156개
GPU 성분**, 8개 형식·3포즈·37정점, 큰 메시 8200정점·3 chunk와 Core/Layered 82샘플,
31개 완전 제출 및 3개 native prefix 제출, DXIL/SPIR-V 22개 artifact를 통과했다.
최대 정규화 오차는 **0.0000596344**(기준 0.0001)다. SRGB source는 별도 기준 0.001로
검사하며 최대 절대 오차 0.000689566이었다. D3D12 GPU validation WARNING 이상은 0건이다.
`TypeTrait.h` C4189와 Debug LNK4075 외 신규 C++ 경고는 없었다.
원본 로그는 `Build/Obj/MaterialProductProbe/mesh-surface-{build-,}{Debug,Release}.log`와
`mesh-partition-gate-final.log`다. RenderEngine/Utility_Framework 링크 결과이며 전체 Editor
신규 빌드나 실제 Scene 재개방으로 기록하지 않는다.

공용 spatial frame 사전 검사의 회귀로 `MaterialSurfaceBatchProbe`도 Debug/Release 각각
19,387개 검사·19,092개 GPU 성분·8프레임을 다시 통과했다. 로그는
`mesh-partition-surface-regression.log` 및 `surface-batch-{build-,}{Debug,Release}.log`다.
이번 단계에서 ScenePacket/IBL 독립 gate를 재실행한 것으로 표시하지 않는다.

## 5. 큰 메시 분할과 삼각형 샘플

`MeshSurfacePlan::Build`가 전체 입력을 검증·복사한 뒤 원본 triangle 순서대로 묶는다.
각 chunk는 4096정점 이내이며 삼각형은 정확히 한 번씩 보존한다. 첫 등장 순서의 local
index와 `sourceVertices` 대응, `firstTriangle`을 함께 보존한다. 같은 위치·UV라도 원본
index가 다르면 용접하지 않으므로 UV/normal/skin seam을 유지한다. chunk 사이에 걸친
공유 정점은 동일 bytes와 LOD로 복제한다. 사용하지 않는 정점은 chunk에 넣지 않는다.
degenerate triangle도 임의 삭제하지 않는다.

기본 상한은 원본 1,048,576정점·2,097,152삼각형·4096 chunk, CPU payload 256MiB·
GPU payload 512MiB다. 호출자가 더 낮게 지정할 수 있으며 초과/잘못된 입력은 전체
후보를 거부하고 이전 plan을 보존한다. `Cost`의 CPU는 원본 복사와 chunk의 정점·LOD·
index·source remap·palette bytes, GPU는 **정점 평가 경로**의 upload 및 world/material/bake
buffer bytes다. vector capacity·객체·작업용 remap, upload alignment·descriptor, texture
residency, 추가 triangle sample buffer와 동시 frame 소유권은 이 수치에 포함하지 않는다.
전체 실시간 메모리 판정은 실제 host의 할당 계측으로 진행해야 한다.

`MeshSurfaceEvaluator::InitializeSampler`는 검증된 `LXSampleMesh` CS를 받는다.
`RecordSamples`는 chunk의 triangle 번호, perspective-correct B/C weight와 명시적 LOD를
밀봉한다. A weight는 `1 - B - C`다. 4096샘플 이내, 유한·비음수 weight와 합 ≤1,
triangle 범위·LOD 0..32를 검사한다. sample batch는 원본 vertex batch를 함께 소유한다.
sample 수는 vertex 수와 독립적이며 sample을 vertex array로 다시 사용하는 것은 거부한다.
GPU의 A weight는 host의 float `B + C <= 1` 판정과 같은 합으로 계산해 삼각형 edge에서
순차 뺄셈이 작은 음수 A를 만들지 않게 한다.

GPU는 이미 skin/world 변환한 세 vertex의 UV·position·normal·tangent·bitangent를
샘플 위치에서 보간하고 **그 위치에서 graph와 물리 IBL을 새로 평가**한다. 정점에서
평가한 base color/roughness나 baked radiance를 섞지 않는다. 서로 다른 texel을 읽는
삼각형에서 이 차이를 CPU reference와 대조한다. 완료 검증은 vertex → sample → material
순서다. 보간으로 tangent frame이 무너지면 샘플을 거부하고 graph/IBL GPU 출력에
실패 표식을 남긴다. 임의 fallback으로 seam을 수리하지 않는다.

`ResolveSurfaceLod`는 host가 제공한 UV dx/dy와 한 texture의 extent/mip 수에 대해
`clamp(log2(max(length(dx * extent), length(dy * extent))) + bias, 0, lastMip)`를 구한다.
magnification은 0으로 제한한다. extent와 mip chain, 비유한 derivative를 검사하며
실패 시 이전 LOD를 보존한다. 이 값은 명시적인 isotropic `SampleLevel` 정책이다.
현재 graph context의 LOD는 하나이므로 다른 extent·UV transform을 쓰는 여러 texture의
독립 footprint나 anisotropic gradient filtering을 구현한 것으로 세지 않는다.

실제 visible pixel의 triangle/weight/derivative 수집, sparse sample 해상도·재사용·오차
수용, GPU 시간과 전체 frame 메모리 예산은 남아 있다. 샘플 요청의 weight가 실제
Scene raster의 perspective 보정 결과인지는 해당 host가 검증해야 한다.

## 6. 남은 제품 설치

이번 포즈의 mesh/skin 생산을 같은 graph의 raster/evaluation/IBL에 선언하고 기록하는
경로와 공유 depth 검증은 [MaterialGraphSharedDepth.md](MaterialGraphSharedDepth.md)에 추가했다.

- Scene draw packet에서 입력 생성 호출 및 per-view 소유권 연결.
- 가시 픽셀의 깊이/원근 보간·UV fine derivative 수집은
  [MaterialGraphRasterSurface.md](MaterialGraphRasterSurface.md)의 독립 RHI/순차 graph 패스에
  구현했다. 실제 Scene 호출, texture별 LOD, spatial lookup 해상도·재사용 및 오차·시간·메모리 판정은 남는다.
- graph host의 실제 mesh/light/coverage ABI, opaque Forward depth와 투명 정렬/합성.
- 제품 Scene의 병렬 RenderGraph usage·구간별 binding과 async 진단/게시, 환경 eviction/device recreation.
- 환경 MIS/수렴, Special SSS/refraction/Volume, 자동 host/compiler cook closure.

MAT-7 전체는 진행 중이며 완료 공수를 추가하지 않는다.
