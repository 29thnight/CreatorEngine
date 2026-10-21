# RG5-10 Decal 접근·출력 버전 이관 — 완료

<!-- master-doc-review:start -->
## 2026-10-20 master 구현 대조

기준: `27de3f5a02aadde7456aab680db3184c36ee735e`. 대시보드는 변경하지 않는다. 아래 현행 대조와 소스 선언이 이전 시점의 구조·미구현 표기보다 우선한다.

[전체 대조·링크 정리 원장](MasterDocumentationAudit20261020.html)

### 현행 계약과 이 문서의 적용 범위

- 제품 live graph는 명시적 버전과 dependency order를 사용한다. 선언 metadata 재사용과 dependency recording은 현재 구현 기반이다.
- 소프트웨어 RHI의 frozen recording은 독립 target의 병렬 native 번역과 조기 sealed-batch handoff를 지원한다. 직렬 전용 번역·별도 C# SRP 저작 전환을 후속 필수 작업으로 두지 않는다.
- queue/alias/hazard·reader join·retirement를 보존하고 실제 overlap·fallback·whole-frame 비용으로 RG8 채택을 판정한다.

### 해당 구현에서 확인한 선언·소유 경계

- [Tools/regression/verify-rg5-decal.ps1:7](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-rg5-decal.ps1#L7) — `$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))`
- [Tools/regression/verify-rg5-decal.ps1:8](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-rg5-decal.ps1#L8) — `$output = [IO.Path]::GetFullPath($OutputDirectory)`
- [Tools/regression/verify-rg5-decal.ps1:42](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-rg5-decal.ps1#L42) — `$marker = [regex]::Match($log, '(?m)^RG5_DECAL_GPU_OK policies=3 frames=([1-9][0-9]*) maxError=([0-9.eE+-]+) validation=0\r?$')`
- [Tools/build.ps1:2](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L2) — `[CmdletBinding()]`
- [Tools/build.ps1:4](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L4) — `[ValidateSet('Game')][string]$Target = 'Game',`
- [Tools/build.ps1:21](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L21) — `$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))`

### 이전 근거의 유효 범위

기존 세부 계약은 위 현행 구현과 모순되지 않는 범위에서 유지한다. 과거 수치·실행 통과·해시·장치 조건은 당시 기록이다. 로컬 산출물이 사라진 항목은 링크 대신 경로와 미보존 상태를 표시하며, 현재 master의 재실행 통과로 승격하지 않는다.

<!-- master-doc-review:end -->

<details data-master-history><summary>세부 계약·원래 분석과 당시 측정 기록</summary>

Decal snapshot과 적용 pass의 접근을 명시하고, 수정된 GBuffer 버전을 후속 소비자에게 전달했다. 제품 기본 DeclarationOrder와 RG5 전체 progress/기성 0은 유지한다.

## 구현

snapshot은 입력 GBuffer를 Read하고 세 복사 대상의 초기 버전을 Write한다. 적용 pass는 diffuse·normal·metalRough를 Modify/ReadWrite하고 snapshot·depth·owner bitmask를 Read한다. 준비된 Decal 원본 텍스처는 실제 업로드 완료 상태인 PixelShaderResource로 import하고 Read하며 중복 핸들을 제거한다. 명시 모드에만 원본 import를 추가하여 기존 기본 경로를 보존했다.

callback은 입력·snapshot·출력 핸들을 값으로 보관한다. GetOutputs가 수정된 세 GBuffer 버전을 반환하고 SceneRenderer 블랙보드와 SceneHost의 DeclareDecalInputs가 이를 후속 Color/Lookup 입력에 연결한다. Decal이 없으면 입력 버전을 그대로 반환한다. 수정한 if/for에는 다중 행 스코프를 작성하고 namespace 내부를 들여썼다.

## 검증

증거 루트는 Build/Obj/RenderRG5Decal, 실행기는 Tools/regression/verify-rg5-decal.ps1이다.

- Debug/Release probe와 Editor 최종 빌드 통과: Build/rg5-decal-{Debug,Release}-final-build.log 및 rg5-decal-editor-{Debug,Release}-final-build.log.
- Debug-gpu-v2, Release-gpu-v1: 각각 DeclarationOrder, ExplicitVersioned+PreserveDeclarationOrder, ExplicitVersioned+DependencyOrder의 3정책 × 114 frames = 342 frames. 두 품질 tier와 sequential/1-worker/4-worker 기록·제출을 포함하며 maxError=0, validationProblems=0, exitCode=0이다.
- 실제 SceneHost GBuffer·Decal·후속 Color/Lookup GPU 경로를 실행한다. HDR·GBuffer 3개·Lookup 입력 3개의 전체 픽셀을 정책 간 비교하여 오차 0을 확인했다. 독립 Decal 계산식, 제거 후 정확한 HDR 복원, cache 검사와 잘못된 snapshot/중복 선언 거부도 유지했다. 정책별 cache 통계 차이가 있는 IBL/통계 버퍼는 정책 간 원시 픽셀 비교에서 제외하며 기존 독립 검사는 유지한다.
- Debug-regression-v1, Release-regression-v1: 기본 제품 경로에서 구성별 독립 프로세스 2회 × 캡처 2회 통과. 실패 0, 정상 종료, 강제 종료 없음.
- Debug-before-after-v1, Release-before-after-v1, Debug-Release-v1: 직전 RG5-9 기준선 대비와 구성 간 각 16개 이미지의 maxError/RMSE/changedPixels/exceededPixels 모두 0.
- current-baseline-phase-complete-v1.json: 현행 소스·Editor executable/runtime SHA-256 일치 및 phaseComplete=true.
- rg5-decal-completion-v1.json: sliceComplete=true, decalGpuAcceptance=true, versionedProductGpuAcceptance=false, rg5Complete=false.

최초 Debug-gpu-v1은 원본 텍스처를 전체 ShaderResource 상태로 import하여 실제 PixelShaderResource 업로드 상태와 불일치하는 GPU validation 오류가 발생했다. 초기 상태를 실제 업로드 계약에 맞춰 수정한 Debug-gpu-v2와 Release-gpu-v1에서 오류 0을 확인했다. 실패 로그는 보존한다.

## 남은 범위

16×16 Decal 체인 검증과 기본 제품 회귀를 전체 SceneRenderer의 versioned GPU 수용으로 확대하지 않는다. Sprite·화면 SSS/SSR·Fog·후처리·UI 등 남은 소비자, Editor/캡처/fixture 및 legacy 추론·adapter 제거와 전체 제품 versioned GPU 수용이 남아 있다. RG6 기본 의존성 정렬 전환은 이후 조건이다.

IBL 1024/4096 및 MAT-9의 SSS·투과 품질/성능 완료 조건은 유지한다. Vulkan 비교는 PHASE 4.9의 RenderDoc 캡처→리소스 확인→픽셀별 비교에서 진행한다.

현행 기준선은 후속 [RG5-11 Sprite 이관 검증](RenderRg5SpriteMigration.md)으로 갱신했다. 이 문서의 RG5-10 증거는 보존한다.

</details>
