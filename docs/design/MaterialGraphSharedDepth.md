# Material Graph 현재 포즈·공유 깊이

<!-- master-doc-review:start -->
## 2026-10-20 master 구현 대조

기준: `27de3f5a02aadde7456aab680db3184c36ee735e`. 대시보드는 변경하지 않는다. 아래 현행 대조와 소스 선언이 이전 시점의 구조·미구현 표기보다 우선한다.

[전체 대조·링크 정리 원장](../analysis/MasterDocumentationAudit20261020.html)

### 현행 계약과 이 문서의 적용 범위

- live material은 compiled graph 프로그램과 밀봉된 scene/view/geometry 입력을 소비한다. 준비·실패 상태에서 옛 instance를 대체 경로로 선택하는 구안을 현행 계약으로 사용하지 않는다.
- GPU scene packet·resident table·visibility/REAC 최적화는 구현 기반이다. shader의 물리 의미, route별 출력과 전체 비용의 수용은 소스 존재와 별개다.

### 해당 구현에서 확인한 선언·소유 경계

- [Tools/regression/verify-material-raster-surface.ps1:4](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-material-raster-surface.ps1#L4) — `[ValidateSet('Debug','Release')][string[]]$Configurations = @('Debug','Release')`
- [Tools/regression/verify-material-raster-surface.ps1:9](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-material-raster-surface.ps1#L9) — `$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))`
- [Tools/regression/verify-material-raster-surface.ps1:10](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-material-raster-surface.ps1#L10) — `if ([string]::IsNullOrWhiteSpace($VisualStudioInstallation)) {`
- [Engine/RenderEngine/ShaderMeta.h:24](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/ShaderMeta.h#L24) — `enum class ShaderPropertyType : std::uint8_t`
- [Engine/RenderEngine/ShaderMeta.h:98](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/ShaderMeta.h#L98) — `enum class ShaderPassQueue : std::uint8_t`
- [Engine/RenderEngine/ShaderMeta.h:106](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/ShaderMeta.h#L106) — `enum class ShaderBlendMode : std::uint8_t`
- [Engine/RenderEngine/MaterialGraphSceneHost.h:160](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/MaterialGraphSceneHost.h#L160) — `bool PrepareResidency(const EnhancedFrameContext& context, const own::shared_owner<const SceneViewInput>& input,`
- [Engine/RenderEngine/MaterialGraphSceneHost.h:162](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/MaterialGraphSceneHost.h#L162) — `bool Prepare(const EnhancedFrameContext& context, own::shared_owner<const SceneViewInput> input,`
- [Engine/RenderEngine/MaterialGraphSceneHost.h:357](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/MaterialGraphSceneHost.h#L357) — `static void DeclareHardwareVisibility(EnhancedRenderGraph& graph, const own::shared_owner<const FrameInput>& frame,`
- [Engine/RenderEngine/MaterialGraphSceneInput.h:15](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/MaterialGraphSceneInput.h#L15) — `own::shared_owner<const Instance> instance;`
- [Engine/RenderEngine/MaterialGraphSceneInput.h:21](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/MaterialGraphSceneInput.h#L21) — `static own::shared_owner<const SceneMaterialSource> Capture(const Material& material);`
- [Engine/RenderEngine/MaterialGraphSceneInput.h:91](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/MaterialGraphSceneInput.h#L91) — `static bool Seal(const SceneInputView& view, std::span<const EnhancedDrawItem> draws,`
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h:231](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h#L231) — `class EnhancedRenderGraph : private IRHIUploadTransactionListener`
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h:451](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h#L451) — `explicit EnhancedRenderGraph(IRenderDeviceServices& services,`
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h:467](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h#L467) — `bool PrepareParallel(IRHIParallelCommandPool& pool, std::string& outError);`
- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.h:31](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.h#L31) — `enum class EnhancedLiveDisplayTarget : uint8_t`
- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.h:459](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.h#L459) — `inline bool EnhancedGraphSnapshotMatchesView(const EnhancedRenderGraph::DiagnosticSnapshot& graph,`
- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.h:1209](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.h#L1209) — `std::shared_ptr<const EnhancedRenderGraph::DiagnosticSnapshot> GetLiveGraphSnapshot(`
- [Tools/build.ps1:2](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L2) — `[CmdletBinding()]`
- [Tools/build.ps1:4](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L4) — `[ValidateSet('Game')][string]$Target = 'Game',`
- [Tools/build.ps1:21](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L21) — `$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))`
- [Engine/RenderEngine/ShaderMeta.cpp:382](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/ShaderMeta.cpp#L382) — `else if ("less" == depthTest.Scalar()) outState.depthTest = RHICompareOp::Less;`
- [Engine/RenderEngine/ShaderMeta.cpp:383](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/ShaderMeta.cpp#L383) — `else if ("lessEqual" == depthTest.Scalar())`
- [Engine/RenderEngine/ShaderMeta.cpp:385](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/ShaderMeta.cpp#L385) — `else return Fail(context, "depthTest는 off|less|lessEqual이어야 한다", outError);`

### 이전 근거의 유효 범위

기존 세부 계약은 위 현행 구현과 모순되지 않는 범위에서 유지한다. 과거 수치·실행 통과·해시·장치 조건은 당시 기록이다. 로컬 산출물이 사라진 항목은 링크 대신 경로와 미보존 상태를 표시하며, 현재 master의 재실행 통과로 승격하지 않는다.

<!-- master-doc-review:end -->

<details data-master-history><summary>세부 계약·원래 분석과 당시 측정 기록</summary>

## 1. 구현 범위

이 문서는 독립 RHI의 공유 capture 계약이다. 실제 legacy GBuffer·Masked 가림·HDR 합성을
연결한 후속 host는 [MaterialGraphSceneHost.md](MaterialGraphSceneHost.md)에 기록한다.

`MeshSurfaceEvaluator::Prepare`와 `MeshSurfaceBatch::Declare`로 이번 입력의 GPU
skin/world 변환을 raster·재질 평가·IBL과 같은 `EnhancedRenderGraph`에 기록한다.
별도 geometry 제출이나 중간 CPU readback을 요구하지 않는다. 업로드·descriptor·PSO는
기록 전에 준비하고 콜백은 전달된 encoder만 사용한다.

`RasterSurfaceCollector::PrepareSharedDepth`는 동일 graph의 완전한 불투명 capture를
소유한다. 선택한 재질의 mesh owner는 그 capture에 포함되어야 한다. viewport·camera·
view revision·cull policy·recording을 대조하고, 다른 graph나 부분 capture의 depth를
다시 빌리는 것은 거부한다. 실패는 caller가 가진 이전 packet을 보존한다.

공유 소비 패스는 D32 depth를 `DepthRead`와 읽기 전용 DSV로 선언한다. PSO는
`Equal`·depth write 0이며 깊이를 clear하지 않는다. DX12/Vulkan 변환표에 `Equal`을
추가했다. native Vulkan 실행 검증은 아직 포함하지 않는다.

depth equality만으로 coplanar draw를 구분할 수 없으므로 전체 capture의 position MRT에
정확한 draw ID(1..64)를 저장한다. `LXRasterSharedPS`는 그 ID와 자기 draw를 대조하고
다른 재질의 fragment를 버린다. UV fine derivative는 ownership 분기 전에 계산한다.
따라서 최초 `Less` depth capture가 선택한 동일 깊이의 winner도 유지한다.
재질별 footprint는 depth producer와 달라도 되며 각 재질의 UV/LOD를 다시 수집한다.
현재 한 graph context에 LOD 하나를 전달하므로 여러 texture의 독립 footprint 완료는 아니다.

## 2. 호출 순서와 수명

1. 제품 `EnhancedDrawItem`의 geometry·world·palette와 per-view 값을
   `MeshSurfaceInput::Seal` 또는 `MeshSurfacePlan::Build`로 복사한다.
2. texture/environment를 준비하고, 워커 경로는 빈 graph의 `PrepareParallel`을 호출한다.
3. 각 mesh의 `Prepare`, 완전한 opaque capture의 `Prepare`, 재질별
   `PrepareSharedDepth`, `SurfaceEvaluator::PrepareGpu`, `IblBaker::PrepareGpu`를 호출한다.
4. mesh → opaque capture → 각 재질의 shared capture/evaluation/bake 순서로 선언한다.
   아직 쓰지 않은 mesh buffer를 SRV로 import해 생산 단계를 생략하지 않는다.
5. compile 후 순차 또는 병렬 기록을 완료하고, native 제출·GPU 완료까지 graph와
   전체 source/material/environment owner를 유지한다. CPU 콜백 기록 순서와 GPU 제출
   순서는 다르므로 워커 콜백에서 앞선 콜백의 ready flag를 기다리지 않는다.
6. GPU 완료 뒤 모든 mesh → opaque capture → 재질별 capture → material 순서로
   readback을 수용한다. ready flag는 기록 완료이며 GPU 완료나 Scene 게시가 아니다.

`MeshSurfaceEvaluator::Record`는 같은 준비 packet/명령을 사용하는 immediate 호환 경로다.
`RecordSamples`는 아직 기록하지 않은 prepared vertex buffer를 거부한다.
구간 전환·descriptor 변경·abort로 stale upload가 생기면 graph 선언/기록을 거부한다.
graph reset 뒤에는 이전 mesh output handle을 사용할 수 없다. 생성/reset 세대는 전역적으로
고유하므로 동일 주소에 graph를 다시 구성해도 이전 buffer index가 producer로 인정되지 않는다.

## 3. 검증

`Tools/regression/verify-material-raster-surface.ps1 -SkipDependencyRestore`가 현재 소스를
Debug/Release로 빌드하고 native D3D12 GPU validation과 함께 실행한다.

- 기존 CPU 대조 baseline 7개와 21개 순차/1워커/4워커 graph의 raster/material/IBL
  바이트 일치. 이제 21개 graph 모두 current mesh producer를 같은 graph에 포함한다.
- 서로 다른 Core/Layered 재질의 가림, 근평면 clipping, coplanar, 현재 포즈 스키닝
  4종 × draw 순서 2종 × 기록 방식 3종의 공유 깊이 graph 24개.
- 모든 가시 픽셀에 재질 owner 하나만 존재하며 CPU depth/coverage·재질·IBL과 대조.
  두 재질의 서로 다른 LOD bias도 검사한다. 소비 전후 공유 depth bytes는 동일해야 한다.
- 스키닝 입력의 palette/source를 seal 후 변조해도 GPU 출력은 복사한 포즈를 사용한다.
- 누락/다른 mesh owner·camera/viewport·부분 depth source·미선언 producer·다른 graph
  거부, 전체 depth source의 acceptance 순서와 실패 시 이전 capture 보존.
- 기존 6개 기록 실패와 4개 mesh prefix/late-prefix/abort/graph 주소 재사용 실패. 아직 기록하지 않은
  geometry가 그럴듯한 CPU 데이터만으로 acceptance를 얻지 못하는 것도 검사한다.

최종 Debug/Release는 각각 아래 결과를 통과했다. 픽셀 수는 각 configuration의
fixture 전체 누계이며, 전체 Scene 해상도나 성능 측정값이 아니다.

| 항목 | 각 configuration 결과 |
|---|---:|
| 전체 검사 / GPU 성분 | 776,402 / 652,048 |
| current mesh 포함 baseline graph / 공유 깊이 graph | 21 / 24 |
| 공유 가시 픽셀 / coplanar 픽셀 | 5,688 / 1,422 |
| 현재 포즈 스키닝 graph | 6 |
| 기록 실패 / mesh 수명 실패 fixture | 6 / 4 |
| DXIL·SPIR-V 컴파일 산출물 | 24 |
| D3D12 GPU validation WARNING 이상 | 0 |

최대 정규화 오차는 `0.0000859499`(기준 `0.0001`), SRGB 절대 오차는
`0.000689566`(기준 `0.001`), LOD transport 절대 오차는 `0.000169724`
(기준 `0.0005`)다. 물리 계산 오차 기준을 늘리지 않았다.

공용 경로의 현재 소스 회귀도 Debug/Release 각각 통과했다. MeshSurface는
621,160개 검사·GPU 286,156성분·8 layout×3 pose·8,200정점·82샘플,
SurfaceBatch는 19,387개 검사·GPU 19,092성분·37지점·8프레임이다.
이 게이트 모두 D3D12 GPU validation WARNING 이상은 0건이다.
Vulkan은 shader 컴파일과 변환 코드 빌드 범위이며 native GPU 검증은 아니다.

원본 로그는 `Build/Obj/MaterialProductProbe/shared-depth-gate-final.log`와
`raster-surface-{build-,}{Debug,Release}.log`에 둔다. 공용 회귀 결과는
`shared-depth-mesh-regression.log`, `shared-depth-surface-regression.log`에 둔다.

## 4. 남은 실제 Scene 설치

이 단계는 최대 4096픽셀·64 draw·동일 cull policy의 독립 RHI 범위다. 전체 capture도
6개 MRT를 사용한다. 이 자원 구성을 제품 full-screen 비용으로 수용한 것은 아니다.
현재 Scene draw pool 호출이나 GBuffer의 depth를 그대로 연결한 것으로 세지 않는다.

- 실제 draw pool/per-view CPU 입력 밀봉은 [MaterialGraphSceneInput.md](MaterialGraphSceneInput.md)에서
  연결했다. 해당 입력을 실제 Scene GPU graph host로 공급하는 설치가 남는다.
- 기존 ShaderMeta 재질·masked coverage까지 포함한 공유 depth와 draw ownership을 연결한다.
- 직접광·컬러·기존 SSAO/SSR/투명 합성과 async publication을 설치한다.
- 전체 해상도·재사용·시간/메모리 예산, 환경 변경/device recreation을 판정한다.
- Special SSS/refraction/Volume과 자동 host/compiler cooker closure를 마무리한다.

`EnhancedSceneRenderer`의 LX 미설치 거부와 MAT-7 progress 상태를 유지한다.
전체 Editor 빌드·실제 Scene·Vulkan runtime 완료나 추가 완료 공수를 기록하지 않는다.

</details>
