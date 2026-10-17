# C# Render Pipeline 저작 계획 (PHASE 4.6)

**2026-10-01 사용자 결정:** 이 페이즈의 실행·픽셀·성능 완료 판정은 DX12 Debug/Release다. RHI 중립 계약과 필요한 backend 구현은 유지한다. Vulkan 실행 비교·동등성·교차 픽셀 수용은 [PHASE 4.9](BackendParityPlan.md)의 RenderDoc 캡처 → 리소스 확인 → 픽셀별 비교가 단독 소유하며 이 페이즈의 선행·잔여·실패 조건으로 사용하지 않는다.


**2026-09-23 신설 · 계획 확정, 구현 미착수 · 7행 32인일(계획 추정).** 목표 구조는
[`RenderPipelineTargetArchitecture.md`](../design/RenderPipelineTargetArchitecture.md)가
정본이다. 이 계획은 **C#에서 Pipeline Description을 작성해 불변 IR로 내리는 경계**만
소유한다. 리소스 DAG·barrier·queue는
[`RenderGraphDependencySchedulingPlan.md`](RenderGraphDependencySchedulingPlan.md),
Material Graph/Principled는 [`BlenderMaterialGraphPlan.md`](BlenderMaterialGraphPlan.md),
라이트맵은 [`LightmapBakerPlan.md`](LightmapBakerPlan.md)가 소유한다.

구 [`ScriptableRenderPipelinePlan.md`](ScriptableRenderPipelinePlan.md)의 Pipeline Asset
Inspector 정본·선택적 C# 값 제어·일반 Pass Shader Graph를 이 계획의 활성 항목으로
승계하지 않는다. 기존 설계와 33일 산정은 구 방향의 역사 기록으로 보존하며, 아래
슬라이스에 그대로 이식하지 않는다. 특히 Roslyn/interop/세대 교체가 포함된 새 범위는
2026-10-01 재산정에서는 경계별 구현·DX12 검증 작업으로 32인일을 배정한다. 세부 근거는 [공수 원장](RenderPhaseEffortEstimate.md)을 따른다.

## 1. 실제 소비 경로와 대체 범위

현재 `EnhancedSceneRenderer::BuildPipelineDesc`가 C++ `LivePassNode`를 구성하고,
`LivePipelineDesc`가 초기화·준비·선언·역순 해제를 순회한다. 프레임마다
`EnhancedRenderGraph::Compile/RecordParallel`이 실제 GPU 작업을 처리한다.
`LivePassNode`의 인스턴스 포인터와 캡처 람다는 직렬화할 수 없으므로 C# IR은
stable Pass ID·타입 있는 resource handle·설정 값만 운반한다.

`EnhancedGBufferPass`·`EnhancedShadowPass` 같은 C++ 알고리즘은 native registry를
통해 계속 실행한다. 이 계획이 대체하는 첫 대상은 **고정된 19 node C++ 조립부**다.
프로젝트가 엔진 재빌드 없이 shader-only Fullscreen/Compute Pass를 추가해야 할 때는
별도 Template/Shader Asset 슬라이스를 실측해 연다. C# `Execute(CommandBuffer)`와
외부 DLL Native Pass ABI는 제공하지 않는다.

## 2. 공통 계약

| 경계 | 소유자 | 입력과 출력 |
|---|---|---|
| 저작 | C# `RenderPipeline.Build(builder)` | native Pass ID, typed slot/version handle, 설정·조건·root·queue 선호를 담은 IR |
| schema | native PassSchema + ShaderMeta/Keyword | Pass 입력·출력·수정·설정의 ID/형식과 permutation 단일 정본 |
| 게시 | Game-thread/저작 경계 | 검증 완료한 immutable IR generation만 native에 원자 게시 |
| 조립 | native IR compiler | Pass registry의 factory와 `LivePipelineDesc` 어댑터 |
| 실행 | `EnhancedRenderPass` + `EnhancedRenderGraph` | 그래프 사용 선언 → DAG/수명/배리어/기록 → RHI |

`Build()`는 프레임마다 호출하지 않는다. 프로젝트/파이프라인 로드, 코드 재컴파일,
설정/variant 변경처럼 **세대가 바뀌는 시점**에 다시 빌드한다. 저작 실패·managed
예외·schema/ABI 불일치 시 새 세대는 게시하지 않는다. 이전 세대는 참조 중인 프레임과
GPU fence가 끝날 때까지 유지한다. per-view history와 Host 기여 노드는 기존 수명
계약을 그대로 소비한다.

## 3. 구현 슬라이스 — 2026-10-01 재산정 32인일

