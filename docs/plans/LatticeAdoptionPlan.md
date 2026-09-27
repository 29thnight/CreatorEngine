# Lattice (LX) 머테리얼 우선 도입 계획

**2026-09-24 범위 수정 · 독립 예제 구현 진행 · LX 공수 미산정.** 명칭과
공통 계약·UI는 [LatticeNodeSystem.md](../design/LatticeNodeSystem.md)가 소유한다.
PHASE 4.25의 재질 의미와 Slang 생성은
[BlenderMaterialGraphPlan.md](BlenderMaterialGraphPlan.md)의 `MAT-0`~`MAT-9`가
소유한다. LX와 MAT의 같은 산출물을 중복 완료 처리하지 않는다.

현재는 **Material Graph만 선행**한다. 기존 `.bt`·AnimatorController 자산은
재작성 예정이므로 LX용 변환기, 의미 보존 migration, 기존 자산 fixture를 만들지
않는다. BT·Animator의 새 저작 창을 실제로 만들 때 LX 계층을 사용한다. 그때까지
기존 창과 `imgui-node-editor` 소비자는 유지한다. 모든 활성 소비자가 LX로
옮겨진 뒤에만 라이브러리를 제거한다.

LX-0의 Blender 5.1.1 노드/소켓 기준선과 Material 목표 schema·대표 graph·
패키지 소비 경계는 [대응표](../design/LatticeMaterialBlenderParity.md)에
고정했다. 이는 제품의 Blender 노드 지원 완료가 아니다.

독립 예제의 빌드·자체 검사·조작 증거와 재현 명령은
[Tools/LatticeExample/README.md](../../Tools/LatticeExample/README.md)에 기록한다.
LX-1의 정의 레지스트리·도메인 검증, 표시 이름과 다른 안정 소켓 식별자,
타입별 소켓 값, 노드의 위치·접힘·Frame 소속·화면 중심·확대율을 분리한
`LXLayout`의 저장 왕복은 독립 예제에서 검증했다. `.lxg` 8은 예제 형식이며
제품 `.shadergraph` schema가 아니다.
ImGui 비의존 `LXDocument`의 revision 검사·명령·dirty·Undo/Redo·Save도
독립 검사에 넣고, Material 예제 UI의 그래프 조작을 이 명령에 연결했다.
연결·거부·드래그 단일 Undo·Library 노드 생성은 문서 연결 상태에서 검사했다.
Material 예제의 Factor·Color·Texture 항목은 typed 핀 값에 연결했고 Factor의
UI 수정→저장·재개방을 검사했다. Property Editor는 여러 항목을 표시하고,
bool/int/float/vector/normal/color/texture 입력을 문서 명령·저장 왕복으로
검사했다. Frame 생성·이름·크기·묶음 이동·삭제와 DPI별 view 복원은 통과했다.
Node Group의 내부 그래프·순서 있는 입출력 interface·안정 ID와 인스턴스를
추가했다. 예제의 Library/Add 메뉴에서 인스턴스를 만들고, 내부 그래프와 외부
링크의 저장·재개방, Undo/Redo, 손상 interface 거부 및 `.lxg` 7→8 재저장을
검사했다. 그룹 내부 그래프는 별도 초안 캔버스에서 편집하고 적용 시 부모 문서의
단일 명령·Undo로 확정한다. 입력·출력은 내부 핀을 선택해 노출하고 이름·식별자·
순서·대응을 바꿀 수 있다. 기존 인스턴스 핀 ID와 링크를 유지하며 새 소켓은 모든
인스턴스에 배포한다. 연결된 소켓 제거는 무변경 거부한다. 적용·취소·두 인스턴스의
참조 유지와 새 입력·출력 노출을 독립 검사로 확인했다. 새 출력의 저장·재개방도
확인했다. `LX_GROUP_INPUT`/`LX_GROUP_OUTPUT` 경계 노드를 별도 그래프에 두고
외부 입력은 내부 출력 핀에, 외부 출력은 내부 입력 핀에 연결하는 방식도 추가했다.
기존 직접 대응 정의는 유지한다. 경계 노드의 연결·기본값·소켓 순서·인스턴스
핀 ID와 외부 링크, Undo/Redo·저장 왕복을 검사했다. 그룹 편집 화면의 타입별
interface 기본값과 경계 소켓 추가를 숨긴 ImGui 입력으로 검사했다. 선택한
Material 노드를 새 그룹으로 바꾸는 `LXCollapseToGroup` 문서 명령과 예제의
`Group selection`·`Ctrl+G`를 추가했다. 내부·외부 링크, 공유 입력 fanout,
Frame 소속, 단일 Undo/Redo, 저장 왕복과 숨긴 ImGui 버튼 입력을 확인했다.
그룹 인스턴스를 선택에 포함한 중첩 생성과 그룹 편집 화면의 내부 그룹 추가·
재그룹화를 검증했다. 루트 정의와 중첩 복사본의 ID를 공유하고 정의 수정과
새 interface 소켓을 중첩 인스턴스까지 전파한다. 소유·참조 순환, 복사본 불일치,
연결된 중첩 소켓 제거와 16단계 초과를 거부하며 Undo/Redo·저장 왕복을 확인했다.
Material 컴파일, LX-1 전체의 reroute·annotation,
안정 오류 코드는 남아 있다. HTTP는
Editor 이관 뒤 같은 문서 계층에 연결한다. LX-2는
독립 예제의 Debug/Release 자체 검사, 저장·복구, 검색·연결·선택·DPI 및 숨긴
DX11 캡처를 통과했다. 변경된 스타일의 보이는 창에서도 노드 드래그·접힘,
검색·생성, 핀 클릭·드래그 연결과 타입 거부, 빈 포트의 호환 노드 생성 검색,
복사/붙여넣기, Undo/Redo, 그래프 저장 후 프로세스 재실행·재개방을
확인했다. 외부 창 스타일의 격자 설정도 저장·재실행 뒤 복원됐다.
정의가 없는 노드를 담은 검사 그래프에서 실제 창의 진단 항목 클릭 이동을
확인했다. Library 추가 노드는 빈 공간에 놓고, 좁은 창에서 화면 밖에 놓일
경우 새 노드로 화면을 이동한다. 두 노드를 연속 추가한 뒤 저장·재실행·재개방을
확인했다. 실제 창의 Ctrl+클릭 다중 선택과 선택 노드 동시 이동은 사용자가
직접 조작해 정상 동작을 확인했다. 숨긴 ImGui 제스처 자체 검사도 통과했다.
이로써 `LX-2`의 **독립 예제 게이트는 통과**했다. 제품 연결 게이트는
끝나지 않았고, `LX-3` Editor 연결은 시작하지 않았다.

