# RG3 버전 producer 컬링·수명·배리어 — 2026-10-03 완료

<!-- master-doc-review:start -->
## 2026-10-20 master 구현 대조

기준: `27de3f5a02aadde7456aab680db3184c36ee735e`. 대시보드는 변경하지 않는다. 아래 현행 대조와 소스 선언이 이전 시점의 구조·미구현 표기보다 우선한다.

[전체 대조·링크 정리 원장](MasterDocumentationAudit20261020.html)

### 현행 계약과 이 문서의 적용 범위

- 제품 live graph는 명시적 버전과 dependency order를 사용한다. 선언 metadata 재사용과 dependency recording은 현재 구현 기반이다.
- 소프트웨어 RHI의 frozen recording은 독립 target의 병렬 native 번역과 조기 sealed-batch handoff를 지원한다. 직렬 전용 번역·별도 C# SRP 저작 전환을 후속 필수 작업으로 두지 않는다.
- queue/alias/hazard·reader join·retirement를 보존하고 실제 overlap·fallback·whole-frame 비용으로 RG8 채택을 판정한다.

### 해당 구현에서 확인한 선언·소유 경계

- [Tools/build.ps1:2](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L2) — `[CmdletBinding()]`
- [Tools/build.ps1:4](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L4) — `[ValidateSet('Game')][string]$Target = 'Game',`
- [Tools/build.ps1:21](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L21) — `$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))`

### 이전 근거의 유효 범위

기존 세부 계약은 위 현행 구현과 모순되지 않는 범위에서 유지한다. 과거 수치·실행 통과·해시·장치 조건은 당시 기록이다. 로컬 산출물이 사라진 항목은 링크 대신 경로와 미보존 상태를 표시하며, 현재 master의 재실행 통과로 승격하지 않는다.

<!-- master-doc-review:end -->

<details data-master-history><summary>세부 계약·원래 분석과 당시 측정 기록</summary>

`ExplicitVersioned` 모드의 컬링을 물리 자원의 모든 writer 검색에서 특정 버전 producer
역추적으로 바꿨다. Read는 해당 버전, Modify는 이전 버전 producer를 살린다.
부작용 패스와 imported 자원의 최종 출력 writer가 root다. 중간 출력 자체를 보존해야 하는
패스는 hasSideEffect를 명시한다. 제품 기본 DeclarationOrder의 기존 root/컬링 계약은 유지한다.

접근·버전·producer 검증은 제거 대상 패스에도 적용한다. 이후 데이터 의존성으로 살아남은
패스만 정렬하며 순환은 이 DAG에서 판정한다. WAR/WAW는 패스 생존을 결정하지 않는다.
죽은 버전 writer를 건너뛸 때 살아남은 이전 writer 및 reader를 다음 writer에 다시 연결해
같은 물리 저장소의 덮어쓰기 순서를 보존한다. 진단 edge도 살아남은 DAG를 기록한다.

CreateTransients의 기존 compiled-index 수명 계산과 PlanBarriers의 기존 실행 순서 추적에
컬링 후 순서를 전달한다. 죽은 자원은 used=false로 남아 할당을 생략하고, 죽은 패스의 배리어는
비운다. versioned UAV는 read→read 배리어를 생략하고 write→read/read→write/write→write
배리어를 유지한다. imported/pooled UAV의 최초 접근에는 이전 작업이 있을 수 있어 보수적으로
배리어를 둔다. texture와 buffer에 같은 접근 계약을 적용한다.

진단의 reachability는 versioned RAW edge로 기록한다. GetTransientLifetime은 physical 자원의
살아남은 접근 전체를 포함하는 compiled-index 범위이며 stale/foreign version handle을 거부한다.
제품 패스 접근 이관은 RG5, 전환은 RG6, transient buffer 생성/aliasing은 RG7에 남긴다.

## 검증

최종 Debug/Release 빌드, native 검사와 고정 장면 회귀·변경 전후 대조를 모두 통과했다.

- 빌드: `Build/rg3-Debug-final-build.log`, `Build/rg3-Release-final-build.log`.
- `Build/Obj/RenderRG3/{Debug,Release}-regression-v2/result.json`: 구성별 독립 프로세스
  2개×캡처 2개, graph/변이/계측 게이트 통과, 소스 변경·실패 0, 정상 종료 코드 0,
  ReplayExtensions=false.
- 각 구성 `dx12-0/graph-fixtures.results.jsonl`: `RG3_PLAN_OK shuffles=120`과
  `RG3_GPU_OK reversed-RAW-WAR-WAW old-version-pixels=0`.
- `Debug-before-after-v2`, `Release-before-after-v2`, `Debug-Release-v2`의 `comparison.json`:
  각각 16개 이미지 maxError=0·exceededPixels=0. 구성 간 환경·자산·tuning identity도 일치했다.
- `current-baseline-phase-complete-v2.json`: 현재 소스·executable/runtime DLL 해시를
  재검증한 두 구성 기준선 증거. `rg3-completion-v2.json`은 이 기준선에 native RG3 검사와
  변경 전후 대조를 결합한 완료 기록이다. RG2 artifact는 변경 전 기준선으로 보존한다.

검사 범위는 다음과 같다.

- 120 shuffle: 죽은 중간 writer 제거, WAR/WAW 연결, 정렬 후 first/last use와 각 transition
  before/after, 죽은 자원/배리어 및 진단 edge 제거를 확인한다.
- imported 최종 출력 root, Modify 입력 유지, WAR가 죽은 reader를 살리지 않는 경우,
  죽은 잘못된 선언 거부, texture/buffer UAV 접근별 배리어와 stale lifetime 거부도 검사한다.
- GPU fixture는 이전 버전을 읽기 전에 중간 writer와 마지막 덮어쓰기를 먼저 선언한다.
  중간 writer가 제거되고 실제로 실행되지 않으며, readback은 덮어쓰기 전 색을 유지해야 한다.
- 첫 Debug precheck 및 regression-v1은 GPU fixture에 추가한 패스 때문에 기대 선언 인덱스가
  바뀐 것을 반영하지 못해 실패했다. 이 결과를 보존하고 기대값을 수정했다.
  Debug-native-precheck-v2는 계획 검사와 GPU 이전 버전 픽셀 검사를 통과했다.

IBL 1024/4096과 기존 이미지 상한을 유지한다. MAT-9 품질·성능 게이트는 열려 있고,
Lattice 재생은 선택 확장, Vulkan 비교는 PHASE 4.9다. GPU 성능 개선은 아직 주장하지 않는다.

</details>
