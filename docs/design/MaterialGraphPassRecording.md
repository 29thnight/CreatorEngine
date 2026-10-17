# Material Graph 준비·선언·병렬 기록

## 1. 구현 범위

`RasterSurfaceBatch`, `SurfaceBatch`, `IblBakeResult`를 같은 `EnhancedRenderGraph`에
선언한다. 불투명 깊이 수집 → 가시 픽셀 resolve → 생성 재질 CS 평가 → 물리 IBL bake의
GPU 순서와 UAV/SRV 전이는 그래프가 소유한다. 기록 콜백은 전달된 encoder를 사용하며
자원·upload·descriptor를 할당하거나 전역 immediate encoder를 가져오지 않는다.

초기 게이트는 한 재질 chunk 묶음·독립 depth·최대 4096픽셀 범위다.
현재 포즈 mesh 생산과 여러 재질의 공유 depth/opaque winner 검증 확장은
[MaterialGraphSharedDepth.md](MaterialGraphSharedDepth.md)가 소유한다. 실제 Scene의
공유 depth·직접광/컬러·legacy 가림·투명 합성·실시간 lookup 예산 설치는 남는다.
`EnhancedSceneRenderer`의 LX 미설치 거부와 MAT-7 진행 상태를 유지한다.

## 2. 호출 순서

1. geometry를 준비한다. `MeshSurfaceEvaluator::Prepare`로 이번 입력의 mesh/skin
   packet을 만들고 raster보다 먼저 같은 graph에 선언할 수 있다. persistent source는
   완료 readback을 통과하고 실제 최초 자원 상태를 host가 보장한 경우만 재사용한다.
   그래프 밖 immediate geometry를 기록했다면 그 제출 순서와 acceptance를 별도로 닫는다.
2. texture/environment 업로드와 필요한 최초 상태를 준비하고 cache residency를 유지한다.
   외부 texture는 `ShaderResource` 상태이거나 host가 실제 현재 상태로 graph에 한 번
   import해야 한다. 같은 texture/buffer를 다른 핸들로 중복 import하지 않는다.
3. 병렬 경로는 빈 graph의 `PrepareParallel(pool)`을 호출한다. immediate upload prefix를
   먼저 제출하고 이후 packet에 사용할 recording/descriptor generation을 고정한다.
4. `RenderBindingCache::Prepare`, `RasterSurfaceCollector::Prepare`,
   `SurfaceEvaluator::PrepareGpu`, `IblBaker::PrepareGpu`로 불변 packet을 만든다.
   PSO·source·instance·environment owner와 constant/binding table을 packet이 보유한다.
   준비 실패는 caller가 가진 이전 결과를 보존한다.
5. raster → surface → bake 순서로 각 batch의 `Declare(graph)`를 호출한다. 미선언 producer,
   다른 graph/device, 중복 선언을 거부한다. `GraphOutput(graph)`는 실제 RHI buffer와
   graph identity/resource epoch를 확인한다. `Reset`은 이전 resource reference를 무효화한다.
6. graph를 compile한 뒤 순차 `Execute` 또는 `RecordParallel`로 기록한다. 일찍 준비한
   병렬 graph는 prefix를 다시 제출하지 않는다. 중간 prefix/abort/descriptor 변경과
   반복 기록은 실패다. 콜백 오류는 두 기록 경로의 실패 진단으로 전달된다.
7. 전체 graph 기록 성공과 각 단계의 ready 상태를 확인한다. 병렬 batch의
   `lifetimeToken`에 graph owner를 넣어 native submission/retirement까지 모든 source와
   material/environment owner를 유지한다. 실패한 graph/batch는 제출·게시하지 않는다.
8. GPU 완료 뒤 geometry → raster → material의 readback acceptance를 확인한다.
   ready는 명령 기록 상태이며 GPU 완료나 Scene publication을 뜻하지 않는다.

기존 `SurfaceEvaluator::Record/RecordGpu`, `IblBaker::Record/RecordGpu`는 immediate host의
호환 경로다. 같은 준비/명령 packet을 사용하되 수동 전이는 해당 wrapper에만 남는다.
이 wrapper를 병렬 graph 콜백에서 호출하지 않는다.

## 3. 검증 게이트

`Tools/regression/verify-material-raster-surface.ps1`은 기존 독립 CPU 대조 baseline의
7개 geometry/coverage fixture 각각을 순차 graph, worker 1개, worker 4개로 다시 기록한다.
21개 graph·35개 worker list를 실제 D3D12 queue에 제출하며 raster/material/IBL의 모든
결과를 baseline과 바이트 단위로 비교한다. 기록 중 upload/descriptor 할당과 worker의
immediate encoder 접근은 probe에서 오류로 처리한다.

6개 실패 fixture는 surface/IBL allocation 중 native prefix, 선언 후 prefix,
늦은 implicit pool preparation, 순차 콜백 오류 전달, abort 후 재사용을 검사한다.
준비 실패 결과 보존, producer/graph identity, 중복 선언, resource reset, 단일 기록,
caller owner/ticket 해제 후 GPU completion retirement도 검사한다.

GPU runtime은 D3D12다. DXIL/SPIR-V artifact compile은 Vulkan runtime이나 전체 Editor
빌드/실제 Scene 렌더 설치의 증거가 아니다. MAT-7 완료 공수는 추가하지 않는다.

## 4. 2026-09-28 검증 결과

Debug/Release 각각 409,684개 검사·GPU 392,560성분·기존 7개 CPU 대조 fixture와
추가 21개 graph·35개 worker list·6개 실패 fixture를 통과했다. 21개 graph의 모든
raster/material/IBL 결과가 검증된 baseline과 바이트 단위로 동일했다.
D3D12 GPU validation WARNING 이상은 0건이다. baseline의 물리/SRGB/LOD 오차
허용 기준과 관측 값은 MaterialGraphRasterSurface.md 그대로 유지한다.

로그는 Build/Obj/MaterialProductProbe/graph-passes-gate-final.log,
raster-surface-{build-,}{Debug,Release}.log에 기록한다. 각 pass는 동일 graph의
입출력 resource handle을 사용하며 prefix/abort 후 stale packet을 제출하지 않는다.

기존 immediate 경로와 공용 GPU source의 회귀도 Debug/Release 모두 통과했다.

| 게이트 | 구성별 검사 / GPU 성분 |
|---|---|
| SurfaceBatch | 19,387 / 19,092 |
| IBL bake | 19,351 / 18,240 |
| MeshSurface·Scene packet | 621,160 / 286,156 |

회귀 로그는 같은 폴더의 graph-passes-regression-{surface-batch,ibl-bake,mesh-surface}.log다.
저장소 포맷(9개 재질 파일과 변경한 graph 구간), PowerShell parser(2개),
dashboard inline JavaScript 및 MAT-7 진행/26일 완료 상태, 문서 링크·변경 범위 공백 검사도 통과했다.
전체 Editor 빌드와 실제 Scene/Vulkan runtime은 이번 게이트에 포함하지 않는다.

후속 current mesh·공유 depth 게이트는 [MaterialGraphSharedDepth.md](MaterialGraphSharedDepth.md)를
참고한다. 위 수치는 초기 게이트의 기록이며 후속 재실행 수치로 대체하지 않는다.