| ID | 닫을 책임 | 선행 | 검증 게이트 | 인일 |
|---|---|---|---|---:|
| `CSRP-0` | 현재 19 node·per-view·Host 기여·PassSchema/ShaderMeta 소비자 실측, sealed A/B fixture | `BASE-0` | 현재 C++ 경로의 node/slot·픽셀·실패 변이 기준선. 계획의 오래된 파일명·호출 수 재계수 | 3 |
| `CSRP-1` | native immutable Pipeline IR, 안정 PassTypeId/slot/setting schema, C++ builder·registry | `CSRP-0`, RG1 단일 writer DAG 계약 | managed 없이 IR → `LivePipelineDesc` dump; missing producer, ID/schema/출력 root 오류를 GPU 전에 거부. version/Modify 경로는 RG2 이후 연다 | 6 |
| `CSRP-2` | C# `RenderPipelineBuilder`, typed handle, 한 번의 immutable interop 게시, 관리 예외/세대 실패 처리 | `CSRP-1` | C# 기본 파이프라인을 native IR과 같은 hash/dump로 재생성; Render/CommandBuild/RHI 스레드 managed callback 0 | 5 |
| `CSRP-3` | PassSchema–ShaderMeta/Keyword 합성, native/C# ID·layout와 permutation 일치 | `CSRP-2` | 키워드 중복·틀린 의미 슬롯·파라미터 범위·누락 필수 입력의 negative fixture | 4 |
| `CSRP-4` | Roslyn generator/analyzer와 Editor schema metadata | `CSRP-3` | 타입 있는 builder/interop/Editor 필드 생성; 생성 출력을 변경·누락시키는 변이가 빌드/검증을 붉게 만듦 | 6 |
| `CSRP-5` | 기본 19 node C# 저작 → native Pass 실행 제품 전환 | RG5·RG6, `CSRP-1~4` | 이전 C++ builder와 같은 밀봉 입력의 DX12 node·slot·픽셀·validation; 구 builder는 fixture 뒤 제품 경로에서 제거 | 4 |
| `CSRP-6` | native advanced Pass/조건·fallback·side effect·queue hint, reload·fence 수명 | `CSRP-5`; multi-queue hint는 Q0/RG8 | native escape가 graph에 사용/ownership을 알림, 지원 불가/재컴파일 실패 시 마지막 정상 generation 유지, per-view/history 회귀 | 4 |

`CSRP-1`의 native IR은 C#에 앞서 C++ builder로 검사한다. `CSRP-2`는 CLR 경계
한 번의 generation 게시를 먼저 닫고, `CSRP-3~4`는 실제 반복 schema를 근거로
생성기 범위를 정한다. `CSRP-5`는 `RG5`의 접근 선언 이관과 별개인 **조립부**
cutover이며, `RG6` 제품 픽셀 판정 뒤 수행한다. 그래프의 resource versioning을
`CSRP` 쪽에서 복제하지 않는다.

## 4. 제외·분리한 작업

- PHASE 4.3: versioned resource, stable DAG, culling/lifetime/barrier 재계산,
  aliasing, queue/fence RHI. 첫 DAG는 단일 writer에 한정하며 RG2가 version/Modify를 연다.
- PHASE 4.25: `.shadergraph(domain=material)`의 typed IR·round-trip·Slang codegen.
  Pass-domain Visual Shader Graph는 C# topology 저작의 필수 조건이 아니며 별도
  사용자 수요와 컴파일러 수직 슬라이스가 확인될 때 다시 산정한다.
- Native Pass 본체: GBuffer/Shadow/Forward/lighting의 scene packet·배치·PSO·
  SDK 수명은 C++에 남는다. 간단한 shader-only custom effect는 후속 template 후보.
- PHASE 4.5: temporal upscaling/FG의 SDK/present 소유권. C# Pipeline은 이들의
  capability와 출력 계약을 소비한다.

## 5. 완료 판정과 공수 규칙

완료는 `CSRP-5`의 기본 파이프라인 동등성과 `CSRP-6`의 실패/세대·native escape
검증을 모두 통과했을 때다. C#이 그래프의 barrier·fence·descriptor를 직접 기록하지
않고도 기본 패스를 재구성할 수 있어야 한다. 같은 frame에서 C++/C# builder의
Pass/slot/compiled order와 final HDR/display를 비교하고, 독립 Pass 순서 shuffle,
stale handle, 잘못된 keyword/queue, reload 실패 변이가 게이트를 붉게 만드는지 확인한다.

기존 SRP 33일과 완료된 구 `4-1` 2일은 새 C# 계획의 진척·예산으로 옮기지 않는다.
각 슬라이스는 실제 native/managed 소비 경계와 배포 표면을 재계수한 뒤 공수를 기록한다.
현재 문서 작성과 정적 대조는 구현·빌드·런타임 통과가 아니다.

## 2026-10-01 Editor viewer / 설정 착지 완료 조건

CSRP-5 제품 cutover는 [RG-V](RenderGraphDependencySchedulingPlan.md#rg-v--compiled-graph-읽기-전용-viewer-2026-10-01-추가)의
같은 read-only viewer에 C# IR 세대·native compiled node/slot/order/hash를 표시한다.
CSRP-6의 reload 실패/마지막 정상 세대·per-view/history 검증은 viewer의 표시 세대와
실제 실행 세대가 일치하는지도 포함한다. 이 연결이 없으면 CSRP-5/6을 완료로 닫지 않는다.
Project Settings Graphics의 pipeline 선택/설정은 W9의 shared schema를 소비한다.
SceneRenderProfile은 파라미터이며 C# RenderPipeline.Build 조립의 대체 저작 정본이 아니다.
공수는 CSRP-0~6 합계 32인일에 포함하며 viewer를 중복 계산하지 않는다.
