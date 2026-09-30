# Lattice (LX) — 공통 노드 저작 계층과 편집 UI

**2026-09-24 Material 우선 범위 수정 · 독립 예제 구현 진행.** 공식 제품 명칭은 **Lattice**, 내부 접두어는 **LX**
(`Lattice eXecution`)다. `imgui-node-editor`는 LX 편집 계층과 기존 소비자 이관이
끝나면 제거한다는 사용자 요구를 확정 조건으로 둔다. 아래의 타입 경계와 UI 세부는
공통 계약의 목표 형태다. 현재 독립 예제 구현·검증 범위는
[예제 README](../../Tools/LatticeExample/README.md)에 따로 기록한다. 실행 순서와
검증 게이트는 [LatticeAdoptionPlan.md](../plans/LatticeAdoptionPlan.md)가 소유한다.

## 1. 현재 소비 경로

| 표면 | 현재 정본과 화면 | LX에서 맡을 부분 |
|---|---|---|
| Material Graph (4.25) | MAT-2의 `.shadergraph(domain=material)`·typed IR·복구는 독립 예제 검증 완료. [MaterialGraphSchema.md](MaterialGraphSchema.md) | LX-3의 Canvas/명령·진단 UI 연결, MAT-6 Slang 생성 |
| Behavior Tree | [`BTBuildGraph.h`](../../Engine/SceneRuntime/BTBuildGraph.h)의 `.bt` 노드/자식, [`BTEditorBridge.h`](../../Editor/EngineGUIWindow/BTEditorBridge.h)와 [`MenuBarWindow.cpp`](../../Editor/EngineGUIWindow/MenuBarWindow.cpp)의 직접 `ax::NodeEditor` 호출 | 이후 BT 자산·창을 재작성할 때 LX를 사용. 기존 `.bt` 변환 없음 |
| Animator state graph | [`AnimatorEditorWindows.cpp`](../../Editor/EngineGUIWindow/AnimatorEditorWindows.cpp)의 `NodeEditor`와 `Assets/NodeEditor/*.json` 위치 sidecar | 독립 예제에서 상태 이름만 보이는 제목 전용 노드와 방향 화살표 전이 스타일을 검증. 이후 controller·창을 재작성할 때 LX를 사용. 기존 위치 JSON 변환 없음 |
| `NodeEditor` / `BlueprintBuilder` | [`NodeEditor.h`](../../Editor/ImGuiHelper/NodeEditor.h)는 프레임마다 노드를 만들고 이름으로 연결한다. [`BlueprintBuilder.h`](../../Editor/ImGuiHelper/BlueprintBuilder.h)는 node-editor 그리기 보조 | 공통 저작 모델로 승격하지 않고 소비자를 옮긴 뒤 은퇴 |
| 렌더 파이프라인 | [현재 C# Pipeline IR 정본](RenderPipelineTargetArchitecture.md)은 C# 저작 → native IR → RenderGraph 실행 | 향후 시각화/저작 요구가 생기면 같은 IR을 투영하거나 생성. 별도 실행 그래프는 만들지 않음 |

현재 `imgui-node-editor`는 [`vcpkg.json`](../../vcpkg.json)의 직접 의존이다. BT와
Animator가 실제 UI 소비자이므로 Material Graph 창만 LX로 그려서는 의존성을
제거할 수 없다. `InspectorWindow.cpp`의 `ed` 선언과 `PinHelper.h`의 include도
정리 대상이며, `DrawYamlNodeEditor.cpp`는 이름만 비슷한 YAML Inspector라
LX 이관 대상이 아니다.

## 2. 책임 경계와 이름

```text
Material .shadergraph ── LXMaterialDocument ── LXMaterialAsset
                                          │
                                LXNodeDefinition registry
                                          │
                            LXValidator → typed, immutable LXIR
                                          │
                            LXMaterialCompiler → Slang + ShaderMeta

Later: new BT/Animator authoring ── LXDocument + LXCanvas
       each domain keeps its own runtime contract

Editor/ImGui → LXCanvas + LXGraphEditor → LXDocument commands
HTTP CLI → CommandService → Editor 명령 큐 → 같은 LXDocument commands
```

- **`LXGraph`**는 편집 가능한 노드·핀·링크·속성의 값 모델이다. 노드/핀/링크는
  문서 내 안정 ID를 갖고, 화면 인덱스나 포인터를 저장 ID로 쓰지 않는다.
  `LXLayout`은 위치·접힘·Frame·주석·뷰 상태를 보존하되 컴파일 의미에서 제외한다.
  독립 예제의 현재 `LXLayout`은 위치·접힘·Frame 소속/경계·DPI 독립 화면
  중심/배율을 분리해 저장한다. 주석·reroute는 남아 있다. Node Group은
  Frame과 달리 자체 그래프와 인터페이스가 있는 의미 모델이다. 독립 예제의
  `LXGroupDefinition`은 내부 그래프·안정 interface 소켓 ID·순서를 소유하고,
  `LX_GROUP` 인스턴스의 핀은 그 ID를 참조한다. `.lxg` 8에서 정의와 인스턴스를
  함께 왕복한다. 내부 그래프는 초안으로 열고 `LXReplaceGroupBody` 명령으로
  한 번의 Undo 변경을 적용한다. `LXUpdateGroup`은 내부 핀을 입력·출력으로 노출하고
  이름·식별자·순서를 수정한다. 기존 interface·인스턴스 핀 ID와 외부 링크는
  유지하며, 연결된 소켓 제거와 노출한 내부 핀이 사라진 초안은 거부한다.
  새 그룹 경계 방식은 `LX_GROUP_INPUT`의 출력 핀과 `LX_GROUP_OUTPUT`의 입력
  핀을 interface 소켓에 대응시킨다. 내부 링크가 값 흐름을 명시하며 타입별
  기본값은 interface 편집 화면에서 설정하고 인스턴스에 전파한다. 기존 직접 핀
  대응 그룹도 읽는다. `LXCollapseToGroup`은 선택한 Material 노드를
  내부 그래프로 옮기고 외부 링크를 Group Input/Output 경계 소켓에 대응시킨다.
  공유 외부 입력은 한 interface 소켓으로 합치며 중복되지 않은 외부 링크 ID를
  유지한다.
  예제의 `Group selection`·`Ctrl+G`가 한 문서 명령으로 실행하고 Undo 한 번에
  되돌린다. 선택한 그룹 인스턴스의 정의는 새 그룹 내부 그래프에 복사해
  중첩한다. 문서 루트 정의와 중첩 복사본은 정의·interface ID를 공유한다.
  루트 정의의 이름·본문·소켓 변경은 복사본과 중첩 인스턴스에 원자적으로
  전파하며, 기존 핀 ID와 링크를 유지한다. 소유·정의 참조 순환, 복사본 불일치,
  연결된 소켓 제거와 16단계 초과는 거부한다. 컴파일 lowering은 아직 없다.
- **`LXNodeDefinition`**은 type ID, domain, 핀 schema, 기본값, 동적 핀 규칙과
  생성 가능 조건을 등록한다. **`LXNodeRecord`**는 저장 자료다. 요청의
  `ULXNode` 이름은 이 정의 객체가 실제로 `U` 객체 수명/리플렉션 규약을 가질 때
  사용한다. 현재 코드베이스에 그런 공통 기반은 없으므로 Unreal의 `U` 접두어를
  자료 레코드에 붙여 타입 계층을 가장하지 않는다.
  독립 예제의 `LXNodeDefinitionRegistry`는 material/behavior/animation 도메인의
  허용 핀 타입과 생성 가능한 타입 정의를 보유한다. 그래프에 등록부를 주입하면
  새 노드 생성·연결·붙여넣기와 파일 재개방 시 정의의 도메인/고정 핀/동적 핀
  규칙을 대조한다. 등록되지 않은 저장 노드는 경고와 함께 원래 핀·속성·링크를
  보존하고 데이터 편집을 막는다. 정의는 있으나 도메인이나 schema가 다른 노드는
  오류로 거부한다. 현재 구현은 독립 `.lxg` 예제의 정책이다. Blender Mix의
  중복 표시 이름은 별도 소켓 identifier로 구분하고, bool/int/float/vector/
  color/texture 기본값은 Core에서 타입 검사·Undo/Redo·저장 왕복을 한다.
  Material 예제의 Factor·Tint Color·Texture 항목은 typed 핀 값을 읽고,
  수정은 `LXSetSocketValue`로 보낸다. 예전 예제 파일의 문자열 property는
  핀 값이 비어 있을 때 표시 대체값으로 읽는다. Property Editor는 등록된
  모든 항목을 표시하고 bool/int/float/vector/normal/color/texture 입력을
  같은 문서 명령으로 처리한다.
- **`LXPinType`**은 flow, bool/int/float, vector/color/normal, texture/sampler,
  object/resource, surface/closure 등 의미와 shape를 분리해 기록한다. 색상만으로
  타입을 판단하지 않는다. 링크는 output → input, domain 호환, 다중 입력 수,
  명시된 변환, 필수 핀과 cycle 정책을 검증한다. 자동 변환은 registry에 선언된
  무손실 변환만 허용하고 나머지는 사용자가 변환 노드를 고른다.
- **`LXValidator`**는 저장·컴파일 전에 ID 중복, 유실된 핀, 잘못된 타입·방향,
  허용되지 않은 cycle, 누락 output을 안정 순서로 진단한다. 진단은 graph/node/pin
  ID와 오류 코드를 갖는다. 미지의 node type은 원자료를 보존해 읽기 전용으로
  보여 주고 마지막 정상 compiled generation을 유지한다.
- **`LXCompiler`**는 schema 검증, 정규화, domain lowering, source mapping을
  조정하는 계층이다. 하나의 범용 VM을 뜻하지 않는다. Material은 순수 데이터
  의존 그래프를 deterministic Slang으로 내린다. 향후 BT/Animator는 각 런타임
  의미에 맞는 새 저작 계층을 갖는다. Flow/Exec 핀은 실행 순서가 있는 domain에만
  나타난다. Material 그래프에 K2의 Exec 선을 억지로 넣지 않는다.
- **`LXGraphEditor` / `LXCanvas`**는 Editor 전용이다. ImGui의 입력·DrawList는
  사용하되 node-editor 라이브러리의 context, pin/link API나 설정 파일에
  의존하지 않는다. hit test, pan/zoom, selection, wire routing, drag preview,
  creation gesture는 LX가 소유한다. Engine 쪽 graph/validator/compiler 자료는
  ImGui 타입을 포함하지 않는다.

Material의 목표 저장 정본은 새 `.shadergraph(domain=material)`이다. 그래프 값과
별도 `LXLayout`을 같은 문서에 저장하며 MAT-2 독립 검사를 통과했다. 현재 예제 UI는 `.lxg` 9를
사용한다. BT·Animator는 자산과 창을 재작성할 때
새 정본을 정하며 기존 `.bt`/controller/위치 JSON 변환은 구현하지 않는다.
LX는 이후 창에도 편집 명령과 캔버스를 제공하고 실행 의미는 각 시스템에 남긴다.

### 실행 중인 Editor의 HTTP CLI 계약

LX를 Editor에 연결할 때 CLI는 **현재 열린 LXDocument**를 실시간으로 조회·수정한다.
별도 프로세스에서 자산을 열어 수정하는 commandlet 경로는 이 요구를 충족하지 않는다.
기존 Editor의 `--command-service`가 제공하는 인증된 loopback HTTP/JSON을 쓴다.
클라이언트는 `Library/CommandService/endpoint.json`에서 실행 중인 Editor의
주소·토큰·PID를 확인하고, `GET /health`로 프레임 진행과 `idle` 상태를 확인한다.
명령 목록은 `GET /commands`, 실행은 **`POST /command`**이며, 긴 명령의 `202`
응답은 `GET /operations/{id}`로 완료를 확인한다. HTTP 수신 스레드는 문서를
직접 읽거나 바꾸지 않는다. 기존 `EditorCommandServiceHost`의 구조화 명령 큐를
통해 Editor/game thread에서 문서 명령을 실행한다.

Editor 화면과 HTTP 명령은 동일한 `LXDocument` 명령 계층을 소비한다. 이 계층은
열린 문서의 안정 `documentId`와 자산 식별자, 현재 `revision`을 제공한다.
수정 요청은 `documentId`·`expectedRevision`·안정 node/pin/link ID를 받아
대상 문서가 닫혔거나 revision이 달라졌으면 변경 없이 충돌을 반환한다. 성공 시
새 revision과 변경된 ID를 구조화 결과로 돌려준다. 화면 선택 상태나 ImGui
포인터를 HTTP 식별자로 쓰지 않는다. mutation은 한 Undo transaction으로 기록하고
dirty 표시·캔버스 갱신을 같은 문서에 전파한다. 저장은 명시적인 별도 명령이며
domain adapter의 기존 정본에 쓴다.

명령 registry에는 다음 표면을 등록한다. 이름과 named parameter schema는
`LX-3H`에서 확정·검색 가능하게 한다. 여기 적은 동작은 **설계 목표**이며 아직
등록된 HTTP 명령이 아니다.

| 명령 범위 | 필요한 동작 |
|---|---|
| 문서·그래프 조회 | 열린 LX 문서 목록, 문서 ID/자산 경로/revision, node·pin·link와 타입·방향·연결 상태 조회 |
| 핀·링크 수정 | 호환성 검증 후 output→input 연결·해제, 입력 기본값 변경, schema가 허용한 동적 핀 추가·삭제·순서 변경 |
| 문서 조작 | Validate, Undo/Redo, 명시적 Save. 각 명령은 적용 revision·ID·진단을 구조화 결과로 반환 |

핀 조작은 node definition의 schema와 domain adapter 제약을 통과해야 한다.
BT의 child order와 Animator의 transition 의미를 일반 링크 순서로 덮어쓰지 않는다.
`correlationId`는 결과 추적용이며 중복 실행 방지 키가 아니다. 동기 요청이
timeout 나도 이미 큐에 들어간 변경은 나중에 실행될 수 있으므로 자동 재시도하지
않는다. 클라이언트는 operation 결과 또는 현재 문서 revision을 다시 조회한다.
유효하지 않은 ID·타입·방향·revision·닫힌 문서는 안정 오류 코드와 함께 무변경
결과를 반환한다. HTTP 인증·loopback 제한·요청 상한은 기존 CommandService
계약을 그대로 따른다.

독립 예제에는 현재 `LXDocument`의 코어 명령과 revision·dirty 검사가 있다.
Material 예제 UI의 그래프 변경은 이 문서 명령을 통과한다. Behavior 예제는
직접 `LXGraph`를 조작한다. Editor/HTTP 연결 및 오류 코드의 세분화는
구현하지 않았다.

## 3. 편집 UI 제안

Material의 목표는 Blender Shader Editor의 **저작 구조와 조작을 거의 1:1로
대응**시키는 것이다. `LXGraph`는 node tree, node definition/instance, 이름·방향·
타입·순서·기본값이 있는 socket, link, node group/interface를 표현해야 한다.
`LXLayout`과 캔버스는 reroute·frame, 접힘·선택·이동·복사/붙여넣기·검색과
소켓에서 시작하는 연결 동작을 맡는다. 노드 내부에는 socket에 맞는 숫자·색상·
텍스처 미리보기 같은 컨트롤을 둔다. 각 노드의 입력·출력·기본값·표시 조건과
지원 여부를 Blender 5.1.1 기준 대응표에 기록한 뒤 Material 정의를 늘린다.
현재 예제의 6개 Material 노드와 문자열 속성은 이 목표를 완료하지 않는다.

2026-09-23 마지막으로 제공한 **Blender 노드 편집기 이미지**를 시각 기준으로
한다. 어두운 중성 회색 패널·촘촘한 격자·낮은 노드 높이·절제된 타입별 헤더,
작은 색상 소켓·가는 회색 연결선과 선택된 노드의 주황색 테두리를 적용한다.
앞서 제공한 NebulaFlow 이미지는 3열 패널 배치 참고로 유지한다.
[이전 스타일 시안](LatticeStyleStudy.svg)은 현재 기본 스타일을 반영하지 않는다.
[독립 ImGui 예제의 실제 렌더 결과](LatticeImGuiPreview.png)는 현재 구현 상태를
보여 준다.
[기존 와이어프레임](LatticeEditorWireframe.svg)은
  패널 배치를 위한 초기 자료다. Material은 좌우 핀, 독립 BT 표현 fixture는
  위아래 핀을 사용한다.
스크린샷의 텍스트나 에이전트 실행 기능을 LX에 복사하는 뜻은 아니다.

| 위치 | 공통 동작 | domain 추가 표면 |
|---|---|---|
| 상단 얇은 document bar | 자산명·dirty 표시, 새 문서/저장, Undo/Redo, Validate, 검색 | Material: Compile. BT/Animator: 각 실행 상태 |
| 왼쪽 Library + Property Editor | 노드 검색·분류·즐겨찾기와 선택 노드의 속성·기본값 | Material: 파라미터·texture. BT: decorator/weight. Animator: transition 조건 |
| 가운데 Canvas | 격자 표시 전환, 조밀한 노드·행에 연결된 pin·wire, 다중 선택, pan/zoom, 선택 강조 | Material은 좌우 핀, BT 표현 fixture는 위아래 핀. domain별 pin 의미/개수와 cycle 정책은 유지 |
| 오른쪽 Flow/Trace 패널 | 단계·연결 순서/진단 목록, 항목 클릭 시 node로 이동 | BT/Animator는 실행 중 `Playbox`, Material은 평가·컴파일 trace. 실행 버튼은 실제 실행이 있는 domain만 |
| 접이식 Problems/Preview | 오류에서 node/pin으로 이동, 현재 실패와 마지막 정상 결과 구분 | Material preview는 별도 tab/패널로 열고 같은 compiled generation을 사용 |

### 시각·조작 규칙

- 기본 node 폭은 내용과 스타일에 맞추고 높이는 header·pin 행·속성 미리보기를
  기준으로 계산한다. Blender를 참고한 낮고 단색에 가까운 타입별 header와
  어두운 회색 body를 쓰며 선택은 주황색 테두리로 표시한다. 기본 연결선은
  회색이고 소켓은 타입별 색상을 쓴다. 독립 예제는 `LXStyleSheet`가 시각값을 소유하고 엔진
  연결 시 `EditorTheme`와 대응시킨다. 오류는 아이콘·문장으로도 구별한다.
- 헤더 왼쪽 화살표로 노드를 접으면 제목과 핀을 유지한 캡슐로 표시한다. 여러
  핀도 구별 가능한 위치에 남기고 같은 위치 계산을 hit test와 연결선에 쓴다.
  좌우 핀이 하나씩인 접힌 Material 노드는 두 핀·화살표·제목을 캡슐의 중앙
  Y축에 맞춘다. 펼친 헤더도 화살표와 제목의 중앙을 맞추며, 핀 클릭 영역과
  연결선은 실제로 그린 핀 좌표를 사용한다. 기본 최소 높이는 0으로 두어 핀
  한 행만 있는 노드의 여백이 커지지 않는다.
- 창 종류와 문서마다 별도 외부 `.lxstyle`을 읽으며 공통 Blender 바탕 위에
  node type, pin layout, wire, 내부 카드 역할별 색을 재정의한다. Behavior Tree
  표현 fixture는 위아래 핀·세로 연결·파란 decorator·초록 service·보라 task
  카드를 같은 LXCanvas에 적용한다. Animation FSM 표현 fixture는 기존 Animator
  창처럼 상태 이름만 남기는 `headerOnly` 노드와 가운데 화살표가 있는 전이선을
  적용한다. 원형 Flow 포트는 상대 상태를 향한 왼쪽 또는 오른쪽 캡슐 외곽에
  놓고, 같은 측면의 전이가 여러 개면 세로로 나눈다. 선 끝점과 hit test는
  포트 좌표를 공유한다. 가까운 높이의 포트는 같은 Y축으로 맞춰 수평 직선으로
  연결한다. 그 밖의 전이선은 포트에서 짧게 수평으로 나온 다음 직접 이어지며,
  역방향도 같은 경로 규칙을 사용한다. 이는 스타일·편집 제스처
  검증이며 기존 controller·`.bt`
  데이터 adapter는 아직 없다.
- 입력 pin은 해당 입력 행의 왼쪽 끝, 출력 pin은 해당 출력 행의 오른쪽 끝에
  둔다. pin과 label의 세로 좌표는 같은 행에서 계산하고 그 좌표를 hit test와
  wire 연결에도 공유한다. 타입을 색만으로 구별하지 않도록 circle·triangle·diamond
  shape와 이름·타입 tooltip을 함께 둔다. 순서가 의미 있는 입력은 작은 번호를
  선 또는 pin 옆에 표시한다.
- `LXStyleSheet`는 canvas 기본값, node type·개별 node, pin type·개별 pin,
  wire type·개별 link의 스타일을 차례로 재정의할 수 있다. 색, shape, radius,
  핀 표시, 제목 전용 캡슐, 선 굵기·굴곡·직선/곡선 경로와 방향 화살표를 graph
  실행 의미에서 분리한다. 독립 예제는
  `LatticeStyles/<창 종류>.<문서명>.lxstyle` 외부 파일로 스타일을 저장·복원한다.
  엔진 자산의 최종 스타일 저장 위치와
  theme 승계 정책은 Editor 이관 시 결정한다.
- 빈 입력의 기본값은 왼쪽 Property Editor에서 편집한다. 작고 자주 바꾸는 값은
  node 안에도 노출할 수 있다. 연결된 입력은 값을 비활성화하고 source를 보인다.
  연결 중에는 유효 target만 강조하고 거부 이유를 cursor 근처에 표시한다.
- node 내부 항목은 타입별 표시 계약을 갖는다. 현재 독립 예제의
  `LXNodeItemRegistry`는 텍스트·숫자 슬라이더·RGB/색조 선택기·텍스처 미리보기를
  노드 타입과 속성 키로 지정한다. 항목은 입력 pin 이름에 연결해 같은 행에
  배치할 수 있으며, 연결된 입력의 슬라이더는 `Linked`로 바뀐다. 색상은 Apply,
  슬라이더는 드래그 종료 시 각각 하나의 Undo 기록으로 저장한다. 텍스처는
  도메인이 제공하는 `ImTextureID` resolver를 사용하며, 실제 자산 로더가 없는
  독립 예제의 기본 썸네일은 시각 확인용 예시다.
- 빈 캔버스의 Space/우클릭은 node 검색, port에서 빈 곳으로 끌면 호환 node만
  검색한다. 더블 클릭 wire는 reroute, 드래그는 자동 pan, Ctrl+C/V는 ID를
  재발급해 붙여넣기, Delete는 하나의 Undo transaction으로 처리한다.
- 격자는 스타일에서 선택하며 현재 독립 예제 시안은 약한 격자를 켠다. 확대/축소·fit·잠금은
  캔버스 왼쪽 아래에 작은 도구 묶음으로 둔다. 크기·hit area와 텍스트는
  예제의 DPI/글꼴 배율을 따르고 엔진에서는 `ThemePixels`를 소비한다. 좁은 창에서는 양쪽 panel을 접어
  canvas를 우선한다.

오른쪽 목록은 사용자가 제공한 화면의 `Playbox` 위치와 계층을 따른다.
Material에서는 shader를 순서대로 매 frame 실행하는 패널로 오해하지 않도록
`Evaluation Trace`라 부르고, BT/Animator에서만 활성 상태·단계별 실행을
표시한다. 공통 캔버스는 UI 표현을 공유하지만 domain 실행 의미는 공유하지 않는다.

## 4. 엔진 도입 전 독립 검증 경계

첫 UI 구현은 Engine/Editor 프로젝트에 연결하지 않는다. 2026-09-23 현재
저장소의 vcpkg 설치본 Dear ImGui **1.92.8**은 Win32·DX11 backend header와
`imgui.lib`를 제공한다. 이 설치본을 사용해,
[공식 `example_win32_directx11` 구성](https://github.com/ocornut/imgui/blob/master/docs/EXAMPLES.md)을
따르는 독립 `LatticeExample.exe`를 만든다. 예제는 `LXGraph`/`LXCanvas`의
소비자이자 조작 fixture이며, 예제 전용 mock node·property/trace adapter만
포함한다. Engine header, Editor theme 구현, RHI, `imgui-node-editor`를 링크하지
않는다. 실제 engine data adapter는 이 단계에 넣지 않는다. 빌드 구성은
저장소와 같은 x64·VS18/v145를 기준으로 하되 Editor.vcxproj를 참조하지 않는다.
DX11은 예제의 창·렌더 setup을 작게 유지하기 위한 선택이며 LX canvas는
ImGui 입력과 DrawList만 소비한다. DX12/Vulkan Editor backend 결과는 엔진 연결
후 별도 확인한다.

독립 예제에서 저장→재시작→재개방, 여러 port 연결/거부, pan/zoom,
selection/undo, 긴 label·많은 node, DPI/창 크기, keyboard 조작과 진단 이동을
검증한다. 빌드 로그·재현 명령·fixture·스크린샷을 남기고 게이트가 통과한 뒤에만
Editor host adapter를 추가한다. 독립 예제 통과는 DX12/Vulkan Editor 동작이나
Material Slang 제품 경로의 증거가 아니다.

## 5. 이번 결정에서 제외한 방식

| 후보 | 제외 근거 |
|---|---|
| `imgui-node-editor`를 LX 이름으로 감싸기 | 라이브러리 교체가 완료되지 않고 hit test/selection/layout 소유권이 둘로 남는다. 짧은 이관 adapter만 허용 |
| 모든 graph에 `.lxgraph` 파일을 새 정본으로 추가 | 현재 Material은 `.shadergraph`를 정본으로 삼는다. 이후 창의 새 자산 형식은 그 창을 재작성할 때 결정한다 |
| K2 전체 VM/bytecode를 먼저 복제 | Material Slang, BT, Animator, RenderGraph의 실행 의미가 다르다. 공통층은 저작·검증·진단까지 |
| Pipeline node 창을 새 실행기로 만들기 | C# 저작 → 동일 immutable Pipeline IR → 기존 native RenderGraph 계약과 충돌한다 |
| Editor 창에서 먼저 LX UI를 개발 | canvas 입력·저장 결함을 엔진 렌더/호스트 문제와 함께 디버그하게 된다. 독립 ImGui 예제 빌드·조작 게이트를 먼저 닫는다 |

## 6. 완료 판정의 경계

설계 문서와 와이어프레임은 구현 증거가 아니다. Material 선행 완료는
Blender 대응표의 약속한 노드·소켓·조작, 새 `.shadergraph` 저장→닫기→재개방,
컴파일 진단과 Editor 조작 회귀로 판정한다. BT·Animator의 새 창은 후속 작업이다.
`imgui-node-editor` 제거는 그 활성 UI 소비자가 모두 사라지고 include·링크·
`vcpkg.json` 직접 의존이 0일 때 판정한다. 제거 직후 Editor Debug/Release와
새 그래프 자산의 제품 재개방을 확인한다.

K2에서 참고한 것은 [실행 핀과 데이터 핀의 구별](https://dev.epicgames.com/documentation/unreal-engine/nodes-in-unreal-engine)과
[노드 처리기·중간 표현·백엔드를 나누는 컴파일 단계](https://dev.epicgames.com/documentation/en-us/unreal-engine/compiler-overview-for-blueprints-visual-scripting-in-unreal-engine)다.
Lattice의 domain별 런타임 계약은 위 §2처럼 CreatorEngine의 현재 경계에 맞춘다.

## 7. 2026-09-29 Material Editor host

[MaterialNodeEditor.md](MaterialNodeEditor.md)의 등록 Editor 창이 독립 예제 검증 이후의
첫 제품 LXCanvas 소비자다. MeshRenderer에서 진입하며 행 기반 항목/핀 정렬,
Blender형 기본 스타일·외부 스타일 파일과 `.shadergraph` 저장을 사용한다.
문서·revision을 검증하는 인증 HTTP도 같은 LXDocument에 연결된다.
Scene 자원 게시와 Player cooked 실행은 기존 MAT 제품 경로가 소유한다.
BT/Animator 교체나 `imgui-node-editor` 전체 제거를 이 연결의 완료로 세지 않는다.
