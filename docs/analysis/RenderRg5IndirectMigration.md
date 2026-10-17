# RG5-4 SSAO·SSGI·히스토리 선언 이관 — 2026-10-03 검증 완료

SSAO.Compute/Filter와 SSGI HiZ/Trace/Resolve/Filter/Composite/StoreHistory에 명시적 접근을 연결했다. 입력 SRV/CopySource는 Read, 완전히 초기화하는 UAV/CopyDest는 Write다. 생성한 transient texture는 버전 0 writer handle을 제품 출력과 callback에 전달한다.

SSGI 현재 저장 대상 imported history color/depth는 Write로 버전 1을 만든다. Resolve가 읽는 이전 history 버전 0 핸들은 해당 단계의 srvHandles에 복사해 유지한다. 히스토리 물리 리소스·핑퐁·상태 writeback은 그대로 사용한다.

실제 Initialize/PrepareFrame/Declare를 호출하는 native 계획 검사는 두 순서 정책 × 선택 입력 유무 × 연속 두 프레임이다. legacy 접근 부재와 StoreHistory color/depth 두 목적지의 Write/v1을 검사한다. 이는 실제 버전 기반 전체 제품 GPU 실행 완료를 대신하지 않는다.

Forward+는 Code·Graph 혼합 스트림과 재질 Graph의 DeclareBlended/DeclareVolume을 함께 이관해야 하므로 별도 다음 묶음이다. 후처리·히스토리 전체·Pipeline compiler와 legacy 추론 제거, RG6 제품 기본 전환도 남아 있다. 제품 기본 DeclarationOrder, IBL 1024/4096, MAT-9 열린 상태, Vulkan 비교 PHASE 4.9를 유지한다.

Debug 빌드는 검사의 진단 필드 참조 오류를 수정한 Build/rg5-indirect-Debug-build-v2.log에서 통과했다. Release는 Build/rg5-indirect-Release-build.log에서 통과했다. 두 구성의 {Debug,Release}-native-v1/result.json은 passed=true/exitCode=0이며 RG5_INDIRECT_OK SSAO/SSGI policies=2 optional=2 frames=2 history-v1과 기존 RG1~4 GPU 픽셀 오차 0을 확인했다. 제품 전체 장면 기준선도 갱신 완료했다. 정적 호출 위치는 제품 59·게이트 207(총 266)이며 declaration-inventory.json에 파일/행 근거를 저장했다.

현행 기준선은 Build/Obj/RenderRG5Indirect/{Debug,Release}-regression-v1/result.json이다. 각 구성의 두 프로세스 × 두 캡처 모두 통과했고 실패 0·exitCode=0이다. RG5-3 기준선 대비 변경 전후와 현행 Debug/Release 구성 간 대조는 각각 16개 이미지 maxError=0·exceededPixels=0이다. 입력·전체 그래프 해시도 일치한다. current-baseline-phase-complete-v1.json은 현행 소스·바이너리 해시 확인 후 phaseComplete=true다.

rg5-indirect-completion-v1.json은 sliceComplete=true, rg5Complete=false, versionedProductGpuAcceptance=false를 기록한다. 제품 기본 DeclarationOrder 화면 회귀와 버전 선언 계획 검증을 구분한다. RG5 전체 기성은 0을 유지한다. 다음 구현은 Forward+ 타일 버퍼 Write/Read 및 Code/Graph 혼합 색상 스트림의 Modify 계보다.