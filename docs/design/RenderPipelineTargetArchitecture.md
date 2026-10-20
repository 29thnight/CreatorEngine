# Render Pipeline — C++ Pass 확장과 실행 경계

<!-- master-doc-review:start -->
## 2026-10-20 master 구현 대조

기준: `27de3f5a02aadde7456aab680db3184c36ee735e`. 대시보드는 변경하지 않는다. 아래 현행 대조와 소스 선언이 이전 시점의 구조·미구현 표기보다 우선한다.

[전체 대조·링크 정리 원장](../analysis/MasterDocumentationAudit20261020.html)

### 현행 계약과 이 문서의 적용 범위

- 제품 live graph는 명시적 버전과 dependency order를 사용한다. 선언 metadata 재사용과 dependency recording은 현재 구현 기반이다.
- 소프트웨어 RHI의 frozen recording은 독립 target의 병렬 native 번역과 조기 sealed-batch handoff를 지원한다. 직렬 전용 번역·별도 C# SRP 저작 전환을 후속 필수 작업으로 두지 않는다.
- queue/alias/hazard·reader join·retirement를 보존하고 실제 overlap·fallback·whole-frame 비용으로 RG8 채택을 판정한다.
- 구 SRP 및 C# pipeline 저작 전환 제안은 취소됐다. 필요한 Pass는 현재 C++ 계약으로 작성하며, 별도 저작 체계는 실제 요구가 생기면 새 미래 계획으로 검토한다. 아래 원래 제안은 활성 계획이 아니다.

### 해당 구현에서 확인한 선언·소유 경계

- [Tools/build.ps1:2](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L2) — `[CmdletBinding()]`
- [Tools/build.ps1:4](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L4) — `[ValidateSet('Game')][string]$Target = 'Game',`
- [Tools/build.ps1:21](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L21) — `$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))`

### 이전 근거의 유효 범위

기존 세부 계약은 위 현행 구현과 모순되지 않는 범위에서 유지한다. 과거 수치·실행 통과·해시·장치 조건은 당시 기록이다. 로컬 산출물이 사라진 항목은 링크 대신 경로와 미보존 상태를 표시하며, 현재 master의 재실행 통과로 승격하지 않는다.

<!-- master-doc-review:end -->

<details data-master-history><summary>세부 계약·원래 분석과 당시 측정 기록</summary>

> 2026-10-20 사용자 결정: 구 SRP 및 C# Pipeline 저작 전환을 취소했다. 필요한 기능은 기존 C++ Pass 경로로 작성한다. 별도 저작 체계는 구체적인 필요가 생길 때 미래 계획으로 다시 검토한다.

[현재 렌더 그래프 잔여](../plans/RenderGraphDependencySchedulingPlan.html) · [취소 범위와 전체 문서 대조](../plans/MasterPlanAudit20261020.html#cancelled-srp)

## 유지하는 실행 경계

C++ Pass는 기존 LivePipelineDesc와 sealed frame 입력을 소비한다. EnhancedRenderGraph가 리소스 버전·DAG·수명·배리어·기록 스케줄을 소유하고 RHI가 backend 제출을 수행한다. EnhancedSceneRenderer의 멀티뷰·in-flight·완료/표시 승격 수명은 유지한다.

## 제거한 목표

C# RenderPipeline.Build, Roslyn Pipeline builder, authored Pipeline Asset 전환과 고정 C++ 조립 제거는 실행 계획이 아니다. 기존 compiled graph viewer는 읽기 전용 진단으로 유지한다.

<details><summary>이전 절 링크</summary>

<span id="passschema와-c-표면"></span>
<span id="sec-passschema와-c-표면"></span>
<span id="sec-단계와-완료-증거"></span>
<span id="sec-리소스-의미와-순서"></span>
<span id="sec-무엇을-대체하는가"></span>
<span id="sec-책임-경계"></span>
<span id="단계와-완료-증거"></span>
<span id="리소스-의미와-순서"></span>
<span id="무엇을-대체하는가"></span>
<span id="이전-srp-계획과의-비교"></span>
<span id="책임-경계"></span>

</details>

</details>
