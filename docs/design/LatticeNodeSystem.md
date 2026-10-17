# Lattice 공통 노드 모델, 문서, Canvas

> 기준: `493536e300e274cbf59989cdd62b20310c8ae64f`의 코드·호출 연결 정적 검토. 이 문서 갱신에서는 빌드·GPU 실행·성능 측정을 수행하지 않았다.

## 계층과 책임

Lattice(LX)는 node authoring을 renderer와 UI에서 분리하는 계층이다. Core의 `LXGraph`/definition registry는 stable ID, typed pin/link, group/interface, layout을 소유한다. `LXDocument`는 명령·revision·Undo/Redo·dirty/preview transaction을, ImGui Canvas는 조작과 표현을 담당한다. Material은 별도 domain adapter와 typed IR/compiler를 추가한다.

graph 모델에 ImGui 타입이나 RHI handle을 넣지 않는다. `LXMaterialAsset`의 Blackboard, 소켓 색 공간과 schema metadata까지 포함하는 Material transaction은 일반 graph-only edit와 동일하지 않다. Editor의 Snapshot/host adapter가 그 경계를 연결한다.

## 구조와 조작

pin의 표시 이름과 identifier/ID는 구분된다. 그룹은 공유 정의와 instance를 가지며 내부 graph/interface와 외부 연결을 보존한다. `LXDocument`에는 create/update group, boundary pin, collapse selection, copy/paste, replace-input connection, node/frame/view 편집 명령이 있다. 명령 존재는 모든 제품 창에 전용 UI가 있다는 뜻은 아니다.

CanvasState는 선택·drag·pending connection의 document/revision, clipboard, typed widget 편집 상태를 보관한다. 여러 frame에 걸친 gesture는 preview/commit/취소 경계를 거쳐 한 Undo 단위가 되어야 한다. stale pending connection을 새 document에 적용하면 안 된다.

`MeasureNode`, `PinPosition`, item registry가 같은 row geometry를 공유한다. header-only 노드와 접힌 노드도 실제 pin 위치로 링크를 그린다. style/layout 저장과 graph 의미를 분리하므로 배율·위치 변경이 shader 재컴파일을 요구하지 않게 한다.

## 현재 제품 소비

Material Node Editor와 MeshRenderer Inspector가 LX 문서·canvas를 사용한다. material.editor 명령은 같은 열린 document ID/revision을 확인한다. compile은 immutable draft snapshot에서 수행하고 renderer는 verified Generation/Instance를 받는다. 자세한 제품 흐름은 [Material 편집기](MaterialNodeEditor.md)를 따른다.

독립 `Tools/LatticeExample`은 Core/Canvas 조작을 작게 검증하는 host다. 그 LXG 형식은 제품 `.shadergraph` schema와 다르다. 예제의 group 편집 UI나 저장 성공을 Editor/Player의 전체 기능 수용으로 확대하지 않는다.

## 설계 의도와 남은 요구

목표는 Blender형 소켓·기본값·그룹·조작에 예측 가능한 대응을 제공하는 것이다. Blender 모든 node/enum을 단번에 지원한다는 뜻은 아니다. 미지원 입력을 보존할 수 있는 문서와 컴파일 거부 진단을 함께 둬 의미 손실을 숨기지 않는다.

자유 annotation의 schema/왕복, wire reroute 삽입 transaction, 제품 그룹/interface·Blackboard 패널, 전체 gesture/DPI/reopen/host shutdown gate는 별도 마감 대상이다. Material compiler와 reroute lowering, 기본 Editor/HTTP 연결은 이미 있으므로 새로 만드는 작업으로 중복 산정하지 않는다.

Animator/Behavior Tree의 향후 새 저작 창은 LX를 재사용한다. 기존 자산 변환기를 이 Material 작업의 필수 산출물로 만들지 않는다. 기존 활성 소비자가 남아 있는 동안 imgui-node-editor를 제거하지 않는다. [도입 계획](../plans/LatticeAdoptionPlan.md)이 후속 범위를 소유한다.

## 구현 근거

- [`Lattice/Core/LXGraph.h`](../../Lattice/Core/LXGraph.h)
- [`Lattice/Core/LXDocument.h`](../../Lattice/Core/LXDocument.h)
- [`Lattice/Core/LXDocument.cpp`](../../Lattice/Core/LXDocument.cpp)
- [`Lattice/ImGui/LXCanvas.h`](../../Lattice/ImGui/LXCanvas.h)
- [`Lattice/ImGui/LXCanvas.cpp`](../../Lattice/ImGui/LXCanvas.cpp)
- [`Editor/EngineGUIWindow/MaterialGraphWindow.cpp`](../../Editor/EngineGUIWindow/MaterialGraphWindow.cpp)
