# Lattice 머테리얼 우선 도입과 남은 편집기 마감

> 기준: `493536e300e274cbf59989cdd62b20310c8ae64f`의 코드·호출 연결 정적 검토. 이 문서 갱신에서는 빌드·GPU 실행·성능 측정을 수행하지 않았다.

## 현재 범위와 의도

Lattice는 renderer-neutral graph/document와 ImGui canvas를 분리하고 Material을 첫 제품 domain으로 연결한다. node 표시 이름과 stable socket ID, 의미 값과 layout, document transaction과 GPU generation을 구분해 예측 가능한 편집·복구를 제공하는 것이 목적이다.

현재 Material Node Editor, MeshRenderer Inspector, authenticated `material.editor` 명령은 실제 LX document/Canvas와 연결되어 있다. Material compiler/typed IR/reroute lowering·기본 group 명령·Slang 생성이 없다는 과거 설명은 현재 상태가 아니다. [공통 노드 계약](../design/LatticeNodeSystem.md), [schema](../design/MaterialGraphSchema.md), [Editor](../design/MaterialNodeEditor.md)가 구현 정본이다.

독립 예제의 성공과 제품 UI 전체 수용은 다르다. 예제 LXG 형식은 `.shadergraph` schema가 아니고, group model/API가 있다고 전용 Editor panel이 완성된 것도 아니다. 이 계획은 남은 제품 마감과 향후 다른 domain을 분리한다.

## 머테리얼 우선 순서

| 단계 | 현재 기반 | 남는 산출물과 gate |
|---|---|---|
| LX-0 | Blender5.1.1 node/socket 대응 기준선 | 새 지원 범위는 대응표에 명시. 전체 Blender 지원 완료로 확대하지 않음 |
| LX-1 | registry·typed pin/ID·document 명령·Frame·group/nesting·Material IR/compiler | 자유 annotation schema/왕복, wire reroute 삽입의 단일 transaction/Undo·진단 |
| LX-2 | 독립 LatticeExample Core/Canvas host와 과거 조작·저장 게이트 | 현재 변경 시 standalone 회귀. 과거 실행을 새 실행으로 표시하지 않음 |
| LX-3 | Editor 창·Inspector 진입·typed widget·Apply/Save/preview | 그룹 내부/interface·Blackboard 전용 제품 panel, 전체 gesture/DPI/reopen 마감 |
| LX-3H | 인증 HTTP·document ID/revision·조회/편집/Undo/Save/Apply | health/idle·frame 진행, Scene/document 전환·shutdown·충돌 무변경 수명 gate |
| LX-4/5/6 | 향후 별도 domain 작업 | Animator/BT 새 저작과 모든 소비자 이관 뒤 구 UI 의존 제거 |

현재 공수는 미산정이다. MAT 계획의 의미/IR/compiler와 LX의 UI/문서 작업을 같은 산출물로 중복 산정하지 않는다.

## LX-1: 편집 transaction을 완성하기

annotation은 단순 화면 draw를 추가하는 작업으로 끝나지 않는다. stable identity·저장 schema·migration·복사/붙여넣기·삭제·Undo/Redo·unknown payload 보존 범위를 정해야 한다. 기존 Frame 기능을 자유 annotation 전체 지원으로 설명하지 않는다.

wire reroute 삽입은 기존 link를 끊고 typed reroute와 두 link를 만드는 전체 작업이 한 Undo여야 한다. 방향/타입/중첩 scope·ID 충돌을 먼저 검증하고 실패 시 원래 연결을 그대로 둔다. reroute node 생성/lowering이 이미 있다는 사실과 이 전용 gesture의 완료는 구분한다.

## LX-3: 제품 panel과 조작 gate

공유 group definition과 instance의 interface 변경은 안정 pin ID·외부 link를 보존하고 모든 instance에 일관되게 전파해야 한다. 연결된 socket 제거, reference cycle·잘못된 nesting과 duplicate identity는 무변경 거부한다. 전용 내부 편집/breadcrumb/interface 기본값·순서 panel을 실제 Editor에서 확인해야 한다.

Blackboard는 ID·타입·default·노출·color-space 의도를 편집하는 단일 원본이다. canvas node와 별도 값을 만들어 저장하지 않는다. Material 전체 asset transaction은 graph-only document command보다 넓은 범위라는 점을 유지한다.

제품 gate는 검색/추가·연결/거부·연결 대체·drag 다중 선택·pan/zoom·접힘·typed widget·copy/paste·Frame/group·Undo/Redo·저장/재열기·DPI/resize·다량 node·오류 위치 이동을 포함한다. gesture preview는 취소/commit 경계와 단일 Undo를 지키고 stale gesture가 다른 document로 넘어가지 않아야 한다.

