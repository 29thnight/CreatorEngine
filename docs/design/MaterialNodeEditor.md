# Material Node Editor 사용과 host 계약

> 기준: `493536e300e274cbf59989cdd62b20310c8ae64f`의 코드·호출 연결 정적 검토. 이 문서 갱신에서는 빌드·GPU 실행·성능 측정을 수행하지 않았다.

## 진입과 화면

`editor::material_editing::Open`, `Draw`, `TickAutomaticChanges`, `DrawInspectorPreview`, `CapturePreviewRequest`가 Editor host API다. MeshRenderer 진입 외에 `material.editor new`, `asset <guid>`로 자산 중심 작업을 연다. 표시 제목/아이콘/section은 `MaterialGraphPresentation`이 구성하고 일반 canvas는 `Lattice/ImGui`가 그린다.

canvas는 stable node/pin ID와 typed widget, 접힘, 선택·이동, pan/zoom, 연결, 검색과 clipboard 등을 지원한다. `LXDocument` 명령 경계가 revision/Undo를 관리한다. style과 graph layout은 material 의미와 별개이며 `CanvasUiScale`은 사용자 배율과 monitor DPI에 맞춰 node/port/widget 측정을 일치시킨다.

## draft와 preview

session은 asset, LXDocument, material/graph identity, disk snapshot, settings revision, accepted/prepared material, preview worker를 보유한다. 변경된 graph snapshot을 약 0.25초 debounce 후 compile하며 결과의 document/revision을 확인한다. 잘못된 shader는 accepted material을 교체하지 않고 오류를 표시한다.

preview는 별도 live view key/history revision과 material source를 사용한다. `CapturePreviewRequest`는 현재 pinned/최근 visible draft를 우선하고 아니면 Inspector preview를 사용한다. 현재 Blended coverage preview는 선택하지 않는다. pinned draft가 표시 불가여도 Inspector의 다른 재질로 몰래 대체하지 않는다.

## 저장·적용·배치

Apply는 현재 준비된 revision을 자산과 사용자에게 게시한다. Save는 accepted material+graph를 디스크에 기록한다. Assign은 saved reference를 MeshRenderer에 배치하며 Scene 저장이 필요하다. 외부 disk 변경, accepted owner 변경, 이름/identity 충돌, 미완료 gesture는 거부한다.

자동 경로는 편집이 안정되고 compile이 준비된 뒤 Apply→Save를 시도한다. `Reload Saved Draft`는 dirty 확인 후 primary material/graph를 엄격히 읽어 draft만 바꾼다. 자세한 원자성/충돌 규칙은 [자산 저작](MaterialAssetAuthoring.md)을 따른다.

## 열린 문서 명령

`material.editor state`로 document ID와 revision, nodes/pins/links, dirty/applied/editable, preview pending/error/ready를 읽는다. 수정 명령은 `<operation> <document-id> <revision> ...`를 요구하며 다른 문서나 stale revision은 무변경 거부한다.

현재 handler는 save/apply/reload/undo/redo, add, assign, connect/disconnect/delete, collapse, property, typed value를 처리한다. 값은 bool의 true/false, exact int, 유한 scalar/vector3/color4를 요구한다. `sessions`는 visible/retained dirty draft 상태를, `preview on|off|refresh`는 preview 요청을 관리한다. 열린 문서에 새 source 파일을 직접 쓰는 우회는 UI 상태와 revision을 깨뜨린다.

`material.graph <object>`는 instance 조회와 inline bind/set/reload용 별도 명령이다. asset reference를 가진 MeshRenderer에는 inline editing을 거부한다. 이 명령을 자산 draft 편집의 대체 API로 사용하지 않는다. HTTP transport/auth/health는 CommandService 경계이며 handler 성공과 실제 화면 render completion을 구별한다.

## 성능·미완성·회귀

화면 측정은 revision별 analysis/cache와 실제 ImGui frame을 확인해야 한다. CPU node-window 시간, shader compile, Scene GPU, display promotion은 다른 지표다. 현재 소스만으로 과거 MAT-9 timing을 재현했다고 주장하지 않는다.

그룹 정의/중첩/typed Blackboard는 Core/Material 모델에 있지만 제품 전용 그룹/interface·Blackboard 패널의 전체 조작 마감은 별도 요구다. annotation schema, wire reroute insertion 단일 Undo, DPI/reopen/scene transition/HTTP shutdown 충돌 gate도 [LX 계획](../plans/LatticeAdoptionPlan.md)에 유지한다.

## 구현 근거

- [`Editor/EngineGUIWindow/MaterialGraphWindow.h`](../../Editor/EngineGUIWindow/MaterialGraphWindow.h)
- [`Editor/EngineGUIWindow/MaterialGraphWindow.cpp`](../../Editor/EngineGUIWindow/MaterialGraphWindow.cpp)
- [`Editor/EngineGUIWindow/MaterialGraphPresentation.cpp`](../../Editor/EngineGUIWindow/MaterialGraphPresentation.cpp)
- [`Editor/EngineEntry/Commands/MaterialGraphCommands.cpp`](../../Editor/EngineEntry/Commands/MaterialGraphCommands.cpp)
- [`Lattice/Core/LXDocument.h`](../../Lattice/Core/LXDocument.h)
- [`Lattice/ImGui/LXCanvas.h`](../../Lattice/ImGui/LXCanvas.h)
