# RG5-11 Sprite 접근·출력 버전 이관 — 완료

월드 Sprite/3D Canvas pass가 기존 HDR에 합성할 때 Modify/ReadWrite를 사용하고, 독립 출력은 초기 Write 버전을 생성하도록 이관했다. 깊이는 Read로 선언하고 실제 업로드 완료 상태 PixelShaderResource의 원본 텍스처를 import/Read한다. FindImportedTexture와 중복 usage 검사로 같은 텍스처를 여러 배치가 사용해도 import·읽기를 하나로 유지한다.

실행 callback은 출력·깊이 핸들을 값으로 보관한다. GetOutput을 소비하는 기존 블랙보드 배선은 최신 색상 버전을 전달한다. Sprite가 비어 있으면 입력 색상을 그대로 반환한다. 수정한 if/for에는 다중 행 스코프를 작성하고 namespace 내부를 들여썼다.

## 실제 GPU 검증 — 완료

Editor의 dx12.rendergraph에 EnhancedSpriteRg5Tests.h를 연결했다. verify-rg5-sprite.ps1은 현재 소스·Editor executable/runtime SHA-256을 실행 전후 확인하며 GPU validation이 활성화되고 오류·경고·유실 메시지가 모두 0이어야 통과한다.

- Debug-native-v4, Release-native-v3: 각 3정책 × 6장면 = 18 frames, maxError=0, validationProblems=0, exitCode=0.
- 정책은 DeclarationOrder, ExplicitVersioned+PreserveDeclarationOrder, ExplicitVersioned+DependencyOrder다. 독립 출력/기존 HDR 합성 각각에서 깊이 없음·깊이 표시·깊이 차폐를 검사한다.
- 실제 업로드한 흰색 텍스처를 공유하는 두 배치로 GPU draw를 실행한다. 독립 중심 색상·깊이 차폐·테두리 입력 보존 및 대상 알파 보존을 검사하고 16×16 전체 RGBA 픽셀을 기준 정책과 정확히 비교한다.
- 명시 정책에서 LegacyState 접근 0, 공유 텍스처 읽기 1, 기존 색상 reader→Sprite Modify의 WAR 간선과 출력 자원/버전을 확인한다. 빈 Sprite는 입력 버전을 보존한다.
- 기존 RG1~RG5 및 GPU RenderGraph 테스트를 함께 통과했다. 전체 제품 SceneRenderer의 versioned 수용을 의미하지 않는다.
- 최종 빌드 로그: Build/rg5-sprite-editor-Debug-final-build-v6.log 및 rg5-sprite-editor-Release-final-build-v4.log.

최초 Debug-native-v1/v2는 독립 출력의 알파를 1로 기대한 테스트 단언으로 실패했다. 실제 공통 PSO는 대상 알파를 보존하므로 초기 알파 0을 기대해야 한다. Debug-native-v3와 Release-native-v1/v2는 Sprite 18프레임 이미지 비교를 통과했지만 테스트 클리어 값과 리소스 생성 시 최적 클리어 값 불일치 경고로 전체 gate가 실패했다. 테스트의 색상 클리어 힌트를 맞추고 차폐용 깊이 리소스를 원하는 최적 클리어 값으로 명시 생성하여 최종 실행에서 경고 0을 확인했다. 실패 로그는 보존한다.

## 제품 기준선 — 완료

증거 루트: Build/Obj/RenderRG5Sprite.

- Debug-regression-v1, Release-regression-v1: 기본 제품 경로에서 구성별 독립 프로세스 2회 × 캡처 2회 통과. 실패 0, 정상 종료, 강제 종료 없음.
- Debug-before-after-v1, Release-before-after-v1, Debug-Release-v1: 직전 RG5-10 기준선 대비와 구성 간 각 16개 이미지의 maxError/RMSE/changedPixels/exceededPixels 모두 0. 동일 inputHash/full graphHash.
- current-baseline-phase-complete-v1.json: 현행 소스·Editor executable/runtime SHA-256 일치 및 phaseComplete=true.
- rg5-sprite-completion-v1.json: sliceComplete=true, spriteGpuAcceptance=true, versionedProductGpuAcceptance=false, rg5Complete=false.

이 대표 제품 장면의 기본 경로 회귀와 16×16 Sprite 체인의 세 정책 GPU 검증을 구분한다. 전체 SceneRenderer의 기본 전환은 RG6 소유다.

## 남은 범위

화면 SSS·SSR·Fog·후처리·UI 및 Editor/캡처/fixture의 나머지 소비자, legacy 추론·adapter 제거, 전체 제품 versioned GPU 수용과 RG6 기본 의존성 정렬 전환이 남아 있다. 제품 기본 DeclarationOrder, RG5 progress/기성 0과 잠정 10인일을 유지한다.

현재 정적 호출 위치는 제품 59·게이트 221이다. 기존 tracked cpp/h 범위에 이번 신규 검사 헤더를 포함하여 계산했으며 inl 전체 목록이나 legacy 접근 제거 완료를 뜻하지 않는다. 증거: Build/Obj/RenderRG5Sprite/declaration-inventory.json.

IBL 1024/4096 및 MAT-9의 SSS·투과 품질/성능 조건은 유지한다. Vulkan 비교는 PHASE 4.9 RenderDoc 캡처→리소스 확인→픽셀별 비교에서 진행한다.

현행 기준선은 후속 [RG5-12 화면 SSS/SSR 이관 검증](RenderRg5ScreenMigration.md)으로 갱신했다. 이 문서의 RG5-11 증거는 보존한다.
