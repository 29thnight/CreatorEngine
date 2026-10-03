# Lattice (LX) 머테리얼 우선 도입 계획

**2026-10-02 재평가 · 머테리얼 제품 편집 마감 진행 · LX 공수 미산정.** 명칭과
공통 계약·UI는 [LatticeNodeSystem.md](../design/LatticeNodeSystem.md)가 소유한다.
PHASE 4.25의 재질 의미와 Slang 생성은
[BlenderMaterialGraphPlan.md](BlenderMaterialGraphPlan.md)의 `MAT-0`~`MAT-9`가
소유한다. LX와 MAT의 같은 산출물을 중복 완료 처리하지 않는다.

현재 Material Node Editor와 MeshRenderer Inspector는 LX 문서·캔버스 및
실시간 HTTP 명령에 연결되어 있다. MAT-0~6·MAT-8과 기존 MAT-7 기반은 구현 이력으로 보존한다.
Graph→ShaderMeta/Slang→공통 재질 소비는 MAT-7의 열린 목표이고 MAT-9의 렌더 대조·성능
수용은 진행 중이다. LX-3/3H는 제품의 전체 조작·그룹/Blackboard
전용 저작 패널까지 닫은 상태가 아니므로 진행 중으로 유지한다.
[편집기 설계](../design/MaterialNodeEditor.md)와
[2026-09-30 다른 세션의 성능 개선](../analysis/MAT9NodeEditorPerformance.md)을 따른다.
아래 날짜별 기록의 미구현·잔여 표기는 해당 측정 시점의 상태다.

## 현재 잔여와 재작성 제외

| ID | 남은 목표 | 재사용하는 구현 기반 |
|---|---|---|
| LX-0 | 완료·이력 | Blender 노드/소켓 대응 기준선. 전체 제품 노드 지원 완료로 사용하지 않음 |
| LX-1 | annotation schema/왕복과 wire Reroute 삽입 transaction·Undo/진단 | registry·typed pin·ID·문서 명령·Frame·그룹/중첩, MAT-2/6의 IR/생성·Reroute lowering·안정 진단 |
| LX-2 | 완료·이력 | 독립 예제 빌드·실제 조작·저장/재실행 |
| LX-3 | 그룹 내부/interface·Blackboard 제품 패널과 전체 지원 조작/DPI/재개방 마감 | MeshRenderer 진입·Editor 창·스타일·typed 위젯·저장/Apply |
| LX-3H | host health·씬/문서 전환·종료·충돌 무변경 수명 게이트 | 인증 material.editor·ID/revision·조회/수정·Undo·Save/Apply |
| LX-4~6 | LX 횡단 후속. 4.25 완료 조건에서 분리 | 현재 Animator/BT 창 유지; 기존 자산 변환 작업 없음 |

“Material 컴파일 없음”, “Reroute 없음”, “HTTP는 이관 뒤 연결”, “Editor 연결 미착수”는 현재 상태로 사용하지 않는다. 해당 기반은 구현돼 있으며 [기존 노트 원문](archive/Phase425ImplementationHistory.md)에 보존했다. 전체 노드/enum 지원과 자유 주석·전용 편집 제스처는 지원 대응표에서 별도로 판정한다.
공통 ShaderMeta 생성·Material binding/PSO·Forward+ 통합은 MAT-7이 소유하고 LX는 문서/UI/HTTP 저작 계약을 닫는다.

현재는 **Material Graph만 선행**한다. 기존 `.bt`·AnimatorController 자산은
재작성 예정이므로 LX용 변환기, 의미 보존 migration, 기존 자산 fixture를 만들지
않는다. BT·Animator의 새 저작 창을 실제로 만들 때 LX 계층을 사용한다. 그때까지
기존 창과 `imgui-node-editor` 소비자는 유지한다. 모든 활성 소비자가 LX로
옮겨진 뒤에만 라이브러리를 제거한다.

LX-0의 Blender 5.1.1 노드/소켓 기준선과 Material 목표 schema·대표 graph·
패키지 소비 경계는 [대응표](../design/LatticeMaterialBlenderParity.md)에
고정했다. 이는 제품의 Blender 노드 지원 완료가 아니다.

<details>
<summary>독립 예제에서 제품 연결까지의 구현 이력 — 당시 잔여 표기 보존</summary>

독립 예제의 빌드·자체 검사·조작 증거와 재현 명령은
[Tools/LatticeExample/README.md](../../Tools/LatticeExample/README.md)에 기록한다.
LX-1의 정의 레지스트리·도메인 검증, 표시 이름과 다른 안정 소켓 식별자,
타입별 소켓 값, 노드의 위치·접힘·Frame 소속·화면 중심·확대율을 분리한
`LXLayout`의 저장 왕복은 독립 예제에서 검증했다. `.lxg` 9는 예제 형식이며
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

