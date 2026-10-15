# MAT-9 Node Editor·Inspector 성능 기록의 현재 사용법

> 기준: `493536e300e274cbf59989cdd62b20310c8ae64f`의 코드·호출 연결 정적 검토. 이 문서 갱신에서는 빌드·GPU 실행·성능 측정을 수행하지 않았다.

## 보존된 측정

[MAT9NodeEditorPerformance.json](MAT9NodeEditorPerformance.json)은 2026-09-30, reflgen merge 전, Scene 1302×614의 실제 창·HTTP·profile 결과다. Debug 수정 전 약 29 FPS와 수정 후 약 42~43 FPS, Release의 더 높은 완료율은 그 입력과 binary의 역사적 값이다. 현재 revision의 성능 보장이나 다른 viewport 결과로 연결하지 않는다.

기록의 FPS는 구간 전체의 completed Scene frame 수/시간이다. CPU 통계는 수집된 유효 span을 쓰므로 Release 보관 제한으로 생긴 표본 공백과 분모가 다를 수 있다. 원시 `.ceprof`가 없으면 summary의 provenance 범위를 넘어서 새 percentile을 계산할 수 없다.

## 현재 UI 연결

Lattice canvas는 node item row의 cache scope를 사용한다. material window의 draft compile/preview는 document revision과 async 작업을 묶고, Inspector preview는 별도 요청·source를 갖는다. `ImGuiDrawHelperMeshRenderer`에서 Inspector preview를 소비하므로 Node Editor 창의 수명과 동일하다고 가정하지 않는다.

현재 pinned draft가 준비되지 않았을 때 unrelated Inspector material을 대신 보여 주지 않는다. Blended preview는 지원 범위를 따로 제한한다. 편집된 draft, accepted runtime, 저장된 asset, 완료된 preview는 다른 상태이며 UI가 이를 구분해야 한다. [Node Editor 계약](../design/MaterialNodeEditor.md)이 정본이다.

## 과거 최적화에서 남길 근거

pin/열 너비 등 반복 canvas 계산과 Scene lock wait를 줄이는 것은 중요하지만 긴 Presentation CPU scope를 DXGI Present 비용이라고 단정하면 안 된다. 과거 캡처는 실제 Present보다 canvas/lock이 지배적임을 보여 주었다. GPU lookup spike는 별도 [근접 카메라 조사](MAT9NearCameraPerformance.md)의 문제다.

preview가 접혔을 때 불필요한 요청을 줄이고 변경/Refresh의 완료 결과를 재사용해야 한다. 그렇더라도 stale document/source를 새 선택의 preview로 표시하지 않는다. 성능 cache는 identity·revision 검사를 생략할 이유가 아니다.

## 재검증 범위

현재 source/binary, 실제 Scene 크기/UI scale, Node Editor 열림/닫힘, Inspector preview 접힘/펼침, recording 유무를 고정해 비교한다. CPU canvas/Present/lock과 GPU prepare/render, 완료 FPS를 각각 기록한다. Debug/Release와 cold/warm compile도 분리한다.

역사적 Debug 1341/Release 1339 checks와 capture 각 5회는 당시 UI 회귀다. full gesture/DPI·문서 전환·reopen·shutdown·HTTP stale revision·auto-save 상호작용을 현재 코드에서 다시 통과했다고 선언하지 않는다. 원래 ImGui scale 0.8의 기원 분석은 이 성능 기록으로 해결하지 않는다.

## 구현 근거

- [`Lattice/ImGui/LXCanvas.cpp`](../../Lattice/ImGui/LXCanvas.cpp)
- [`Editor/EngineGUIWindow/MaterialGraphWindow.cpp`](../../Editor/EngineGUIWindow/MaterialGraphWindow.cpp)
- [`Editor/EngineGUIWindow/ImGuiDrawHelperMeshRenderer.cpp`](../../Editor/EngineGUIWindow/ImGuiDrawHelperMeshRenderer.cpp)
- [`docs/analysis/MAT9NodeEditorPerformance.json`](MAT9NodeEditorPerformance.json)
