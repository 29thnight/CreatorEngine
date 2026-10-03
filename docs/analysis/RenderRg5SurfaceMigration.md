# RG5-8 GraphSurface·Draw 접근과 반환 출력 — 완료

SceneHost의 Shadow/GBuffer/Color/Blended는 갱신 출력 핸들을 반환하고 EnhancedSceneRenderer 블랙보드와 Forward+ m_output이 이를 이어 받는다. opaque GBuffer/Shadow는 기존 legacy 생산자가 채운 값을 보존하는 Modify/ReadWrite다. Forward GraphDepthCopy는 Write, 이어지는 GraphSurface 색상은 Write·깊이는 복사된 값 보존을 위한 Modify다. 최종 색상 합성은 Modify이며 선언 당시 값을 콜백에 보관한다.

공통 SceneAccess의 AdvanceSceneSurface/AdvanceSceneColor는 실제 SceneHost가 사용하는 버전 계약이다. NormalizeSceneReads는 명시 모드에서 동일 epoch/kind/index/version의 Read만 병합하며 ShaderResource/PixelShaderResource는 넓은 ShaderResource로 합친다. 중복 쓰기나 서로 호환되지 않는 읽기 상태는 거부한다. 기존 DeclarationOrder 진단은 그대로 보존한다.

Lookup/Refraction/SSS 캡처와 모든 SceneHost Draw 소비자는 명시적 Read를 선언한다. MeshSurfaceBatch는 cold 정적 입력 업로드를 Write/v1로, world transform 출력을 Write/v1로, index/SRV/Ready·재사용된 완료 출력은 Read로 선언한다. 정점·index·상수의 물리 소유권과 제출 수명은 유지한다. if/for 스코프 및 namespace 들여쓰기 규칙을 적용했다.

검사 범위: 생산자에서 사용하는 접근/버전 함수와 합성 pass로 GraphSurface 세 반복·두 순서 정책을 구성한다. copied-depth→Modify 계보 v5, Code→Graph→Code 갱신 반환 계보 v9, WAR·중복 읽기 병합·중복 쓰기/상태 거부를 확인한다. 기존 Special 검사에는 실제 MeshSurfaceEvaluator/Batch cold 정적 입력·world 출력 선언을 포함했다. 합성 pass 계획 검사는 실제 versioned SceneHost 셰이딩 GPU 수용을 대신하지 않는다.

제품 기본 DeclarationOrder와 Forward Graph guard는 유지하며 guard 메시지를 현재 남은 mixed-stream versioned GPU acceptance 조건으로 갱신했다. 실제 SceneHost/mixed Code·Graph GPU 검증과 guard 제거, 나머지 소비자/ReadWrite·legacy 추론 제거, RG6 기본 전환은 후속 완료 조건이다. MAT-9·IBL 1024/4096·Vulkan 4.9는 유지한다.

증거는 Build/Obj/RenderRG5Surface에 저장한다. 회귀는 원본 source/hash 대상을 유지하는 240초 native 종료 대기 runner를 사용한다.


최종 검증: Build/rg5-surface-Debug-final-build.log 및 Release-final-build.log 빌드 통과. Build/Obj/RenderRG5Surface/{Debug,Release}-native-v1과 제품 회귀 내 graph-fixtures에서 RG5_SURFACE_OK marker 및 확장된 실제 MeshSurfaceBatch 선언 검사가 통과했다. 이 native 검사는 SceneHost의 공통 접근/버전 함수와 합성 pass 계획을 확인하며 실제 versioned SceneHost Draw/셰이딩 실행은 아직 수용하지 않는다.

동일 증거 루트의 {Debug,Release}-regression-v1: 구성별 독립 프로세스 2회 × 캡처 2회, 정상 종료 코드 0·실패 0·강제 종료 없음. 이전 RG5-7 기준선과 변경 전후 Debug/Release 비교 및 구성 간 비교 각각 16개 이미지의 maxError/RMSE/changedPixels/exceededPixels 모두 0, inputHash/full graphHash 동일. current-baseline-phase-complete-v1.json은 현행 source/executable/runtime SHA-256과 identity 일치를 확인해 phaseComplete=true다.

rg5-surface-completion-v1.json은 sliceComplete=true, rg5Complete=false, versionedProductGpuAcceptance=false, productDefault=DeclarationOrder다. declaration-inventory.json: 제품 59·게이트 217 호출. 전체 4.x 공수 355·기성 88, RG5 progress/earnedDays 0을 유지한다.

다음은 실제 versioned SceneHost와 Code/Graph 혼합 스트림 GPU 검증 및 guard 제거다. 준비·캐시 재사용·transmission/SSS/Volume 조합과 출력 전달을 실제 실행으로 수용해야 한다. 나머지 소비자/ReadWrite 이관·legacy 추론 제거·RG6 기본 전환 조건도 계속 열려 있다.

커밋 통합 검증(2026-10-03): origin/master의 d6fa3fae까지 물리·CSM 변경 6개를 반영하고 SceneHost의 CSM 가시성·배칭·draw count와 RG5 반환 핸들 계약을 함께 보존했다. 통합 후 Debug 빌드(Build/rg5-sync-Debug-build.log) 및 Debug-sync-native-v1 RenderGraph 검사는 통과했다. 위 Debug/Release 이미지 비교와 current-baseline-phase-complete-v1.json은 원격 통합 이전 RG5-8 소스·바이너리에 대한 증거이며, 통합 후의 Release/전체 이미지 회귀 및 현행 해시 기준선 갱신은 이번 커밋·푸시 작업에서 실행하지 않았다.