2026-09-28 MAT-2: `Lattice/Material`의 `.shadergraph` 문서·Blackboard·소켓 색 공간/
표시 상태·공유/중첩 그룹·typed IR과 마지막 정상 IR 유지가 독립 예제 Debug/Release
95개 신규 검사 및 LX 전체 검사를 통과했다. 초기 Blender 6개 정의의 소켓 기준과
Material 전체 transaction/Undo/Redo를 포함한다. [MaterialGraphSchema.md](../design/MaterialGraphSchema.md)가
이 계약을 소유한다. LX-3는 기존 Canvas/명령을 이 문서에 연결하고 제품 경로를 검증해야 하며
LX-3H 실시간 HTTP, MAT-6 Slang·MAT-7 GPU/asset generation은 아직 남는다.

2026-09-28 MAT-3: 공용 core Principled 의미·직접광·IOR-aware IBL 응답을 독립 GPU에서
8,280개 수치 검사로 검증했다. [PrincipledCoreSemantics.md](../design/PrincipledCoreSemantics.md)를 따른다.

2026-09-28 MAT-4: 공용 Layered coat/sheen/anisotropy/thin film을 GPU 216,776개 수치·독립성
검사와 energy bound/numeric golden으로 검증했다. [PrincipledLayeredSemantics.md](../design/PrincipledLayeredSemantics.md)를 따른다.
이 결과는 LX-3 Editor 연결이나 MAT-6/MAT-7 graph→제품 lookup/route의 완료로 세지 않는다.
이 결과는 LX Editor Canvas 이관, Slang codegen이나 제품 lookup/packing 완료를 뜻하지 않는다.

2026-09-28 MAT-7: RenderEngine에 Lattice Material 소스를 연결하고 reflection/binding,
후보 PSO·texture owner 수명, typed cooked generation·AssetCooker·loose/encrypted PAK의
독립 검증을 진행했다. [MaterialGraphProduct.md](../design/MaterialGraphProduct.md)를 따른다.
DataSystem/Material GUID generation·typed instance 저장 왕복/실패 복구를 연결했다.
독립 runtime 43개 및 실제 DataSystem 직접 호출 Debug/Release 각 26개를 검사했다.
제품 b2·texture·독립 sampler render binding adapter를 실제 엔진 DX12 오프스크린
draw로 검증했다(Debug/Release 각 168개 검사·64개 GPU 성분). Scene/Editor 소비 증거는 아니다.
Core/Layered evaluated-point IBL bake→lookup 소비 draw도 실제 엔진 RHI로 검사했다
(Debug/Release 각 19,351개 검사·18,240개 GPU 성분). Scene lookup 배치/보간/재사용과
환경 MIS/수렴·실시간 예산은 남는다. [PrincipledIblBake.md](../design/PrincipledIblBake.md)를 따른다.
[MaterialGraphScenePacket.md](../design/MaterialGraphScenePacket.md)의 material render owner는
graph instance·coverage/queue·PSO·IBL·binding을 함께 보존하고 제출 성공 확인 뒤 게시한다.
실제 두 in-flight GPU 제출에서 교체·실패·abort·완료 해제를 검사했다. Scene pass 설치는 후속이다.
Scene의 graph pass 소비·자동 route와 고급 transport는 남으며 LX-3 Editor
Canvas/HTTP 이관 완료로 세지 않는다.
[MaterialGraphSurfaceBatch.md](../design/MaterialGraphSurfaceBatch.md)의 공간·시선 graph CS는
UV/LOD·world frame·eye·typed input을 evaluated GPU point buffer로 만들어 IBL bake/draw에
전달한다. 잘못된 지점 진단과 완료 readback acceptance를 검사하는 제품 RHI 기반 경로다.
[MaterialGraphMeshSurface.md](../design/MaterialGraphMeshSurface.md)의 제품 정점 형식·pose 복사와
GPU skin/world 입력 생성, GPU Scene packet의 완료 검증 후 게시도 연결했다.
큰 메시의 triangle 분할·원본 remap/예산 거부, UV/world frame 보간 후 재질·IBL 평가와
명시적 footprint LOD도 추가했다. 실제 Scene visible sample/texture별 footprint,
lookup 해상도/재사용/오차/실시간 예산·RenderGraph/async polling, LX Editor 편집 설치는 남는다.

[MaterialGraphRasterSurface.md](../design/MaterialGraphRasterSurface.md)의 독립 RHI graph는
depth/MRT의 가시 픽셀·원근 UV/frame·fine derivative를 graph/IBL 평가에 전달한다.
동일 graph의 평가·IBL 준비/선언/병렬 기록은 [MaterialGraphPassRecording.md](../design/MaterialGraphPassRecording.md)에 구현했다. 단일 재질 chunk 묶음·4096픽셀 범위이며 제품 Scene의 공유 depth·조명/컬러·
async 게시와 texture별 footprint, LX Editor 편집의 완료를 의미하지 않는다.

[MaterialGraphSharedDepth.md](../design/MaterialGraphSharedDepth.md)에 current mesh/skin 생산과
Core/Layered의 read-only depth·coplanar winner 공유를 같은 graph에 확장했다. 독립 RHI 범위이며
실제 Scene GPU host·legacy/Masked depth·조명/컬러와 LX Editor 연결은 계속 남는다.

