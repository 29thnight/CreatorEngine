# RG5-12 화면 SSS·SSR 접근·출력 이관 — 완료

SSS의 두 축과 SSR 소비 체인을 명시 접근으로 이관했다. Material Graph의 Subsurface 소유자(RG5-7)와 별개인 화면 전체 후처리 pass다. 기본 제품 DeclarationOrder와 기존 셰이더·품질 tuning은 유지한다.

## 배선

SSS는 입력 색상·깊이를 Read하고 Horizontal 초기 버전을 Write한다. Vertical은 Horizontal·깊이를 Read하고 최종 출력의 초기 버전을 Write한다. SSR은 최신 색상과 깊이·metalRough·normal·bitmask를 Read하고 별도 출력의 초기 버전을 Write한다. 세 새 transient 출력은 ExplicitVersioned에서 v0이다. 입력 색상을 제자리에서 수정하는 구조가 아니므로 이 출력에 Modify를 적용하지 않는다.

실행 callback은 선언 당시의 source/target/depth 또는 Inputs/output을 값으로 보관한다. 기존 SceneRenderer 블랙보드가 반환 출력을 다음 색상 입력으로 전달한다. SSS/SSR 비활성 경로 및 SSR 입력 누락 시 입력 핸들을 그대로 전달하는 계약은 유지한다. 수정한 if/for는 다중 행 스코프를 사용하며 namespace 내부를 들여썼다.

## 실제 GPU 검증 — 완료

Editor dx12.rendergraph에 EnhancedScreenRg5Tests.h를 연결하고 RenderTests 프로젝트에 등록했다. verify-rg5-screen.ps1은 현재 소스·Editor executable/runtime SHA-256을 실행 전후 확인하며 GPU validation 활성·오류/경고/유실 0을 요구한다.

- Debug-native-v1, Release-native-v1: 각각 3정책 × 16장면 = 48 frames, maxError=0, validationProblems=0, exitCode=0.
- DeclarationOrder, ExplicitVersioned+PreserveDeclarationOrder, ExplicitVersioned+DependencyOrder를 실행한다. SSS on/off × SSR on/off × uniform/pattern/mask-skip/missing-normal을 조합한다.
- 실제 Sprite로 패턴 입력을 그린다. 원본·Horizontal·SSS 최종·SSR 최종의 16×16 전체 RGBA를 기준 정책과 정확히 비교한다. 명시 정책에서 LegacyState 접근 0, 활성 pass 수, 새 출력 v0 및 비활성/입력 누락의 반환 버전 보존을 확인한다.
- uniform SSS 색상 보존(허용 오차 0.002), 패턴에 실제 블러 변화, 실제 SSR 반사 RGB 기여를 독립 조건으로 검사한다. SSR 비활성·입력 누락·bitmask skip은 SSS 출력과 정확히 같아야 한다. 선언 후 SetInputs({})를 호출하여 실제 GPU 실행이 선언 당시 핸들을 소비하는지 검사한다.
- fixture tuning만 SSS width=0.2, SSR stepSize=0.1/maxThickness=0.5/maxRayCount=4/time=0.25로 고정하여 블러·반사 기여를 관측한다. 제품 기본 tuning은 변경하지 않았다. 이 검사는 MAT-9 최종 SSS/투과 품질 판정이 아니다.
- 기존 RG1~RG5 및 Sprite GPU RenderGraph 검증도 함께 통과했다. 최종 빌드 로그: Build/rg5-screen-editor-{Debug,Release}-build.log.

## 제품 기준선 — 완료

증거 루트: Build/Obj/RenderRG5Screen.

- Debug-regression-v1, Release-regression-v1: 기본 제품 경로에서 구성별 독립 프로세스 2회 × 캡처 2회 통과. 실패 0, 정상 종료, 강제 종료 없음.
- Debug-before-after-v1, Release-before-after-v1, Debug-Release-v1: 직전 RG5-11 기준선 대비와 구성 간 각 16개 이미지의 maxError/RMSE/changedPixels/exceededPixels 모두 0. 동일 inputHash/full graphHash.
- current-baseline-phase-complete-v1.json: 현행 소스·Editor executable/runtime SHA-256 일치 및 phaseComplete=true.
- rg5-screen-completion-v1.json: sliceComplete=true, screenGpuAcceptance=true, versionedProductGpuAcceptance=false, rg5Complete=false.

기본 제품 대표 장면 회귀와 16×16 화면 SSS/SSR 체인의 세 정책 GPU 검증을 구분한다. 전체 SceneRenderer versioned GPU 수용과 RG6 기본 전환을 완료했다고 판정하지 않는다.

## 남은 범위

VolumetricFog·후처리·UI 및 Editor/캡처/fixture 소비자, legacy 추론·adapter 제거, 전체 SceneRenderer versioned GPU 수용과 RG6 기본 의존성 정렬 전환은 남아 있다. 제품 기본 DeclarationOrder, RG5 progress/기성 0 및 잠정 10인일을 유지한다.

정적 cpp/h 호출은 제품 59·게이트 224(이번과 직전 신규 검사 헤더 포함)이다. inl 전체 목록과 구분하며 호출 수로 이관 완료를 판정하지 않는다. 증거: Build/Obj/RenderRG5Screen/declaration-inventory.json.

IBL 1024/4096 및 MAT-9의 SSS·투과 품질/성능 조건은 유지한다. Vulkan 비교는 PHASE 4.9 RenderDoc 캡처→리소스 확인→픽셀별 비교에서 진행한다.