Apply/Save/Assign과 settled auto-save, strict primary reload, draft async compile과 preview의 document/revision 검사를 함께 검증한다. preview가 지원하지 않는 Blended나 준비되지 않은 pinned draft를 다른 material로 숨기지 않는다.

## LX-3H: 살아 있는 Editor의 명령 계약

HTTP는 별도 commandlet의 가짜 문서가 아니라 실행 중인 Editor의 열린 document를 조작한다. command service가 요청을 host queue로 전달하고 UI와 같은 document operation을 실행한다. 수정 요청은 document ID/revision과 stable node/pin/link ID를 확인하며 성공 시 새 revision을 반환한다.

health/commands·조회부터 connect/disconnect/property/value/delete/collapse/undo/redo/save/apply/reload/assign을 실제 제품 상태에서 확인한다. 인증 실패·stale revision·닫힌 문서·Scene 교체·shutdown은 무변경 오류여야 한다. async compile 완료가 새 문서를 덮거나 종료 중 owner를 참조하지 않아야 한다.

저장 명령은 명시적으로 호출할 수 있고 UI에는 자동 저장 정책도 존재한다. 과거의 “Save는 명시적 명령뿐”이라는 설명으로 현재 자동 저장을 누락하지 않는다. HTTP successful receipt와 GPU frame completion은 별개다.

## 이후 창과 의존성 제거

Material만 우선 진행한다. LX-4 Animator는 새 controller/state/transition의 저작·저장·실행, LX-5 BT는 새 child order/decorator/weight 의미를 검증한다. 기존 AnimatorController/위치 JSON·`.bt` 자산 변환기나 기존 자산 migration은 이 계획의 요구가 아니다.

그때까지 기존 Animator/BT 창과 imgui-node-editor 소비자는 유지한다. LX-6는 모든 활성 UI 소비자가 LX로 이관되고 새 창의 실제 저장·실행·Debug/Release 회귀가 확보된 뒤 NodeEditor/BlueprintBuilder/PinHelper와 include/link/vcpkg 의존을 제거한다. Material 창 하나의 성공을 삭제 허가로 사용하지 않는다.

Pipeline/pass graph domain은 향후 별도 계약이다. Material closure/Output·shader compiler의 지원 범위를 일반 그래프 UI의 지원 범위로 혼동하지 않는다.

## 검증과 인계

`Tools/LatticeExample`은 Engine/Editor와 별도 host에서 Core/Canvas를 검사하는 출발점이다. 제품 gate는 Editor의 실제 document·asset generation·runtime을 관통해야 하며 독립 예제 결과로 대신하지 않는다. Vulkan 제품 비교는 backend parity 범위로 유지한다.

이번 문서 갱신에서는 예제·Editor를 빌드/실행하지 않았다. 과거 [Node Editor 성능](../analysis/MAT9NodeEditorPerformance.md)과 [Material Symbols](../analysis/EditorMaterialSymbolsW1Validation.md) 결과는 당시 증거다. 의미/품질/renderer gate는 [Material 계획](BlenderMaterialGraphPlan.md), UI/수명 마감은 이 계획이 소유한다.

## 구현 근거

- [`Lattice/Core/LXGraph.h`](../../Lattice/Core/LXGraph.h)
- [`Lattice/Core/LXDocument.h`](../../Lattice/Core/LXDocument.h)
- [`Lattice/Core/LXDocument.cpp`](../../Lattice/Core/LXDocument.cpp)
- [`Lattice/ImGui/LXCanvas.cpp`](../../Lattice/ImGui/LXCanvas.cpp)
- [`Lattice/Material/LXMaterialGraph.cpp`](../../Lattice/Material/LXMaterialGraph.cpp)
- [`Lattice/Material/LXMaterialCompiler.cpp`](../../Lattice/Material/LXMaterialCompiler.cpp)
- [`Editor/EngineGUIWindow/MaterialGraphWindow.cpp`](../../Editor/EngineGUIWindow/MaterialGraphWindow.cpp)
- [`Editor/EngineEntry/Commands/MaterialGraphCommands.cpp`](../../Editor/EngineEntry/Commands/MaterialGraphCommands.cpp)
- [`Tools/LatticeExample/main.cpp`](../../Tools/LatticeExample/main.cpp)
- [`Editor/EngineGUIWindow/AnimatorEditorWindows.cpp`](../../Editor/EngineGUIWindow/AnimatorEditorWindows.cpp)
