# RG5-10 Decal 접근·출력 버전 이관 — 완료

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
