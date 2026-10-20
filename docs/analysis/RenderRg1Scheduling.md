# RG1 단일 writer 의존성 정렬 — 2026-10-03 완료

<!-- master-doc-review:start -->
## 2026-10-20 master 구현 대조

기준: `27de3f5a02aadde7456aab680db3184c36ee735e`. 대시보드는 변경하지 않는다. 아래 현행 대조와 소스 선언이 이전 시점의 구조·미구현 표기보다 우선한다.

[전체 대조·링크 정리 원장](MasterDocumentationAudit20261020.html)

### 현행 계약과 이 문서의 적용 범위

- 제품 live graph는 명시적 버전과 dependency order를 사용한다. 선언 metadata 재사용과 dependency recording은 현재 구현 기반이다.
- 소프트웨어 RHI의 frozen recording은 독립 target의 병렬 native 번역과 조기 sealed-batch handoff를 지원한다. 직렬 전용 번역·별도 C# SRP 저작 전환을 후속 필수 작업으로 두지 않는다.
- queue/alias/hazard·reader join·retirement를 보존하고 실제 overlap·fallback·whole-frame 비용으로 RG8 채택을 판정한다.

### 해당 구현에서 확인한 선언·소유 경계

- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h:75](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h#L75) — `enum class RGSchedulingMode { DeclarationOrder, ExplicitSingleWriter, ExplicitVersioned };`
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h:77](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h#L77) — `enum class RGOrderPolicy { DependencyOrder, PreserveDeclarationOrder };`
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h:231](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h#L231) — `class EnhancedRenderGraph : private IRHIUploadTransactionListener`
- [Tools/build.ps1:2](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L2) — `[CmdletBinding()]`
- [Tools/build.ps1:4](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L4) — `[ValidateSet('Game')][string]$Target = 'Game',`
- [Tools/build.ps1:21](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L21) — `$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))`
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp:184](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp#L184) — `RHITextureHandle EnhancedRenderGraph::ExecuteContext::ResolveHandle(RGHandle handle) const`
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp:194](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp#L194) — `void EnhancedRenderGraph::Reset()`
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp:239](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp#L239) — `RHIBufferHandle EnhancedRenderGraph::ResolveBufferHandle(RGHandle handle) const`

### 이전 근거의 유효 범위

기존 세부 계약은 위 현행 구현과 모순되지 않는 범위에서 유지한다. 과거 수치·실행 통과·해시·장치 조건은 당시 기록이다. 로컬 산출물이 사라진 항목은 링크 대신 경로와 미보존 상태를 표시하며, 현재 master의 재실행 통과로 승격하지 않는다.

<!-- master-doc-review:end -->

<details data-master-history><summary>세부 계약·원래 분석과 당시 측정 기록</summary>

RG3 이후 현행 소스/바이너리 기준선은 [RG3 검증 기록](RenderRg3LifetimeBarriers.md)을 따른다. 아래 RG1 artifact는 RG2 변경 전 증거로 보존한다.

같은 `EnhancedRenderGraph`의 `ExplicitSingleWriter` 모드에 명시적 Read/Write/ReadWrite 접근과
writer→reader RAW 의존성 정렬을 구현했다. ready 패스 중 최소 authored index를 선택해 안정적으로
정렬하며, 순환은 실제 패스·자원 사슬로 보고한다. 잘못된 handle/접근/상태, 누락 writer,
중복 접근, 다중 writer와 모호한 ReadWrite는 자원 할당 전에 컴파일을 실패시킨다.

imported Read는 writer가 없으면 외부 초기값을, 있으면 그 writer의 결과를 소비한다.
ReadWrite는 초기화된 imported 자원의 단독 사용만 허용한다. 접근과 RHI state를 분리했으므로
명시적 Read/UAV도 읽기로 처리하며 culling이 producer를 유지한다.

제품 기본값은 `DeclarationOrder`다. 기존 `{handle,state}` 선언은 이 호환 모드에서만 허용한다.
RG1은 정렬 알고리즘 완료이며, version/Modify는 RG2, 제품 접근 이관은 RG5, 제품 전환은 RG6이다.
명시적 ordering token은 이번 구현 범위에 포함하지 않는다.

## 검증 증거

- VS18/v145 Debug·Release Editor 빌드 성공. 최종 Debug 증거는
  `Build/rg1-Debug-closure-build.log`, Release는 `Build/rg1-Release-final-build.log`다.
- 두 구성의 `dx12-0/graph-fixtures.results.jsonl`에서 `RG1_DAG_OK shuffles=24`와
  `RG1_GPU_OK reversed-RAW pixels=0`을 확인했다. shuffle은 RAW/UAV-read producer 유지와
  독립 패스 및 새로 ready가 된 패스의 authored-index 우선순위를 검사한다.
  순환·누락 writer·다중 writer·Modify 모호성·invalid handle·중복·접근/상태 오류 거부도 통과했다.
- 실제 GPU fixture는 transient texture 소비 패스를 먼저 선언하고 clear writer를 나중에 선언했다.
  실행 순서가 생산→소비로 바뀌었고 readback 픽셀 불일치가 0이었다.
- `Build/Obj/RenderRG1/{Debug,Release}-regression-v1/result.json`: 구성별 독립 프로세스
  2개×캡처 2개, graph/변이/계측 게이트 통과, 소스 변경 0, 실패 0, 정상 종료 코드 0.
- `Debug-before-after-v1`, `Release-before-after-v1`, `Debug-Release-v1`의 `comparison.json`:
  각각 16개 이미지 maxError=0, exceededPixels=0. 고정 입력·환경·자산·tuning도 일치했다.
- `Build/Obj/RenderRG1/current-baseline-phase-complete-v1.json`은 현재 바이너리/소스 해시를
  다시 검증한 기준선 증거다. `rg1-completion-v1.json`은 이 기준선에 RG1 native 검증과
  변경 전후 대조를 결합한 완료 기록이다. 기존 BASE-0 v4는 변경 전 역사적 기준선으로 보존한다.

IBL 1024/4096과 기존 이미지 상한을 유지했다. 이 결과는 성능 개선이나 MAT-9 품질·성능 수용을
뜻하지 않는다. Lattice 재생은 선택 확장으로 유지하고 Vulkan 비교는 PHASE 4.9에 남긴다.

</details>
