# RG5-1 GBuffer·Shadow 생산자 선언 — 2026-10-03 검증 완료

RG5의 첫 이관 묶음이다. GBuffer의 diffuse/metalRough/normal/emissive/bitmask/depth와 Shadow.Cascades는 새 transient 자원을 완전히 초기화한다. ExplicitVersioned에서는 CreateTexture 뒤 Write로 버전 0을 만들고 실제 제품 출력 handle과 callback 소비에 전달한다. 두 패스의 비기본 모드는 explicit Write 접근을 선언한다.

DeclarationOrder는 여전히 제품 기본이며 현재 BuildOrder가 explicit 접근을 거부하고 Write도 버전 모드에서만 동작한다. 이 첫 묶음은 기존 기본 경로를 유지하는 모드 분기를 둔다. 이를 RG5 전체 완료로 계산하지 않는다. 선언 모델과 정렬 정책을 분리해 legacy 추론·분기를 정리하는 후속 RG5 작업이 필요하다. RG6의 제품 기본 전환을 앞당기지 않는다.

착수 시 59개 제품 호출, 201개 게이트 호출을 확인했다. 새 검사 호출 1개를 추가한 현재 총합은 제품 59·게이트 202이다. Editor/RenderTests뿐 아니라 Tools/regression의 포인터 호출 18개도 게이트에 포함한다. 이는 정적 호출 위치 수이며 live node 수가 아니다. Build/Obj/RenderRG5Producers/declaration-inventory.json이 파일·행 근거다.

검사는 실제 GBuffer/Shadow Declare를 호출한다. 세 모드 × 두 선언 순서에서 출력 7개·버전 kind/epoch·explicit Write·RAW 7개·producer/consumer 생존을 확인한다. 이 검사는 fake services의 계획 검사이며 versioned 제품 드로우의 GPU 수용을 대신하지 않는다. 제품 기본 DX12 고정 장면 회귀와 변경 전후 대조는 별도로 수행한다.

RG4의 현재 layer-ID fixture와 Debug/Release 기준선을 변경 전 증거로 사용한다. IBL 1024/4096·기존 이미지 상한을 유지한다. MAT-9는 열려 있고, Lattice 재생은 선택 확장, Vulkan 비교는 PHASE 4.9다.
Debug/Release 빌드는 Build/rg5-producers-Debug-build.log와 Build/rg5-producers-Release-build.log로 통과했다. 두 구성의 실제 Declare 계획 검사에서 RG5_PRODUCERS_OK passes=GBuffer/Shadow modes=3 orders=2 outputs=7 RAW=7을 확인했다.

현행 기준선은 Build/Obj/RenderRG5Producers/Debug-regression-v1 및 Release-regression-v1의 result.json이다. 각각 두 프로세스 × 두 캡처, complete=true, 실패 0, 강제 종료 없음, exitCode=0이다. current-baseline-phase-complete-v1.json은 현행 소스·바이너리 해시를 검사해 phaseComplete=true를 판정했다.

RG4 기준선과 Debug/Release 변경 전후 대조, 현행 Debug/Release 구성 간 대조는 각각 16개 이미지 모두 maxError=0·exceededPixels=0이며 입력 및 전체 그래프 해시도 일치했다. 해당 comparison.json은 Debug-before-after-v1, Release-before-after-v1, Debug-Release-v1 아래에 있다.

rg5-producers-completion-v1.json은 sliceComplete=true, rg5Complete=false, versionedProductGpuAcceptance=false다. RG5 전체의 기성은 0으로 유지한다. 다음 묶음은 선언 모델과 정렬 정책 분리 및 consumer/ReadWrite 이관이다. 이후 legacy 추론 제거와 versioned 제품 GPU·전체 프레임 수용을 검증한다. 제품 기본은 DeclarationOrder이며 Vulkan 대조는 PHASE 4.9에서 수행한다.