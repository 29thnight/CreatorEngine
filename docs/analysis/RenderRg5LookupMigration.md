# RG5-6 Lookup 소유자 버전 핸들 — 완료

SceneLookupFrame::DeclareCaptureOutputs가 범위·graph/epoch/upload/descriptor 소유권을 검사하고 캡처할 입력 맵의 Write 버전을 만들어 반환한다. SceneHost LookupCapture는 FindImportedTexture로 imported v0를 다시 찾는 대신 이 출력 핸들을 소비한다. 입력·decal 자원은 Read, 캡처 목적지는 Write를 선언한다.

Lookup Bake는 graphInputs_의 최신 버전을 읽고 sample/statistic buffer를 Write로 갱신한다. GraphSamples/GraphStatistics 및 Ready는 현재 소유 핸들을 전달한다. imported 물리 자원·풀·제출 후 공개·완료 수명 계약은 유지한다.

실제 SceneLookupCache::Prepare를 호출하는 계획 검사는 두 순서 정책에서 11개 입력 맵의 캡처/Bake/Ready를 두 번 선언한다. 출력 v1/v2·이전 독자 WAR·잘못된 캡처 범위 거부를 확인한다. 입력 캡처는 합성 pass이며 실제 Lookup GPU 셰이딩 실행 수용으로 계산하지 않는다.

Forward Graph guard는 유지한다. 나머지 Refraction/Subsurface/Volume/GraphSurface·Draw 자원 선언·중복 읽기 정리 및 DeclareBlended 갱신 출력 계약이 남아 있다. 이후 실제 혼합 GPU 검증과 guard 제거를 수행한다. RG5 전체와 RG6 기본 전환은 열려 있다.

SceneHost와 Lookup 소유자 파일의 namespace 들여쓰기 및 if/for 스코프를 함께 정리했다. 최종 Debug/Release 빌드는 모두 통과했다. Debug 제품 회귀는 독립 2회 정상 종료로 통과했다. Release-regression-v1은 native 명령 성공 후 전체 프로세스 종료 120초 제한으로 실패했으며 기록을 보존한다. Build/verify-rg5-lookup-retry.ps1은 원본 검증 스크립트의 경로와 소스 해시 대상을 유지하고 해당 종료 제한만 240초로 늘려 Release-regression-v2를 실행한다. 이미지 기준은 변경하지 않는다.

최종 검증: Build/rg5-lookup-Debug-final-build.log 및 Release-final-build.log 빌드 통과. 최종 바이너리의 Debug-regression-v1과 Release-regression-v2에서 native Lookup marker와 RG1~RG4 GPU 검사 통과, 각각 독립 프로세스 2회 × 캡처 2회 정상 종료·실패 0·강제 종료 없음. 변경 전후 Debug/Release 및 구성 간 비교는 각각 16개 이미지의 maxError/RMSE/changedPixels/exceededPixels가 모두 0이다. current-baseline-phase-complete-v1.json은 현행 소스·실행 파일·runtime 해시와 구성 간 identity를 확인하여 phaseComplete=true다.

증거 루트: Build/Obj/RenderRG5Lookup. rg5-lookup-completion-v1.json의 sliceComplete=true, rg5Complete=false, versionedProductGpuAcceptance=false를 유지한다. declaration-inventory.json은 제품 59·게이트 209 호출이다. 대시보드 RG5는 progress/earnedDays 0, 전체 공수 355·기성 88을 유지한다.

다음 구현: Refraction/Subsurface/Volume 소유자 버전 핸들 → GraphSurface 및 Draw 자원 접근 선언·중복 읽기 정리 → DeclareBlended 갱신 색상 반환 → 실제 Code/Graph 혼합 GPU 검증과 guard 제거. 이후 나머지 소비자/ReadWrite·legacy 추론 제거 및 RG6 기본 전환을 검증한다.