[MaterialGraphSceneInput.md](../design/MaterialGraphSceneInput.md)에 producer의 Mesh/Foliage proxy/delta와
실제 draw pool/per-view CPU 입력 밀봉을 연결했다. 같은 typed instance·복사한 geometry/pose·camera
입력을 native shared-depth graph에 공급한다. 실제 Scene GBuffer composition host는 아직 거부하며
이 입력 경계 설치를 Scene 렌더 활성화나 LX Editor 창 이관 완료로 세지 않는다.

</details>

## 머테리얼 우선 순서

### 2026-09-29 Editor Material 창 연결

[MaterialNodeEditor.md](../design/MaterialNodeEditor.md)에 현재 구현과 검증 경계를 기록한다.
MeshRenderer의 재질 이름/More 메뉴가 등록된 Material Node Editor를 열며,
Blender형 단일 상단바·캔버스 위 breadcrumb·Principled/Output 캔버스를 제공한다.
왼쪽 context/메뉴·중앙 slot/material/문서 작업 아이콘·오른쪽 overlay 순서이며
헤더와 캔버스는 외부 LX 스타일의 배경색을 공유한다. 작은 꺾쇠와 아이콘 묶음을
사용하며 오른쪽 스냅 버튼은 다중 선택의 상대 위치를 보존하는 격자 스냅을 제어한다.
상단 아이콘과 breadcrumb는 공통 Material Symbols 폰트로 통일한다.
Material 기본 격자는 점으로 표시한다. 점 패턴·크기는 LXS 6 외부 스타일에 저장하고
이전 스타일 파일도 계속 읽는다.
typed 값·enum·texture preview와 고급 section을 실제 Material definitions에서 등록하고
핀/항목/높이를 같은 row 목록으로 계산한다. 엔진 사용자 배율과 monitor DPI의 곱을 사용한다.

저작 저장은 metadata를 보존한 `.shadergraph`, 적용은 검증된 authoring Slang/Scene
generation, MeshRenderer 바인딩 영속화는 Scene 저장으로 나눈다. 실패 시 기존 정상
generation을 유지한다. 재시작한 Editor는 새 graph source를 authoring compile로 읽으며
Player는 기존 cooked bytecode 경계를 유지한다. 외부 창 스타일은
`RuntimeDataRoot/Editor/Styles/Material.lxstyle`에서 Export/Reload한다.

`material.editor` HTTP는 UI와 동일한 열린 LXDocument를 조작하며 문서 ID/revision
두 값을 요구한다. typed 연결/해제·값·property·Undo/Redo·저장/Apply를 제공한다.
전용 gate는 실제 ImGui frame·정확한 파일 bytes 왕복·충돌 무변경·compile 실패 보존·
GBuffer 변화·새 Editor 재개방을 검사한다. 제품의 모든 마우스 제스처와
group/Blackboard 전용 저작 패널까지 닫은 증거는 아니므로 LX-3/LX-3H는 진행 중이다.
이 기록의 기본 창/HTTP 연결을 재작성하지 않는다. 2026-10-02 현재 공통 재질 소비는 MAT-7 재개방, UI/HTTP 마감은 LX-3/3H, 품질/성능 수용은 MAT-9로 구분한다.

| 순서 | 닫을 범위 | 선행 | 완료 증거 |
|---|---|---|---|
| `LX-0` | Blender Shader Editor 대비 Material 노드·소켓·링크·그룹·기본값·편집 동작 대응표를 만들고 `.shadergraph` 목표 schema와 Editor/패키지 소비 경계를 확정. 독립 ImGui 예제의 pinned package/backend·빌드 입력 기록 | 없음 | 지원·미지원·예정 노드/조작 목록, 목표 schema와 대표 그래프 명세, `vcpkg.json`/프로젝트 링크 목록과 예제 빌드 입력 |
| `LX-1` | 구현된 ImGui 비의존 모델/명령을 재사용하고 annotation schema·wire Reroute 삽입 편집 계약 마감 | LX-0, 기존 Core/MAT-2/6 | annotation/reroute 삽입의 저장 왕복·단일 Undo·안정 진단; 기존 타입/ID/그룹 회귀 유지 |
| `LX-2` | 독립 `LatticeExample.exe`에서 자체 `LXCanvas`와 Blender형 노드·소켓 UI, 검색, 접힘, 선택, pan/zoom, 타입별 내부 항목 구현 | LX-1 | Engine/Editor와 `imgui-node-editor` 링크 0, Debug/Release 빌드, 실제 조작·저장→재시작→재개방·DPI·resize·keyboard·다량 노드 확인 |
| `LX-3` | LX를 Editor host에 연결하고 `.shadergraph(domain=material)` 문서와 `LXMaterialCompiler`를 MAT 작업에 적용 | **LX-2 독립 게이트**, MAT-1 | Editor DX12 조작 (Vulkan 비교는 PHASE 4.9로 이관), Material graph·layout·default 저장 왕복, MAT-2/MAT-6의 typed IR·Slang·진단·마지막 정상 generation 확인 |
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
