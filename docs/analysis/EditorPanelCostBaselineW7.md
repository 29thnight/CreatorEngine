# W7 패널 비용: 현재 계측과 기준선 비교 규칙

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 등록 경로를 2026-10-15에 대조했다. 이 개정에서는 빌드·실행·화면 검증을 하지 않았다. 과거 결과와 현재 구현의 존재를 구분한다.

## 무엇을 재는가

`EditorPanelCost`는 Presentation 스레드에서 steady clock으로 scope 경과 시간을 모으고 frame 경계에서 잠금 아래 게시한다. 게임 스레드의 `editor.panelcost`는 게시된 값만 읽는다. Hierarchy, browser_tree, browser_files 세 slot은 일부 본문의 비용이며 별도의 stable-ID 창 장부 및 셸 구간과 중첩될 수 있다. slot 합계에 창 시간을 다시 더하면 같은 작업을 이중 계산한다.

각 slot/창은 최근 최대 512개 표본의 평균과 p95를 계산한다. p95는 정렬 기준 `min(n-1,floor(n*0.95))` 위치다. last는 마지막 표본, max는 reset 이후 최대여서 링에서 오래된 spike가 빠져도 max는 남을 수 있다. frames와 samples도 같은 값이 아니다. units/scans/probes는 작업량을 설명하는 정수이며 allocation 바이트나 GPU 시간으로 변환할 수 없다.

셸 장부는 beforeframe, host.beginframe, dockspace, prewindows, postwindows, endrender, present를 구별한다. host.beginframe에는 실제 resize/RHI 프레임 준비와 대기가 섞일 수 있다. present는 host EndFrame의 ImGui render 및 RenderAndPresent를 포함하며 기존 `ui_cpu_ms` 범위 밖이다. 전체 UI 프레임 값과 각 scope의 차이는 측정 경계를 먼저 확인한 뒤 해석한다.

## 재현 도구의 계약

`measure-panel-cost.ps1`은 자동 통과 게이트가 아니라 숫자를 남기는 측정 도구다. 기본 실행 파일은 Release, count는 문자열 `1000,10000,50000`, fanout은 0, 요청 창은 2400×1400이다. 현재 목표 표본은 240개이며 최대 poll 수를 둔다. 옛 보고서의 “매 회 512 프레임”을 현재 runner 동작으로 옮기지 않는다. 게임 스레드 `wait N`으로 Presentation 표본 수를 추정하지 않고 결과의 samples를 본다.

fixture는 `scene.populate` 이후 hierarchycheck로 일관성을 확인한다. `-WithAssets`는 실제 프로젝트 Assets 아래 임시 PNG들을 만들고 watcher/meta와 browser를 사용하므로 단순 읽기 전용 측정이 아니다. 종료 시 fixture와 recents 복원을 확인해야 한다. 실행 중 Editor가 있으면 endpoint 충돌을 피하려고 거부한다. 이번 문서 작업에서는 이 도구를 실행하지 않았다.

비교 조건에는 commit/build, 하드웨어·전원, 실제 client/DPI/user scale, workspace/preset, 열린 창과 도킹 폭, 선택/펼침/검색 상태, asset 수와 cache warmup을 기록한다. 요청 창 크기만 기록하면 OS clamp와 browser tree 숨김 때문에 모수가 달라질 수 있다. 시작·검색 재구축 spike와 안정 대기를 따로 측정한다.

## 현재 최적화와 해석 한계

Hierarchy는 평탄 캐시와 보이는 행 clipper를 사용하지만 검색 활성 시 rename을 반영하려고 매 프레임 재구축한다. Content Browser는 디렉터리 결과 cache, 재스캔 budget, 보이는 타일 요청을 사용한다. cache miss와 명시 무효화가 일어난 프레임은 정상 대기보다 비쌀 수 있다. 썸네일의 48MiB 보유 예산도 전체 GPU/프로세스 메모리 상한이 아니다.

과거 1k/10k/50k 수치와 “O(보이는 행)” 해석은 당시 입력·배치에 대한 관측이었다. 현재 복잡도 경계의 근거로 알고리즘을 설명할 수 있지만, 최신 실행 없이 p95 개선율·메모리 절감률을 새로 선언하지 않는다. 특히 평균·p95만으로 cache 재구축 최악값을 숨기지 않는다.

## 남은 수용 요구

[W0 역사 기준선](EditorWorkspaceW0Baseline.md)과 [브라우저 계약](EditorContentBrowserLayoutValidation.md)을 함께 사용한다. Release cold/warm, 깊은 계층, 검색 반복, 파일 변경 burst, 썸네일 늦은 완료 및 여러 작업공간에서 표본을 다시 수집해야 한다. CPU 개선뿐 아니라 allocation/retained memory와 무효화 정확성을 같이 판단하고, 복잡성만 늘리는 cache는 제거하는 원래 요구를 유지한다. 숫자가 없는 항목은 미측정으로 남긴다.

## 구현 근거

- [Editor/EngineGUIWindow/EditorPanelCost.h](../../Editor/EngineGUIWindow/EditorPanelCost.h)
- [Editor/EngineGUIWindow/EditorPanelCost.cpp](../../Editor/EngineGUIWindow/EditorPanelCost.cpp)
- [Editor/EditorWindow/EditorWindowHost.cpp](../../Editor/EditorWindow/EditorWindowHost.cpp)
- [Editor/EngineGUIWindow/EditorRenderer.cpp](../../Editor/EngineGUIWindow/EditorRenderer.cpp)
- [Editor/EngineGUIWindow/HierarchyFlatten.cpp](../../Editor/EngineGUIWindow/HierarchyFlatten.cpp)
- [Editor/EngineGUIWindow/ContentsBrowserWindow.cpp](../../Editor/EngineGUIWindow/ContentsBrowserWindow.cpp)
- [Tools/regression/measure-panel-cost.ps1](../../Tools/regression/measure-panel-cost.ps1)