## 머테리얼 우선 순서

| 순서 | 닫을 범위 | 선행 | 완료 증거 |
|---|---|---|---|
| `LX-0` | Blender Shader Editor 대비 Material 노드·소켓·링크·그룹·기본값·편집 동작 대응표를 만들고 `.shadergraph` 목표 schema와 Editor/패키지 소비 경계를 확정. 독립 ImGui 예제의 pinned package/backend·빌드 입력 기록 | 없음 | 지원·미지원·예정 노드/조작 목록, 목표 schema와 대표 그래프 명세, `vcpkg.json`/프로젝트 링크 목록과 예제 빌드 입력 |
| `LX-1` | ImGui 비의존 `LXGraph`/definition/pin/link/layout/command, Material 타입 검증, 안정 ID와 새 Material schema migration 규칙 | LX-0 | 연결 가능·거부, 동적 소켓, cycle, 미등록 노드 보존, Undo/Redo, `.lxg` 예제 저장 왕복의 headless 검사 |
| `LX-2` | 독립 `LatticeExample.exe`에서 자체 `LXCanvas`와 Blender형 노드·소켓 UI, 검색, 접힘, 선택, pan/zoom, 타입별 내부 항목 구현 | LX-1 | Engine/Editor와 `imgui-node-editor` 링크 0, Debug/Release 빌드, 실제 조작·저장→재시작→재개방·DPI·resize·keyboard·다량 노드 확인 |
| `LX-3` | LX를 Editor host에 연결하고 `.shadergraph(domain=material)` 문서와 `LXMaterialCompiler`를 MAT 작업에 적용 | **LX-2 독립 게이트**, MAT-1 | Editor DX12/Vulkan 조작, Material graph·layout·default 저장 왕복, MAT-2/MAT-6의 typed IR·Slang·진단·마지막 정상 generation 확인 |
| `LX-3H` | 실행 중인 Editor의 **열린 Material LXDocument**를 인증된 HTTP CommandService로 조회·수정 | LX-3의 열린 문서 수명 | `/health` 프레임 진행·idle, `/commands`, `POST /command`로 핀 연결·해제→같은 창 반영→Undo/Redo→저장·재개방, revision 충돌과 무변경 오류 |

