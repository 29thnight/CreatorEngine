# RG5-5 Forward+ 타일 버퍼·Code 색상 스트림 — 2026-10-03 검증 완료

Forward+.Cull의 imported tile count/list에 Write/v1을, depth 및 Shade 소비자에 Read를 연결했다. 자체 색상이면 Clear가 Write/v0을 초기화하고 Code 배치마다 Modify/ReadWrite로 다음 색상 버전을 만든다. 기존 lighting이면 그 버전에서 바로 Modify를 시작한다. 각 Clear/Shade callback은 해당 패스의 출력 handle을 값으로 캡처해 최종 멤버 handle을 공유하지 않는다.

선언 검사는 실제 Forward Initialize/PrepareFrame 후 합성된 세 Code draw 순서를 사용한다. Compile 계획만 검사하며 합성 draw를 GPU에서 실행하지 않는다. 두 정책 × 자체/입력 색상 경로에서 tile 두 개 Write/v1, 색상 세 배치 ReadWrite/v1→v2→v3와 출력 v3을 확인한다. 실제 제품 장면 회귀는 별도 수행한다.

혼합 Graph 경로는 미완료다. SceneHost::DeclareBlended는 현재 갱신 색상을 반환하지 않으며 LookupCapture/Bake/Ready·Refraction·Subsurface·Volume과 forward surface/depth 복사도 이관해야 한다. 명시 모드에서 준비된 Graph/Volume frame이 있으면 구체적인 RG5 오류로 거부한다. 제품 기본 DeclarationOrder의 기존 Code/Graph 혼합 경로는 유지한다. 이 guard는 RG5 Graph 이관에서 제거할 항목이며 RG6 adapter/guard 0 수용 조건에 포함한다.

RG5 전체 완료 및 버전 기반 혼합 제품 GPU 수용으로 계산하지 않는다. 다음 묶음은 Graph 자원 소유자의 버전 handle 전달과 DeclareBlended 출력 반환 계약이다. 후처리·Pipeline compiler·legacy 제거와 RG6 전환도 남아 있다.

Debug native-v1은 검사에서 프레임 기록을 열지 않아 Forward 업로드가 거부됐다. 검사에 BeginFrame/AbortFrame 수명을 추가한 뒤 Debug build-v2 및 Debug/Release native-v2에서 통과했다. 실패 기록은 보존한다. 기존 RG1~4 GPU fixture도 두 구성 픽셀 오차 0이다. 제품 전체 장면 기준선도 갱신 완료했다. 정적 호출 위치는 제품 59·게이트 208(총 267)이며 declaration-inventory.json에 저장했다.

현행 기준선은 Build/Obj/RenderRG5ForwardCode/{Debug,Release}-regression-v1/result.json이다. 각각 두 프로세스 × 두 캡처 모두 통과했고 실패 0·exitCode=0이다. RG5-4 대비 변경 전후와 현행 Debug/Release 구성 간 비교는 각각 16개 이미지 maxError=0·exceededPixels=0이며 입력·전체 그래프 해시가 일치한다. current-baseline-phase-complete-v1.json은 현행 소스·바이너리 해시 확인 후 phaseComplete=true다.

rg5-forward-code-completion-v1.json은 sliceComplete=true, rg5Complete=false, versionedProductGpuAcceptance=false다. Code 경로의 버전 선언 계획 검사와 제품 기본 DeclarationOrder 화면 회귀를 구분한다. 다음은 SceneHost/Lookup/Refraction/Subsurface/Volume의 소유 핸들 전달, DeclareBlended의 갱신 색상 반환 및 실제 혼합 Code/Graph GPU 검증이다. 해당 수용 후 이번 guard를 제거한다. RG5 전체 기성은 0을 유지한다.