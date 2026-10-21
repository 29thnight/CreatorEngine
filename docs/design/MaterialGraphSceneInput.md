# Material Graph 실제 Scene 입력 밀봉

<!-- master-doc-review:start -->
## 2026-10-20 master 구현 대조

기준: `27de3f5a02aadde7456aab680db3184c36ee735e`. 대시보드는 변경하지 않는다. 아래 현행 대조와 소스 선언이 이전 시점의 구조·미구현 표기보다 우선한다.

[전체 대조·링크 정리 원장](../analysis/MasterDocumentationAudit20261020.html)

### 현행 계약과 이 문서의 적용 범위

- live material은 compiled graph 프로그램과 밀봉된 scene/view/geometry 입력을 소비한다. 준비·실패 상태에서 옛 instance를 대체 경로로 선택하는 구안을 현행 계약으로 사용하지 않는다.
- GPU scene packet·resident table·visibility/REAC 최적화는 구현 기반이다. shader의 물리 의미, route별 출력과 전체 비용의 수용은 소스 존재와 별개다.

### 해당 구현에서 확인한 선언·소유 경계

- [Engine/SceneRuntime/ProxyCommand.cpp:97](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/ProxyCommand.cpp#L97) — `? material_graph::SceneMaterialSource::Capture(*update.material) : nullptr;`
- [Engine/SceneRuntime/ProxyCommand.cpp:104](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/ProxyCommand.cpp#L104) — `auto effective = own::make_shared<experiment::Material>();`
- [Engine/SceneRuntime/ProxyCommand.cpp:224](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/ProxyCommand.cpp#L224) — `? material_graph::SceneMaterialSource::Capture(*type.m_material) : nullptr;`
- [Engine/SceneRuntime/PrimitiveProxyBridge.cpp:82](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/PrimitiveProxyBridge.cpp#L82) — `m_graphMaterialSource = material_graph::SceneMaterialSource::Capture(*m_Material);`
- [Engine/SceneRuntime/PrimitiveProxyBridge.cpp:90](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/PrimitiveProxyBridge.cpp#L90) — `auto effective = own::make_shared<experiment::Material>();`
- [Engine/SceneRuntime/PrimitiveProxyBridge.cpp:177](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/PrimitiveProxyBridge.cpp#L177) — `? material_graph::SceneMaterialSource::Capture(*type.m_material) : nullptr;`
- [Tools/regression/verify-material-raster-surface.ps1:4](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-material-raster-surface.ps1#L4) — `[ValidateSet('Debug','Release')][string[]]$Configurations = @('Debug','Release')`
- [Tools/regression/verify-material-raster-surface.ps1:9](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-material-raster-surface.ps1#L9) — `$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))`
- [Tools/regression/verify-material-raster-surface.ps1:10](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-material-raster-surface.ps1#L10) — `if ([string]::IsNullOrWhiteSpace($VisualStudioInstallation)) {`
- [ScriptCore/Animator.cs:14](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/ScriptCore/Animator.cs#L14) — `public sealed class Animator : NativeComponent`
- [ScriptCore/Animator.cs:18](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/ScriptCore/Animator.cs#L18) — `public void SetBool(string name, bool value) => Native.AnimatorSetBool(OwnerHandle, name, value);`
- [ScriptCore/Animator.cs:19](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/ScriptCore/Animator.cs#L19) — `public void SetFloat(string name, float value) => Native.AnimatorSetFloat(OwnerHandle, name, value);`
- [Engine/SceneRuntime/Animator.h:81](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/Animator.h#L81) — `own::shared_owner<const assets::ModelAnimationPayload> payload{};`
- [Engine/SceneRuntime/Animator.h:86](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/Animator.h#L86) — `own::shared_owner<const assets::ModelAnimationDescriptor> descriptor{};`
- [Engine/SceneRuntime/Animator.h:87](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/Animator.h#L87) — `own::shared_owner<const assets::ModelSkeletonPayload> skeleton{};`
- [Engine/RenderEngine/ShaderMeta.h:24](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/ShaderMeta.h#L24) — `enum class ShaderPropertyType : std::uint8_t`
- [Engine/RenderEngine/ShaderMeta.h:62](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/ShaderMeta.h#L62) — `struct ShaderMaterialSampler`
- [Engine/RenderEngine/ShaderMeta.h:71](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/ShaderMeta.h#L71) — `struct ShaderGeneratedMaterial`
- [Engine/RenderEngine/ProxyCommand.h:63](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/ProxyCommand.h#L63) — `own::shared_owner<const Material> material{};`
- [Engine/RenderEngine/ProxyCommand.h:68](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/ProxyCommand.h#L68) — `own::shared_owner<const material_graph::SceneMaterialSource> graphMaterialSource;`
- [Engine/RenderEngine/ProxyCommand.h:72](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/ProxyCommand.h#L72) — `own::shared_owner<const experiment::Material> authoredMaterial{};`
- [Engine/RenderEngine/FrameCameraSnapshot.h:14](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/FrameCameraSnapshot.h#L14) — `struct FrameCameraSnapshot`
- [Engine/RenderEngine/FrameCameraSnapshot.h:16](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/FrameCameraSnapshot.h#L16) — `math::matrix4x4 view{};`
- [Engine/RenderEngine/FrameCameraSnapshot.h:44](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/FrameCameraSnapshot.h#L44) — `bool HasSameTemporalHistory(const FrameCameraSnapshot& previous) const noexcept`

### 이전 근거의 유효 범위

기존 세부 계약은 위 현행 구현과 모순되지 않는 범위에서 유지한다. 과거 수치·실행 통과·해시·장치 조건은 당시 기록이다. 로컬 산출물이 사라진 항목은 링크 대신 경로와 미보존 상태를 표시하며, 현재 master의 재실행 통과로 승격하지 않는다.

<!-- master-doc-review:end -->

<details data-master-history><summary>세부 계약·원래 분석과 당시 측정 기록</summary>

## 1. 이번 설치 범위

`EnhancedSceneRenderer`의 실제 draw pool과 per-view 선택에 LX 입력을 연결했다.
`SceneMaterialSource`는 producer가 고른 불변 graph 인스턴스와 coverage를 보존하고,
`SceneViewInput`은 선택된 뷰의 camera·geometry·world·pose를 값으로 밀봉한다.
raw `Material`/Camera/Animator/proxy 주소는 최종 입력으로 넘기지 않는다.

이 문서의 입력 밀봉 범위는 **실제 Scene CPU 입력 경계 설치**다. 후속
[SceneHost](MaterialGraphSceneHost.md)는 같은 입력을 실제 GBuffer·공유 D32·HDR 합성에
연결한다. 정확도 기준용 IBL 적분을 사용하므로 4,096픽셀·64 draw 이하로 제한한다.
전역 full-resolution capability나 실제 Editor 렌더 완료로 세지 않는다.

## 2. 실제 호출 경로

### Producer → proxy

- `SceneRuntime/PrimitiveProxyBridge.cpp`의 Mesh/Foliage 프록시 생성자가
  `SceneMaterialSource::Capture`로 현재 typed instance와 coverage를 복사한다.
- `SceneRuntime/ProxyCommand.cpp`는 mesh delta와 foliage type delta를 만들 때도
  같은 capture를 수행한다. RT 적용은 이미 밀봉한 owner를 옮긴다.
- Mesh delta의 LX owner 갱신은 기존 `Material*`/GUID 비교와 별도로 수행한다.
  같은 legacy owner/GUID에 새 인스턴스를 붙여도 다음 **프록시 갱신**에 반영한다.
  인스턴스가 사라진 delta는 이전 LX owner를 null로 교체한다.
- 실제 편집/재로딩은 프록시 갱신을 발행해야 한다. 이번 단계가 모든 material 편집의
  dirty 통지나 재로딩 이벤트를 자동으로 추가한 것은 아니다.

### Proxy → draw pool → selected view

1. `TickLive`가 delta를 적용한 뒤 `BuildDrawPool`을 실행한다.
2. Mesh/Foliage source에 producer가 밀봉한 LX owner가 있으면 instance·coverage를
   `EnhancedDrawItem`에 담는다. RT가 LX용 mutable `Material`을 다시 읽지 않는다.
3. `SealForwardMaterials`/`SealGBufferMaterials`는 LX source에 ShaderMeta snapshot을
   붙이지 않는다. 기존 두 queue의 primary/secondary ShaderMeta 계약은 각 pass가 소유한다.
4. `CaptureFromView`는 기존 frustum 판정 뒤 LX draw를 별도로 선택한다.
   frame/scene epoch·view ID/history·extent와 `FrameCameraSnapshot`을 함께 전달한다.
5. `SceneViewInput::Seal`이 `MeshSurfacePlan::Build`로 source geometry·world·palette를
   복사하고 triangle order/local-source remap을 보존한다. 이후 임시 graph draw 목록을 비운다.
6. GPU host는 이 owning plan의 각 chunk를 `MeshSurfaceEvaluator::Prepare`에 전달할 수 있다.
   현재 [공유 depth 검증](MaterialGraphSharedDepth.md)은 이 경로로 입력을 공급한다.

새 draw pool과 로딩 중 pool 제거, 새 view capture에서 이전 임시 LX 입력을 비운다.
앞선 Scene/view의 source를 새 frame 신원으로 이름만 바꾸어 사용하지 않는다.

## 3. 입력 계약과 실패 처리

- frame ID·scene epoch·view ID·extent는 유효해야 한다. camera view/projection은
  finite/invertible이며 matrix product와 eye는 spatial 범위 안이어야 한다.
- 각 draw는 owning typed instance와 일치하는 graph generation을 가져야 한다.
  LX와 GBuffer/Forward ShaderMeta snapshot을 동시에 담은 draw는 거부한다.
- producer는 runtime Material GUID를 `materialSlot`으로 함께 복사한다.
  [Scene generation 선택](MaterialGraphSceneGeneration.md)이 같은 epoch/view/Material의
  마지막 정상 instance·coverage를 유지하며 geometry와 슬롯 신원은 별개다.
- Opaque/Masked/Blended coverage와 double-sided를 복사한다. graph alpha는
  인스턴스가 소유하며 legacy base-alpha를 graph 기본값으로 곱하지 않는다.
  현재 clip cutoff는 coverage 기본값 `0.5`다. Masked/Blended 입력 보존은
  GPU alpha/투명 transport 구현 완료를 뜻하지 않는다.
- source vertex/triangle/chunk/CPU/GPU payload 한도와 뷰 전체 합계를 검사한다.
  이것은 geometry/evaluation payload 예산이며 full-frame MRT·descriptor·환경·
  동시 제출 비용을 포함한 실시간 예산 수용은 아니다.
- 전체 candidate가 성공한 뒤 결과를 교체한다. 뒤쪽 draw나 합계 예산 실패가
  이미 밀봉한 앞쪽 draw만으로 이전 뷰를 교체할 수 없다.
- 새 입력의 전역적으로 고유한 revision은 model generation뿐 아니라
  같은 frame의 다른 camera·world·pose·instance capture도 구분한다.
  hash나 legacy Material 주소를 geometry/view 신원으로 쓰지 않는다.
- vertex LOD의 0은 raster transport 초기화다. 최종 texture LOD는
  perspective UV derivative로 계산하며 texture별 독립 footprint는 후속이다.

## 4. 검증 경로

`Tools/regression/verify-material-raster-surface.ps1 -SkipDependencyRestore`는
기존 native D3D12 raster/material/IBL 게이트와 아래 입력 검사를 함께 실행한다.

- 서로 다른 두 뷰의 camera/revision과 같은 view의 world/instance 교체.
- producer snapshot 유지·graph 제거·미지원 coverage가 legacy로 내려가지 않는 계약.
- source geometry/index/world 수정 뒤 밀봉된 결과의 보존.
- frame/view/camera·geometry·instance·중복 ShaderMeta transport·coverage·
  source/aggregate payload 및 부분 성공 거부 24종. 실패 시 전체 이전 결과 유지.
- 동일 Scene 입력의 chunk를 GPU current mesh→opaque/shared depth→material→IBL로 공급.
  Core/Layered·coplanar·clipping·스키닝·draw 순서와 순차/1워커/4워커 24 graph를 대조한다.
  partition 뒤 GPU geometry는 원본 vertex index remap으로 독립 CPU 결과와 비교한다.

Debug/Release 각각 **773,635개 검사·GPU 649,168성분**, 21개 baseline graph와
24개 Scene-input/shared-depth graph, 입력 실패 24종을 통과했다. shader 산출물은
DXIL/SPIR-V 24개이며 D3D12 GPU validation WARNING 이상은 0건이다.
가시 5,688픽셀·coplanar 1,422픽셀·스키닝 6 graph를 검사했다.
최대 정규화 오차 `0.0000859499`, SRGB 절대 오차 `0.000689566`,
LOD 절대 오차 `0.000169724`는 이전 기준을 그대로 통과했다.
Scene partition이 실제로 참조한 정점만 GPU로 보내므로 종전 shared-depth 게이트보다
정점 성분 수가 줄었다. 전체 raster/material/IBL 픽셀 판정은 유지한다.

실제 producer 생성·delta 적용 코드를 포함한 `SceneRuntime`과 연결된 `RenderEngine`은
VS18/v145로 Debug/Release 빌드를 통과했다. 기존 mesh-surface 회귀 검사도
두 설정에서 각각 **621,160개 검사·GPU 286,156성분**을 통과했다.
D3D12 GPU validation WARNING 이상은 0건이다. SceneRuntime 빌드의
`C4244` 경고는 남아 있으며 전체 Editor 실행·Scene 렌더 검증은 수행하지 않았다.

원본 로그는 `Build/Obj/MaterialProductProbe/scene-input-gate-final.log` 및
`raster-surface-{build-,}{Debug,Release}.log`에 둔다.
producer/consumer 빌드는 `scene-input-runtime-{Debug,Release}-build.log`에 둔다.
기존 메시 회귀 로그는 `scene-input-mesh-regression.log`에 둔다.

## 5. 다음 설치 경계

실제 Scene CPU 수집 호출과 후속 [bounded GPU host](MaterialGraphSceneHost.md)를 연결했다.
legacy/Masked 가림·draw ownership·공유 depth·직접광/HDR 합성의 설치 검증과
실제 Editor의 전체 Scene 회귀는 구분한다. async acceptance/publication, full-resolution/reuse/예산,
환경 residency/MIS·Special transport·자동 host/compiler cooking도 별도로 남는다.

독립 D3D12 GPU 검사와 RenderEngine/SceneRuntime 빌드를 실제 Editor Scene 렌더나
Vulkan 전체 Scene 검증으로 세지 않는다. Vulkan native PSO 준비·기본 draw/readback은
[Scene generation §6](MaterialGraphSceneGeneration.md)에서 별도로 검증한다.
MAT-7은 progress이며 완료 공수를 늘리지 않는다.

</details>
