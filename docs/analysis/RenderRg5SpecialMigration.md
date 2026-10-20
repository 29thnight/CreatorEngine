# RG5-7 Refraction·Subsurface·Volume 소유자 버전 핸들 — 완료

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

Refraction/SSS 입력 캡처는 소유자의 DeclareCaptureOutputs가 Write 버전을 반환하며 SceneHost가 그 핸들을 선언한다. Refraction Background는 lighting/depth Read 및 복사 목적지 Write를 선언하고 콜백에 그 선언 당시의 출력 핸들을 값으로 보관한다. Bake sample은 Write, SSS reflection/irradiance도 각각 Write다.

Lookup GraphInputs는 graph/epoch/upload/descriptor 소유권을 검사한 뒤 최신 캡처 버전을 반환한다. Refraction Bake와 SSS Reflection은 FindImportedTexture의 v0 복구 대신 이를 읽는다. 물리 자원·PSO·캐시·제출 수명 계약은 유지한다.

Volume은 한 번의 완전한 계수 선언과 합성 선언 계약을 유지하며 coefficient를 v1, transient output을 v0 Write로 선언한다. 입력 lighting/depth/shadow/environment와 계수는 Read다. 여러 매질이 공유하는 동일 material texture 읽기는 한 번만 선언한다. 합성 콜백도 출력 핸들을 값으로 보관한다. 환경 복구 pass는 명시적 Read로 상태 계약을 복구한다.

실제 세 자원 소유자를 생성하는 native 계획 검사: 닫힌 사면체 2개·보존된 합성 재질/계수 파이프라인·두 순서 정책, Refraction/SSS 캡처·Bake·필터 두 번의 v1/v2와 transient 배경 v0/v1, Volume 계수 v1/출력 v0, WAR, 중복 합성 거부. 캡처와 계수 파이프라인은 계획용 합성 fixture이며 실제 versioned Graph 혼합 셰이딩 GPU 수용으로 계산하지 않는다.

제품 기본은 DeclarationOrder다. GraphSurface/Draw 접근 선언·중복 읽기 정리·DeclareBlended 갱신 색상 반환·혼합 GPU 검증은 후속 범위다. Forward Graph guard와 RG5 progress/earnedDays 0을 유지한다. Vulkan 비교는 4.9에 유지한다.

검증은 Build/Obj/RenderRG5Special에 저장한다. 이전 Lookup 회귀의 종료 지연을 반영하여 Build/verify-rg5-lookup-retry.ps1의 native 종료 대기 240초를 사용한다. 원본 검증 스크립트/소스 해시 대상과 이미지 상한은 유지한다.


검증 완료: Build/rg5-special-Debug-final-build.log와 Release-final-build.log 빌드 통과. Debug-native-v2/Release-native-v2 및 최종 제품 회귀 내 graph-fixtures에서 RG5_SPECIAL_OK marker 통과. Refraction Bake/SSS Reflection이 두 반복에서 Lookup 최신 입력 11개씩을 읽는 것도 snapshot으로 검사한다. Debug-native-v1의 transient 출력 버전 기대값 오류는 기록을 보존했으며, 실제 계약(imported 첫 Write v1, transient 첫 Write v0)에 맞춘 v2 검사가 통과했다.

제품 회귀는 Build/Obj/RenderRG5Special/{Debug,Release}-regression-v1이다. 구성별 독립 프로세스 2회 × 캡처 2회, 실패 0·강제 종료 없음·종료 코드 0. 이전 Lookup 기준선과 변경 전후 Debug/Release 비교 및 현재 구성 간 비교 각각 16개 이미지의 maxError/RMSE/changedPixels/exceededPixels 모두 0, 동일 inputHash/full graphHash를 확인했다. current-baseline-phase-complete-v1.json의 phaseComplete=true는 현재 소스·실행 파일·runtime SHA-256과 두 구성 identity 일치를 확인한 판정이다.

rg5-special-completion-v1.json: sliceComplete=true, rg5Complete=false, versionedProductGpuAcceptance=false, productDefault=DeclarationOrder. declaration-inventory.json: 제품 59·게이트 213 호출. 전체 4.x 공수 355·기성 88 및 RG5 progress/earnedDays 0은 유지한다. IBL 1024/4096·MAT-9 별도 품질/성능 게이트도 유지한다.

다음 구현은 GraphSurface/Draw 자원 접근 선언과 중복 읽기 정리, DeclareBlended의 갱신 색상 반환이다. Code/Graph 혼합 스트림 실제 GPU 검증을 마친 뒤 guard를 제거한다. 나머지 소비자/ReadWrite·legacy 추론 제거와 RG6 기본 정렬 전환은 계속 열린 조건이다.

</details>