`LX-0`은 기존 BT·Animator 콘텐츠의 기준선 작업이 아니다. Material의 Blender
대응표에는 최소한 node tree·
node definition/instance·방향과 타입이 있는 socket·
link, 소켓 기본값과 값 위젯, 동적 소켓/표시 순서, node group/interface,
reroute·frame, 접힘·선택·이동·복사/붙여넣기, 검색·연결 제스처를 각각 기록한다.
대응표는 Blender 5.1.1을 기준으로 노드별 입력·출력·기본값·미리보기·지원
여부를 명시한다. `LXGraph`의 현재 문자열 속성·제한된 `PinType`·예제 노드
6종은 이 목표의 일부일 뿐이다. `MAT-0`의 렌더 비교 fixture와 이 저작 대응표는
서로 다른 판정이다.

대응표의 기준 자료는 [Blender 5.1 Shader Nodes](https://docs.blender.org/manual/en/5.1/render/shader_nodes/introduction.html),
[노드 편집 동작](https://docs.blender.org/manual/en/5.1/interface/controls/nodes/editing.html),
[NodeTree API](https://docs.blender.org/api/5.1/bpy.types.NodeTree.html)로 고정한다.

## 이후 창과 의존성 제거

| 순서 | 지금 상태 | 실제 착수 시 범위 |
|---|---|---|
| `LX-4` Animator | 보류. 독립 예제의 제목 전용 상태·방향 화살표 스타일만 검증 | 새 controller/state/transition 저작을 LX에 구현하고 새 자산의 저장·실행을 검증. 기존 controller/위치 JSON 변환은 수행하지 않음 |
| `LX-5` Behavior Tree | 보류 | 새 BT 저작을 LX에 구현하고 새 자산의 child order·decorator·weight·실행을 검증. 기존 `.bt` 변환은 수행하지 않음 |
| `LX-6` 의존성 제거 | 보류 | LX-3~5의 활성 UI 소비자가 모두 사라지면 `NodeEditor`/`BlueprintBuilder`/`PinHelper`와 `imgui-node-editor` include·링크·vcpkg 직접 의존 제거 |

`LX-4/5`는 현재 Material 선행 작업의 완료 조건이나 LX-0의 선행 조건이 아니다.
향후 각 창을 재작성할 때 새 자산 형식과 실행 계약을 정한다. 기존 자산의
의미 동등성·위치 보존은 이 계획의 게이트로 사용하지 않는다. 라이브러리 제거는
기존 두 창을 실제로 교체한 뒤 Editor Debug/Release 빌드와 새 그래프 제품
경로를 확인해 판정한다. 현재 C# Pipeline IR의 node 저작도 이후 요구가 생길 때
별도 산정한다.

## 엔진 연결 게이트

`LX-2` 독립 예제를 저장소의 pinned ImGui로 빌드·실행해 연결·거부·Undo/Redo,
헤더 접힘·핀/제목 Y축 정렬, 값 위젯, 저장→재시작→재개방, DPI/창 크기와
다량 노드를 확인한 다음 Editor 프로젝트에 연결한다. 독립 예제 통과는
Editor DX12/Vulkan이나 Material Slang 제품 경로의 통과를 뜻하지 않는다.

HTTP CLI는 별도 commandlet이 아니라 실행 중인 Editor의 열린 문서를 조작한다.
`POST /command` → `EditorCommandServiceHost` → Editor/game thread 명령 큐를
통해 UI와 동일한 문서 명령을 실행한다. 문서 ID·revision·안정 node/pin/link ID를
받고, 성공 시 새 revision을 반환한다. 수정은 한 Undo transaction과 dirty 표시로
전파하며 Save는 명시적 명령이다. 이 계약은 먼저 Material 창에서 검증한다.
