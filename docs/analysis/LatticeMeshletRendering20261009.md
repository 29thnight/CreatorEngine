# Lattice mesh shader 연결 및 검증 — 2026-10-09

<!-- master-doc-review:start -->
## 2026-10-20 master 구현 대조

기준: `27de3f5a02aadde7456aab680db3184c36ee735e`. 대시보드는 변경하지 않는다. 아래 현행 대조와 소스 선언이 이전 시점의 구조·미구현 표기보다 우선한다.

[전체 대조·링크 정리 원장](MasterDocumentationAudit20261020.html)

### 현행 계약과 이 문서의 적용 범위

- live material은 compiled graph 프로그램과 밀봉된 scene/view/geometry 입력을 소비한다. 준비·실패 상태에서 옛 instance를 대체 경로로 선택하는 구안을 현행 계약으로 사용하지 않는다.
- GPU scene packet·resident table·visibility/REAC 최적화는 구현 기반이다. shader의 물리 의미, route별 출력과 전체 비용의 수용은 소스 존재와 별개다.

### 해당 구현에서 확인한 선언·소유 경계

- [Engine/RenderEngine/MaterialGraphSceneHost.h:98](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/MaterialGraphSceneHost.h#L98) — `class SceneHost final : private IRHIUploadTransactionListener`
- [Engine/RenderEngine/MaterialGraphSceneHost.h:101](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/MaterialGraphSceneHost.h#L101) — `explicit SceneHost(job_scheduler& scheduler = ce::get_job_scheduler());`
- [Engine/RenderEngine/MaterialGraphSceneHost.h:108](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/MaterialGraphSceneHost.h#L108) — `bool RequestProgram(const EnhancedFrameContext& context, own::shared_owner<const Generation> generation,`
- [Tools/build.ps1:2](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L2) — `[CmdletBinding()]`
- [Tools/build.ps1:4](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L4) — `[ValidateSet('Game')][string]$Target = 'Game',`
- [Tools/build.ps1:21](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L21) — `$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))`
- [Engine/RenderEngine/MaterialGraphSceneHost.cpp:61](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/MaterialGraphSceneHost.cpp#L61) — `std::memcpy(local, static_cast<const std::byte*>(geometry.vertexData) + index * geometry.vertexStride,`
- [Engine/RenderEngine/MaterialGraphSceneHost.cpp:87](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/MaterialGraphSceneHost.cpp#L87) — `sphere.radius = std::hypot(high[0] - low[0], high[1] - low[1], high[2] - low[2]) * .5f * 1.0001f + 1e-4f;`
- [Engine/RenderEngine/MaterialGraphSceneHost.cpp:89](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/MaterialGraphSceneHost.cpp#L89) — `if (!shadow_math::IntersectsClip(sphere, lightViewProjection))`

### 이전 근거의 유효 범위

기존 세부 계약은 위 현행 구현과 모순되지 않는 범위에서 유지한다. 과거 수치·실행 통과·해시·장치 조건은 당시 기록이다. 로컬 산출물이 사라진 항목은 링크 대신 경로와 미보존 상태를 표시하며, 현재 master의 재실행 통과로 승격하지 않는다.

<!-- master-doc-review:end -->

<details data-master-history><summary>세부 계약·원래 분석과 당시 측정 기록</summary>

기준 HEAD: `4bb4978897e2c99ae7f64233d3e691b0a113ad9b` + 로컬 변경.
VS 2026 v18 / v145, Release Editor, DX12, RTX 4070 Ti.

## 구현 범위

- `MaterialGraphSceneHost`의 불투명·마스크 재질에 실제 mesh shader PSO와
  `DispatchMeshIndirect`를 연결했다. 카메라 재질 패스와 독립적인 cascade 그림자가 대상이다.
- 기존 `LX.MeshWorld`의 변형된 정점 버퍼, 재질 pixel shader, 재질 상수·텍스처를 그대로 소비한다.
  mesh shader를 사용한다는 이유로 material graph나 skin/world 변형을 우회하지 않는다.
- 장치의 mesh shader·indirect·출력 및 dispatch 한도를 확인한다. 정렬된 투명체·transmission,
  미지원 장치, 캐시 예산 초과 및 dispatch 한도 초과는 indexed 경로를 유지한다.
  지원 경로는 기본 ON이며 `CREATOR_LX_MESHLETS=0`으로 비교·복귀할 수 있다.
- Scene host identity를 17로 변경하고 `LXSceneMS`·`LXSceneShadowMS`의 DXIL/SPIR-V
  컴파일 대상을 추가했다. 생성 재질 검증은 현재 host에 대응하는 정확한 추가 진입점만 인정한다.
- `preparedMeshletBatchCount`에 lattice의 준비된 mesh draw bin을 합산한다.
  이 값은 GPU 컬링 후 생존 수가 아니다.

## 토폴로지·가시성·수명

Lattice의 sealed chunk는 원래 모델과 정점 번호가 다를 수 있다. 원본 모델 meshlet의 remap을
잘못 재사용하지 않고, 해당 chunk의 index stream을 64정점/126삼각형 이하로 나눈다.
**원본 삼각형 순서와 각 삼각형의 corner 순서를 보존**한다. 결과는 static geometry cache의
최초 입장 시 한 번 만들고 재사용하며, 매 draw/frame에 다시 만들지 않는다.

토폴로지 버퍼는 정점·인덱스·LOD와 같은 업로드 transaction/완료점/캐시 소유권으로 유지한다.
업로드 pass와 실제 mesh raster consumer의 읽기를 RenderGraph에 명시한다.

기존 GPU 가시성 kernel을 유지한다. mesh 전용 bin은 indirect record의 첫 세 uint를
`(meshletCount, visibleCount, 1)`로 만든다. 각 bin에는 정확히 한 candidate가 있으므로
두 번째 값은 0 또는 1이다. mesh consumer만 이 prefix를 `DispatchMeshIndirect`로 읽는다.
기존 카메라 HZB와 그림자의 독립적인 caster 판정을 CPU readback 없이 보존한다.

이는 **draw 단위 GPU 컬링 + meshlet raster frontend**이다. meshlet별 GPU 압축·cone culling,
GPU LOD 선택까지 lattice에 연결한 것은 아니다. 원본 cooked model의 meshlet payload를 그대로
소비하는 native 경로와도 구분한다. 최초 chunk packing 비용과 전체 성능 이득은 별도 측정 대상이다.

## 발견하고 수정한 실패

1. 새 mesh entry가 vertex entry를 직접 호출하던 버전은 설치된 Slang의
   `getEntryPointCode`에서 ACCESS_VIOLATION을 일으켰다. 공통 일반 함수
   `LXSceneVertexAt`으로 분리한 뒤 아래 실제 실행들에서 재현되지 않았다.
   최초 덤프: `CreatorEditor_20261009_180159_157056`.
2. 기존 generated-pass 검사에 mesh entry가 빠져 재질 로딩이 거부됐다.
   Scene host의 고정 대체 vertex frontend 두 개를 정확히 검증하도록 보완했다.
3. 최초 meshoptimizer 재배열 버전은 깊이가 같아도 일부 재질 픽셀이 달랐다.
   정상 출력으로 수용하지 않았다. source triangle/corner 순서를 보존하는 packing으로 바꾸자
   아래 비교가 전부 바이트 단위로 일치했다. 특정 GPU 내부 미분 동작까지 단정하지 않는다.

## 실행 증거

- Release Editor 빌드 통과: `Build/lattice-meshlet-release-order.log`.
  기존 `Utility_Framework.pdb`의 LNK4020 경고는 남아 있다.
- 기존 vertex-layout 계약 runner: **59/59 통과**.
  실제 packed topology를 해석해 원본 삼각형·corner의 순서, bounds, 무효 layout 거부를 확인했다.
  `Build/lattice-meshlet-topology-order-test.log`.
- 실제 `Dynamic_CPP/Assets/Scenes/TestShadow.creator`, 1496×692, 동일 카메라·변환·포즈·재질,
  indexed/mesh 각 4회. 기본 7 attachment + 중간 HDR 9개 × 4 = **64개 바이트 단위 일치**.
  입력 의미 비교도 통과했다.
  `Build/Verification/LatticeMeshlets20261009/order-preserved/exact-comparisons.json`.
- ON 로그에서 surface·shadow 모두 `indirect=1` mesh dispatch 확인.
  두 비교 실행 모두 exit 0. 빈 화면 비교가 아니라 7개 lattice draw를 포함한 제품 capture다.
- 최종 ON GPU validation: **problems 0, droppedMessages 0, exit 0**.
  `Build/Verification/LatticeMeshlets20261009/order-gpu-validation/Meshlet-1/validation.json`.
  검증 레이어의 PSO instrumentation 준비가 수 분 걸렸으며 성능 측정에 사용하지 않았다.
- indexed OFF의 GPU validation도 `contract-fix/Meshlet-0`에서 problems 0 / exit 0을 확인했다.
  이 실행은 최종 stable-order packing 이전이므로 최종 ON 증거와 구분한다.

재현: `Tools/regression/verify-lattice-meshlets.ps1 -Configuration Release -OutputDirectory <새 경로>`.
기존 Editor에서 실행하며 별도 프로젝트를 생성하지 않는다. `-ValidateGpu`는 correctness 전용이다.

Debug·Vulkan 실행, 다양한 skinned/특수 재질 장면, 성능 수용은 이 기록에 포함하지 않는다.
이 연결을 GPU-1 전체 완료나 FPS 개선으로 계산하지 않는다.

</details>
