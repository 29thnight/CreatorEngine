# RG5-1 GBuffer·Shadow 생산자 선언 — 2026-10-03 검증 완료

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

RG5의 첫 이관 묶음이다. GBuffer의 diffuse/metalRough/normal/emissive/bitmask/depth와 Shadow.Cascades는 새 transient 자원을 완전히 초기화한다. ExplicitVersioned에서는 CreateTexture 뒤 Write로 버전 0을 만들고 실제 제품 출력 handle과 callback 소비에 전달한다. 두 패스의 비기본 모드는 explicit Write 접근을 선언한다.

DeclarationOrder는 여전히 제품 기본이며 현재 BuildOrder가 explicit 접근을 거부하고 Write도 버전 모드에서만 동작한다. 이 첫 묶음은 기존 기본 경로를 유지하는 모드 분기를 둔다. 이를 RG5 전체 완료로 계산하지 않는다. 선언 모델과 정렬 정책을 분리해 legacy 추론·분기를 정리하는 후속 RG5 작업이 필요하다. RG6의 제품 기본 전환을 앞당기지 않는다.

착수 시 59개 제품 호출, 201개 게이트 호출을 확인했다. 새 검사 호출 1개를 추가한 현재 총합은 제품 59·게이트 202이다. Editor/RenderTests뿐 아니라 Tools/regression의 포인터 호출 18개도 게이트에 포함한다. 이는 정적 호출 위치 수이며 live node 수가 아니다. Build/Obj/RenderRG5Producers/declaration-inventory.json이 파일·행 근거다.

검사는 실제 GBuffer/Shadow Declare를 호출한다. 세 모드 × 두 선언 순서에서 출력 7개·버전 kind/epoch·explicit Write·RAW 7개·producer/consumer 생존을 확인한다. 이 검사는 fake services의 계획 검사이며 versioned 제품 드로우의 GPU 수용을 대신하지 않는다. 제품 기본 DX12 고정 장면 회귀와 변경 전후 대조는 별도로 수행한다.

RG4의 현재 layer-ID fixture와 Debug/Release 기준선을 변경 전 증거로 사용한다. IBL 1024/4096·기존 이미지 상한을 유지한다. MAT-9는 열려 있고, Lattice 재생은 선택 확장, Vulkan 비교는 PHASE 4.9다.
Debug/Release 빌드는 Build/rg5-producers-Debug-build.log와 Build/rg5-producers-Release-build.log로 통과했다. 두 구성의 실제 Declare 계획 검사에서 RG5_PRODUCERS_OK passes=GBuffer/Shadow modes=3 orders=2 outputs=7 RAW=7을 확인했다.

현행 기준선은 Build/Obj/RenderRG5Producers/Debug-regression-v1 및 Release-regression-v1의 result.json이다. 각각 두 프로세스 × 두 캡처, complete=true, 실패 0, 강제 종료 없음, exitCode=0이다. current-baseline-phase-complete-v1.json은 현행 소스·바이너리 해시를 검사해 phaseComplete=true를 판정했다.

RG4 기준선과 Debug/Release 변경 전후 대조, 현행 Debug/Release 구성 간 대조는 각각 16개 이미지 모두 maxError=0·exceededPixels=0이며 입력 및 전체 그래프 해시도 일치했다. 해당 comparison.json은 Debug-before-after-v1, Release-before-after-v1, Debug-Release-v1 아래에 있다.

rg5-producers-completion-v1.json은 sliceComplete=true, rg5Complete=false, versionedProductGpuAcceptance=false다. RG5 전체의 기성은 0으로 유지한다. 다음 묶음은 선언 모델과 정렬 정책 분리 및 consumer/ReadWrite 이관이다. 이후 legacy 추론 제거와 versioned 제품 GPU·전체 프레임 수용을 검증한다. 제품 기본은 DeclarationOrder이며 Vulkan 대조는 PHASE 4.9에서 수행한다.

</details>
