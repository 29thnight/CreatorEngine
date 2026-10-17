# Content Browser의 탐색·배치·캐시 계약

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 등록 경로를 2026-10-15에 대조했다. 이 개정에서는 빌드·실행·화면 검증을 하지 않았다. 과거 결과와 현재 구현의 존재를 구분한다.

## 현재 사용 표면

Content Browser는 도킹 패널 안에 디렉터리 트리와 자산 목록을 가진다. 과거 Tile/Tree가 창 종류 자체를 나누던 선호 옵션은 현재 EditorPreferences에 없다. 현재 폴더, 전체 자산, recent 범위를 구별하고 범위에 따라 검색 힌트가 달라진다. Back/Forward visit은 위치·검색·스크롤 상태를 함께 복원한다.

toolbar는 New, 경로 breadcrumb와 경로 탐색, 검색, 필터 및 view 옵션을 배치하며 좁은 폭에서는 두 행으로 나눈다. list view와 64~160 tile size는 표시 방식이다. 디렉터리 선호 폭은 logical px로 유지하고 사용 가능한 현재 폭에만 clamp한다. splitter의 넓은 입력 영역과 얇은 표시 선은 서로 다른 치수다. drag, 키보드 조절, reset 뒤 선호 폭을 workspace가 저장한다.

## 파일 작업의 경계

폴더 만들기와 Scene Render Profile 생성은 EditorAssetDatabase의 authoring 경로를 사용한다. Assets 밖, 잘못된 이름, 중복 경로를 거부하며 profile 본문만 생기고 meta 생성에 실패한 경우 정리한다. 목록 선택·열기·drag payload는 파일 분류와 같은 presentation을 사용해야 한다. 새 아이콘만 추가하고 filter classification을 바꾸지 않으면 결과와 그림이 불일치할 수 있다.

## 비용과 freshness

BrowserDirectorySnapshot은 entries와 revision, path 문자열, 파일 종류, 오류를 보관한다. 기본 재검증 간격은 최소 1000ms이며 실제 스캔 비용×100과 비교해 늘릴 수 있다. 일반 stale rescan 예산은 프레임당 1개지만 최초 miss 및 명시 invalidation은 별도이므로 “어떤 상황에도 1회” 보장은 아니다. 전체 검색은 peek로 cache 존재를 확인하고 별도 예산으로 목록을 준비한다.

정렬/필터 결과는 listing generation과 사용자 선택에 따라 갱신한다. thumbnail은 독립적인 비동기 cache이며 기본 budget은 48MiB다. UI path가 모든 파일을 동기 decode하는 것으로 설명하지 않는다. scans와 비스캔 probes를 따로 측정해야 cache hit 뒤의 filesystem 호출을 놓치지 않는다.

## 호환성과 수용

예전 “시작 시 Scene 탭 선택” 기록은 별도 Scene/Game 창 시절의 내용이다. 현재는 ViewportHost 모드와 workspace 복원이 그 역할을 맡는다. [viewport 설명](EditorSceneCropValidation.md)을 따른다. 변경 후에는 Back/Forward, 범위 전환, 새 폴더 실패, 좁은 폭, stale listing, 큰 검색 결과 clipping과 thumbnail 실패를 각각 확인한다. 이 문서 개정은 과거 Artifacts 화면이나 현재 Windows UI를 재검증하지 않았다.

## 구현 근거

- [Editor/EngineGUIWindow/ContentsBrowserWindow.cpp](../../Editor/EngineGUIWindow/ContentsBrowserWindow.cpp)
- [Editor/EngineGUIWindow/BrowserDirectorySnapshot.cpp](../../Editor/EngineGUIWindow/BrowserDirectorySnapshot.cpp)
- [Editor/EngineGUIWindow/BrowserThumbnailCache.cpp](../../Editor/EngineGUIWindow/BrowserThumbnailCache.cpp)
- [Editor/EngineEntry/EditorAssetDatabase.cpp](../../Editor/EngineEntry/EditorAssetDatabase.cpp)
- [Editor/EngineEntry/EditorPreferences.h](../../Editor/EngineEntry/EditorPreferences.h)
- [Editor/EngineGUIWindow/EditorWorkspaceStore.cpp](../../Editor/EngineGUIWindow/EditorWorkspaceStore.cpp)
