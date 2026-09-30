# Lattice Material × Blender Shader Editor 대응표 초안

**2026-09-24 · LX-0 기준선 고정.** 비교 기준은 Blender 5.1.1 Shader Editor다.
기준 자료는 Blender의 [Shader Nodes 설명](https://docs.blender.org/manual/en/5.1/render/shader_nodes/introduction.html),
[노드 편집 동작](https://docs.blender.org/manual/en/5.1/interface/controls/nodes/editing.html),
[NodeTree API](https://docs.blender.org/api/5.1/bpy.types.NodeTree.html)다.
사용자가 제공한 Blender 화면은 외관 기준으로 사용한다.

목표는 머테리얼 저작자가 Blender에서 보는 노드·소켓·연결·값·그룹과 편집
동작을 LX에서도 거의 1:1로 다루는 것이다. 아래의 **현재**는 독립
`LatticeExample` 소스 기준이다. `목표`나 `부분`은 Editor 제품 경로의 완료
증거가 아니다. 기존 BT·Animator 자산의 변환은 이 표의 범위에 없다.

## Blender 5.1.1 실측 기준

설치된 Blender 5.1.1 (`b70da489d7f4`)을 `--background --factory-startup`으로
실행했다. [재생성 스크립트](../../Tools/LatticeExample/ExportBlenderShaderSchema.py)가
Shader Node RNA 타입과 reroute/frame/group 입출력 타입 105개를 조사했다.
103개는 `ShaderNodeTree`에 생성되며, `ShaderNodeCustomGroup`과
`ShaderNodeTree`는 이 방식으로 생성할 수 없는 기반 타입이다.
동일 설치본에서 두 번 추출한 JSON의 SHA-256은 모두
`E027E8DA5A6CD08E6D6E5A577297655D507E2FB31423C783ECCD81B0E9F3309E`였다.
[읽기용 노드 목록](../../Tools/LatticeExample/fixtures/blender-5.1.1-shader-nodes.md)에는
103개 타입의 기본 입력·출력 수와 현재 LX 제품 지원 상태를 기록했다.
[전체 소켓 JSON](../../Tools/LatticeExample/fixtures/blender-5.1.1-shader-nodes.json)은
소켓 순서·식별자·화면 이름·타입·활성/숨김 상태·기본값을 보존한다. 17개 노드에서
단일 enum 변경으로 소켓 구성이 달라졌다. enum 조합은 아직 전수 조사하지 않았다.

`ShaderNodeMix`의 화면 이름 `Factor`, `A`, `B`는 타입별로 반복되지만
`Factor_Float`, `Factor_Vector`, `A_Color`처럼 식별자는 다르다. LX 독립
예제는 표시 이름과 식별자를 분리하고 동일한 이름의 서로 다른 소켓을 저장
왕복한다. group interface 예제에서도
입력 `Base Color`와 출력 `Surface`가 별도 식별자를 받는 것을 확인했다.

Material의 첫 구현 후보는 `ShaderNodeRGB`, `ShaderNodeValue`,
`ShaderNodeTexImage`, `ShaderNodeNormalMap`, `ShaderNodeBsdfPrincipled`,
`ShaderNodeOutputMaterial`, `ShaderNodeMix`, `ShaderNodeMath`,
`ShaderNodeVectorMath`, `ShaderNodeSeparateColor`, `ShaderNodeCombineColor`,
`ShaderNodeTexCoord`, `ShaderNodeMapping`, `ShaderNodeBump`,
`ShaderNodeGroup`, `NodeGroupInput`, `NodeGroupOutput`, `NodeReroute`,
`NodeFrame`으로 고정한다. 이 후보의 제품 지원 상태도 현재는 모두 `미구현`이다.
나머지 실측 타입은 목록에 남기고 Material compiler의 의미·비용 계약을 정할 때
지원 여부를 결정한다. LX 계층은 이 판단과 관계없이 노드·소켓 구조를 보존할 수
있어야 한다.

아래 Canvas 표는 2026-09-24 조사 기록이다. 현재 native 문서/IR·생성기는 아래 MAT-2/MAT-6 절을 따른다.

| Blender 저작 개념 | LX의 현재 상태 | Material에서 필요한 계약과 판정 |
|---|---|---|
| Node tree와 node instance | `LXGraph`의 domain·node/link 목록과 안정 ID. 위치·접힘·Frame·view는 별도 `LXLayout`에 있고 `.lxg` 8에서 왕복 | `.shadergraph(domain=material)` 정본에 graph/layout 저장. Node Group과 재개방 후 안정 ID 보존 |
| Node definition | `LXNodeDefinitionRegistry`가 type/domain, 고정 pin·property, 한 종류의 동적 pin 규칙을 검증 | 노드별 입력·출력 소켓의 이름·순서·기본값·표시 조건, 생성 가능성과 버전별 schema를 정의. 등록부만으로 UI·compiler가 같은 정의를 사용 |
| Input/output socket | `Pin`의 ID·내부 identifier·화면 이름·방향·타입·multiple·dynamic·typed value. 타입은 Flow/Bool/Int/Float/Vector/Color/Normal/Texture/Surface | Material에 필요한 색 공간·vector/normal·texture/sampler·shader/closure 구분, 소켓 표시/활성 조건과 연결 수 검증 |
| Link와 연결 제스처 | `Link`는 output/input ID, LX가 타입·방향을 검증. 캔버스에서 연결·거부·빈 포트 검색 구현 | Blender 대응표에서 허용한 변환·다중 입력·cycle 규칙을 정의. 연결선 끝점·클릭 좌표가 실제 소켓 좌표와 일치 |
| 소켓 값 위젯 | `LXNodeItemRegistry`의 텍스트·float slider·color·texture preview·card. 예제 Material의 Factor·Color·Texture와 Property Editor 입력은 typed socket value에 연결 | 타입별 기본값과 UI 위젯을 소켓 정의에 결속. 연결 시 값 비활성화, 연결 해제 시 값 복원, 색 공간과 직렬화 왕복 검사 |
| Node group/interface | 독립 예제의 `LXGroupDefinition`이 내부 graph·순서 있는 입출력 interface와 안정 ID를 소유하고 `LX_GROUP` 인스턴스 핀이 참조한다. Library/Add에서 인스턴스 생성, `.lxg` 8 왕복·잘못된 interface 거부 검증. 내부 graph 초안에서 핀을 입력·출력으로 노출하고 이름·식별자·순서를 수정해 단일 Undo로 적용. 기존 인스턴스 핀·링크 유지, 새 소켓 배포·연결된 소켓 제거 거부. Group Input/Output 경계 노드·타입별 기본값 위젯을 독립 예제에서 추가. 선택 Material 노드와 그룹 인스턴스의 그룹 생성, 외부 링크·Frame·Undo·저장 왕복을 검증. 루트 정의와 중첩 복사본은 ID를 공유하며 수정과 새 소켓을 중첩 인스턴스까지 전파한다. 참조·소유 순환, 복사본 불일치와 16단계 초과를 거부한다. Material 컴파일 없음 | group 내부 graph·입출력 interface·재사용과 중첩, 안정 참조 및 순환 금지. 저장→재개방→compile 검사 |
| Reroute/frame/label | Frame의 label·경계·노드 소속·이동·삭제·저장 왕복 구현. Reroute와 annotation은 없음 | reroute 연결 유지와 annotation을 추가하고, 복사/붙여넣기·Undo/Redo를 Editor에서 검사 |
| 접힘·소켓 가시성 | 접힘 캡슐과 헤더 화살표 구현. 사용하지 않는 소켓 숨김은 없음 | 접힌 Material의 좌우 단일 소켓·화살표·제목을 중앙 Y축에 정렬. 다중 소켓의 위치·hit test·wire 일치. 미사용 소켓 숨김과 연결 보존 검사 |
| 검색·선택·이동 | 예제에서 검색·다중 선택·이동·복사/붙여넣기·pan/zoom 구현 | Blender 대응 제스처 목록을 명시하고 독립 예제와 Editor에서 키보드·DPI·Undo/Redo로 확인 |
| Shader node 목록 | 예제 Material 노드 6종과 별도 Normal Texture 변형. 실제 Material compiler 없음 | Blender 5.1.1의 지원 대상 노드를 분류별로 열거하고 각 노드의 socket·기본값·미리보기·lowering 상태를 `지원/예정/미지원`으로 표시. 지원으로 표시한 노드는 제품 fixture 필수 |
| Material Output·평가 | 예제 `Principled Surface`는 Base Color·Normal 연결 데모 | MAT 계획의 `PrincipledSurface` ABI, typed IR, deterministic Slang, 진단·source mapping, 마지막 정상 generation과 preview/scene 일치 검증 |

편집 동작의 기준 상태도 분리한다. `지원`은 독립 예제 기준이며 Editor 제품 지원을
뜻하지 않는다.

| 편집 동작 | 독립 예제 | 제품 목표 |
|---|---|---|
| 검색·노드 추가, 소켓 연결·거부·빈 곳 연결 검색 | 지원 | Material 정의 전체에 같은 제스처 적용 |
| 다중 선택·이동·복사/붙여넣기·Undo/Redo·pan/zoom | 지원 | Editor에서 DPI·키보드·저장 왕복 확인 |
| 헤더 접힘과 연결 유지 | 지원 | 소켓 숨김·표시 규칙과 함께 검증 |
| 미연결 소켓 숨김·mute | 미구현 | 정의/문서 명령에 상태 보존과 평가 규칙 추가 |
| frame 배치·view 저장 | 독립 예제 지원 | `.shadergraph` 정본과 Editor에서 저장·DPI 복원 확인 |
| group/interface, reroute | group 정의·인스턴스·저장 왕복과 내부 graph·입출력 interface 편집은 독립 예제에서 부분 지원. Group Input/Output 경계·기본값 위젯과 선택 노드의 그룹 생성 UI를 지원. 중첩 복사본은 루트 정의의 수정에 동기화되며 참조 순환을 거부한다. Material 컴파일과 reroute는 미구현 | 새 graph/layout 계약으로 저작·저장·compile 확인 |
| node별 enum에 따른 소켓 활성 전환 | 미구현 | 내부 identifier와 이전 링크·기본값을 보존하며 변경 |

## 새 Material 문서의 목표 계약

`.shadergraph` 첫 형식은 `domain=material`과 schema version을 기록한다.
`graph`에는 안정 node·socket·link ID, node definition ID와 schema revision,
순서가 있는 socket 목록, 각 socket의 내부 identifier·표시 이름·방향·타입·
활성/숨김 상태·연결 수·typed default value를 둔다. 이미지·컬러 공간과 노드별
enum 설정은 node 속성으로 보존한다. `layout`에는 node 위치·접힘, frame 소속,
reroute 위치와 view 상태를 둔다. 화면 위치는 Material 컴파일 입력이 아니다.

group은 자체 graph ID와 안정 interface socket ID를 갖는다. group instance의
socket은 interface ID에 대응하며 중첩 참조의 cycle을 검사한다. reroute는 값
전달 의미를 갖고 frame은 배치 정보만 갖는다. 저장된 미등록 node는 원자료를
보존해 읽기 전용으로 표시하고, 알려진 node의 schema 불일치는 진단한다.
소켓 기본값은 bool/int/float, vector, 색 공간 의도가 있는 color, texture 자산
참조, surface/closure 등 타입에 맞는 값으로 기록한다. 현재 Core의 bool/int/
float/vector/color/texture typed value에 MAT-2의 Sampler/Closure·색 공간·Blackboard를
추가했고 `.shadergraph` 저장과 typed IR을 독립 예제에서 검증했다.
제품 UI 위젯 결속은 남아 있다. 초기 지원 Slang 생성은 아래 MAT-6 절을 따른다. 문자열 property는 Material 정본의 typed
default value를 대신하지 못한다.

위 표의 캔버스 대응은 독립 UI 기준이다. 2026-09-28 MAT-6에서 아래 native compiler
지원 범위를 추가했으며 Editor 제품 지원 상태와 구분한다.

대표 graph 계약은 다음 다섯 경우로 고정한다.

| 경우 | 필요한 node와 연결 | 왕복·검증 관점 |
|---|---|---|
| 상수 재질 | RGB/Value → Principled BSDF → Material Output | 소켓 기본값·색 공간·active output |
| 기본 텍스처 | Image Texture → Mix의 타입별 Color 소켓 → Principled Base Color | 같은 화면 이름의 다른 소켓 identifier, 이미지 자산 참조 |
| 노멀 | Image Texture → Normal Map → Principled Normal | normal 타입과 strength 기본값 |
| 재사용 | Group Input → Mix → Group Output, 바깥 graph의 group instance | interface ID·내부 graph·중첩 참조 |
| 배치 | 위 graph에 reroute와 frame, 노드 접힘 | link endpoint와 layout 보존, 컴파일 의미 분리 |

## 독립 예제와 Editor 경계

독립 예제는 ImGui 1.92.8의 Win32·DX11 backend와 `imgui.lib`/`imguid.lib`,
시스템 `d3d11.lib`·`dxgi.lib`를 사용한다. VS18/v145 x64 프로젝트는
`Lattice/Core`·`Lattice/Material`·`Lattice/ImGui`를 컴파일하며 Engine/Editor나
`imgui-node-editor`를 링크하지 않는다. 현재 Editor는 BT의
[`MenuBarWindow.cpp`](../../Editor/EngineGUIWindow/MenuBarWindow.cpp)와 Animator의
[`AnimatorEditorWindows.cpp`](../../Editor/EngineGUIWindow/AnimatorEditorWindows.cpp)가
node-editor를 사용한다. [`vcpkg.json`](../../vcpkg.json)의 직접 의존은 이 두
활성 UI 소비자를 새 창으로 교체한 뒤 제거한다.

## 구현 단계에서 이어받을 조사

- 첫 후보 밖의 84개 타입에 제품 지원 범위를 정하고, 각 enum **조합**에서
  달라지는 소켓을 노드 구현 시 검증한다. 현재 목록의 전 항목은 제품 Material
  compiler 기준 `미구현`이다.
- MAT-2는 초기 6개 Blender 정의와 Parameter/Reroute, 상수·image/normal·그룹·배치의
  `.shadergraph` 왕복과 typed IR을 독립 Debug/Release에서 검증했다.
  [MaterialGraphSchema.md](MaterialGraphSchema.md)를 따른다. Mix와 전체 노드/enum
  조합과 Editor UI·나머지 Slang operator는 별도 지원/제품 게이트에서 구현한다. 예제 `.lxg`를 제품 정본으로 취급하지 않는다.

## MAT-6 native 생성기 지원 (2026-09-28)

[MaterialSlangCodegen.md](MaterialSlangCodegen.md)가 실제 lowering·색 공간·진단·비용 계약을 소유한다.
초기 RGB/Value, Image Texture, Normal Map, Principled BSDF, Material Output의 지원 설정을
Slang으로 낮추고 실제 texture/sampler GPU와 DXIL/SPIR-V로 검증했다. 모든 Blender enum을
지원한다는 뜻은 아니다. 미지원 설정은 소유 node/socket/property를 진단한다.

공유/중첩 Group Input/Output과 인스턴스 입력 override, typed parameter/reroute·Closure reroute를
해석한다. 엔진의 `LXMultiply*`·`LXTextureSample`·6입력 `LXPrincipledVolume`을 추가했다.
이 정의를 Blender Math/Mix/full Volume의 1:1 구현으로 표시하지 않는다.
Editor의 위젯/preview·HTTP 및 제품 ShaderMeta/Scene binding은 LX-3/LX-3H·MAT-7/MAT-8에 남는다.
