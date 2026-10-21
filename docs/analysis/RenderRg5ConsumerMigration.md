# RG5-3 Deferred·SkyBox 소비 체인 — 2026-10-03 검증 완료

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

Deferred의 GBuffer 5개 입력·선택 Shadow/AO 입력에 명시적 Read를, 새 조명 출력에 Write를 연결했다. SkyBox는 새 출력이면 Write, 기존 조명 색상이면 Modify/ReadWrite를 선언한다. 자체 깊이는 Write, 입력 깊이는 Read이며 기존 깊이 버전을 보존한다. 제품 blackboard는 이미 SkyBox.GetOutput()을 후속 LitColor로 전달한다.

실제 패스 Declare 검사는 두 실행 순서 정책 × 자체/연결 출력 경로를 검사한다. 연결 경로는 GBuffer→Shadow→Deferred→이전 조명 독자→SkyBox→색상 소비자로 구성한다. 이전 조명 독자와 SkyBox 사이 WAR·색상 버전 1·깊이 버전 0·legacy access 부재를 확인한다. SkyBox PSO는 실제 DX12로 초기화하되 이 검사는 Compile 계획 검사다. 버전 기반 전체 제품 드로우의 GPU 검증으로 계산하지 않는다.

제품 기본 DeclarationOrder 및 호환 분기는 유지한다. 나머지 소비자·반복 쓰기·히스토리·Pipeline compiler·legacy 추론 제거와 RG6 전환은 열려 있다. IBL 1024/4096, MAT-9 열린 상태, Vulkan 비교 PHASE 4.9를 유지한다.

Debug/Release 빌드와 Build/Obj/RenderRG5Consumers/{Debug,Release}-native-v1/result.json이 통과했다. RG5_CONSUMERS_OK Deferred/SkyBox policies=2 paths=2 Modify/WAR/depth를 두 구성에서 확인했다. 전체 장면 기준선 갱신과 변경 전후 대조도 통과했다.

정적 호출 분류 정정: RG5-1에 저장된 declaration-inventory.json은 실제로 제품 59·게이트 202였다. 문서의 70·191은 집계 전사 오류여서 바로잡았다. RG5-3 현재 목록은 제품 59·게이트 206, 합계 265개 호출 위치다. 이 수는 live node 수가 아니다.

현행 기준선은 Build/Obj/RenderRG5Consumers/{Debug,Release}-regression-v1/result.json이다. 두 구성 각각 두 프로세스 × 두 캡처를 통과했고 실패 0, 정상 종료 exitCode=0이다. RG5-1 대비 Debug/Release 변경 전후 및 현행 구성 간 비교는 각각 16개 이미지 maxError=0·exceededPixels=0이고 입력·전체 그래프 해시가 일치했다. current-baseline-phase-complete-v1.json은 현행 소스/실행 바이너리 해시를 확인한 phaseComplete=true다.

rg5-consumers-completion-v1.json은 sliceComplete=true, rg5Complete=false, versionedProductGpuAcceptance=false로 기록한다. 실제 제품 기본 DeclarationOrder의 화면 회귀와 버전 선언 계획 검증을 구분한다. 다음은 SSAO·SSGI·Forward+ 등 남은 소비자와 반복 쓰기 이관이다. RG5 전체 진행/기성 0을 유지한다.

</details>
