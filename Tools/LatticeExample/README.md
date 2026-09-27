# Lattice 독립 ImGui 예제

LX 그래프 모델(`Lattice/Core`)과 자체 ImGui 캔버스(`Lattice/ImGui`)를
Editor/Engine 프로젝트 없이 실행하는 Win32·DX11 예제다. 저장소의 ImGui
1.92.8 헤더와 정적 라이브러리를 사용한다. `imgui-node-editor`를 include하거나
링크하지 않는다. 예제의 `.lxg`는 조작 검증용 형식이며 PHASE 4.25의 최종
`.shadergraph` 자산 형식이 아니다.
Debug/Release 빌드의 중간 산출물은 구성별 디렉터리에 분리된다.

`LXNodeDefinitionRegistry`는 예제의 material/behavior 노드 정의와 도메인별
허용 핀을 등록한다. 새 노드는 정의의 고정 핀·동적 핀 규칙으로 검사되며,
저장 파일의 미등록 노드는 경고와 함께 원래 값을 유지하고 데이터 편집을 막는다.
정의가 있는 노드의 도메인 또는 핀 schema가 다르면 재개방을 거부한다.
자체 검사는 이 동작과 알 수 없는 노드의 저장→재개방, 잘못된 붙여넣기의
무변경 거부를 확인한다.

ImGui와 독립인 `LXDocument`는 그래프 조회, 문서 ID·revision, 타입이 있는
명령과 dirty 상태를 소유한다. 독립 검사는 낡은 revision의 무변경 거부,
노드/핀/링크 수정, 복사·붙여넣기, Undo/Redo, Validate, Save·재개방을
확인한다. Material 예제 UI의 그래프 변경·Undo/Redo·Save/Reload는 이 문서
계층을 사용한다. 노드 드래그는 미리보기 후 놓을 때 한 명령으로 확정한다.
Behavior 예제는 아직 직접 그래프를 조작한다. HTTP endpoint는 아직 없다.

## 빌드와 자체 검사

VS18/v145 환경에서 저장소 루트 기준:

```powershell
$msbuild = 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe'
& $msbuild Tools\LatticeExample\LatticeExample.vcxproj /p:Configuration=Release /p:Platform=x64 /m /nologo /v:minimal
& $msbuild Tools\LatticeExample\LatticeExample.vcxproj /p:Configuration=Debug /p:Platform=x64 /m /nologo /v:minimal
& Build\LatticeExample\LatticeExample.exe --self-test
& Build\LatticeExample\Debug\LatticeExample.exe --self-test
```

예제 UI는 `Build\LatticeExample\LatticeExample.exe <문서 경로>`로 실행한다.
경로를 생략하면 실행 파일 폴더의 `LatticeExample.lxg`를 사용한다.
`<문서 경로>`가 아직 없으면 예제 그래프가 열린다. Save 후 같은 경로로 다시
실행하면 저장된 ID·노드 위치·링크·속성을 읽는다. ImGui UI 시안은
[실제 DX11 렌더 결과](../../docs/design/LatticeImGuiPreview.png)로 확인할 수 있다.
[Animation FSM 스타일 시안](../../docs/design/LatticeAnimationFSMPreview.png)은
기존 Animator 창의 상태 이름·전이 화살표 표현을 LX로 그린 결과다.
화면에 창을 띄우지 않고 같은 결과를 새로 만들려면 다음 명령을 사용한다:

```powershell
& Build\LatticeExample\LatticeExample.exe --capture Build\LatticeExample\LatticePreview.png
& Build\LatticeExample\LatticeExample.exe --capture-collapsed Build\LatticeExample\LatticeCollapsed.png
& Build\LatticeExample\LatticeExample.exe --capture-frame Build\LatticeExample\LatticeFrame.png
& Build\LatticeExample\LatticeExample.exe --capture-group Build\LatticeExample\LatticeGroup.png
& Build\LatticeExample\LatticeExample.exe --capture-group-editor Build\LatticeExample\LatticeGroupEditor.png
& Build\LatticeExample\LatticeExample.exe --capture-bt Build\LatticeExample\LatticeBehavior.png
& Build\LatticeExample\LatticeExample.exe --capture-fsm Build\LatticeExample\LatticeAnimationFSM.png
```

이 모드는 숨긴 Win32 창에 ImGui 프레임 한 장을 그린 뒤 DX11 back buffer를
1440×840 PNG로 저장하며, 마우스와 기존 창 포커스를 조작하지 않는다.
실행 중인 보이는 창 자체를 캡처해야 한다면
`Capture-LatticeExample.ps1`은 DWM의 **실제로 보이는 창 경계**를 사용해
지정 파일로 저장한다. 이 스크립트는 마우스나 창 포커스를 조작하지 않는다.

선택 노드의 Property Editor에서 노드 색상·크기, 핀 색상·모양·크기와
타입별·개별 연결선 색상·굵기·굴곡을 바꿀 수 있다. 입력 핀은 이름 붙은 입력 행의 왼쪽,
출력 핀은 출력 행의 오른쪽에 고정된다. 같은 `PinPosition` 계산을 그리기,
hit test, 연결선 끝점에서 사용한다. 스타일은 그래프의 실행 의미와 분리한
`LXStyleSheet`에 있다. Frame의 채우기·헤더·테두리·선택 테두리·문자 색과
헤더 높이도 같은 스타일에서 설정한다. 그래프와 스타일은 각각 저장한다.
`Save Window Style`은
`<문서 폴더>/LatticeStyles/<창 종류>.<문서명>.lxstyle` 외부 파일을 기록한다.
Material Graph·Behavior Tree·Animation FSM은 서로 다른 파일과 캔버스 상태를 사용한다.
Reload와 재시작 시 해당 창의 스타일 파일을 읽으며, 파일이 없으면 Blender 계열
기본 스타일을 쓴다. 노드 높이의 기본 최소값은 0이며 실제 핀 행과 내부 항목의
높이로 결정된다.
기본 스타일은 제공된 Blender 노드 편집기 이미지를 참고해 어두운 회색 패널,
작은 타입별 색상 소켓, 낮은 단색 헤더, 가는 회색 연결선과 주황색 선택 테두리를
쓴다. 캔버스 위의 View·Add는 실제 격자 전환·노드 추가 메뉴다. 기존
`.lxstyle`을 연 문서는 저장된 사용자 설정을 유지하며, 선택 노드 패널의
`Blender preset` 버튼으로 새 기본 스타일을 적용할 수 있다. 왼쪽 헤더 화살표를
누르면 노드는 연결 핀을 유지한 작은 캡슐로 접힌다. 접힌 Material 노드에서
좌우 핀이 하나씩이면 핀·화살표·제목을 같은 중앙 Y축에 맞춘다. 접힌 높이를
제목의 클립 영역과 화살표 클릭 영역에도 적용한다. `--capture-collapsed`는
Normal Map을 접은 화면을 만든다. 접힘 상태는 Undo/Redo와 `.lxg`
저장·재개방에 포함된다.

Animation FSM 시안은 기존 Animator 창의 상태 이름만 있는 노드와 방향 화살표가
있는 전이를 기준으로 한다. `FSM_STATE`와 `FSM_ANY`의 `headerOnly` 스타일은
그래프의 접힘 플래그를 바꾸지 않고 항상 제목만 있는 캡슐로 그린다. 펼침
화살표와 내부 항목은 표시하지 않는다. Flow 핀은 상태 외곽의 원형 포트로
표시한다. 연결된 포트는 상대 상태를 향한 왼쪽 또는 오른쪽 캡슐 외곽에 놓는다.
같은 측면의 전이가 여러 개면 세로로 나누고, 기본 입력·출력 위치가 비어
있으면 중앙 포트를 남긴다. 각 포트를 클릭해 연결할 수 있다. 전이선은 포트에서
짧게 수평으로
나온 뒤 상대 포트까지 직접 이어지고 가운데 방향 화살표를 그린다.
가까운 높이의 포트는 같은 Y축으로 맞춰 수평 직선으로 잇는다. 역방향
전이도 같은 규칙을 사용한다. animation
도메인의 Flow 전이는 상태로 되돌아오는 cycle을 허용하고 데이터 링크는 cycle을
거부한다. `AnimationFSM` 외부 스타일
파일에서 색·크기·핀 표시·선과 화살표를 바꿀 수 있다. 이 예제의 `.lxg`는
FSM 표현 검증용이며 기존 AnimationController 자산·런타임과 연결하지 않았다.

Ctrl을 누른 채 노드를 클릭하면 다중 선택한다. 선택한 노드는 함께 이동하거나
Delete로 함께 지울 수 있으며 각각 한 번의 Undo로 복구된다. Ctrl+C/V는 선택한
노드와 그 사이의 링크를 새 ID로 복제하며 붙여넣기 한 번이 Undo 한 번이다.
빈 공간으로 핀을 끌면 호환 노드만 검색하는 메뉴가 열리고, 노드 생성과 연결도
Undo 한 번으로 복구된다. Multiply에는
최대 4개의 Color 입력 핀을 추가·순서 변경·삭제할 수 있다. 동적 핀 규칙,
핀 ID·순서·식별자·타입별 기본값과 연결, 별도 `LXLayout`의 위치·접힘은
`.lxg` 8 형식으로 왕복한다. 기존 `.lxg` 1~7 형식도 읽으며 저장 시 8
형식으로 올린다.

Material의 Add 메뉴에서 선택 노드 둘레에 Frame을 만든다. Frame 헤더를 끌면
소속 노드가 함께 움직이고, 선택한 Frame은 Inspector에서 이름·크기·색상을
바꾸거나 Delete로 제거한다. Frame의 영역·
소속과 문서별 화면 중심·확대율은 `LXLayout`에 저장된다. 화면 이동은 저장되지만
노드 편집 Undo 순서를 차지하지 않는다. DPI가 달라도 그래프 중심을 기준으로
화면 위치를 복원한다. 외부 `.lxstyle`은 Frame 및 header-only·핀 표시·전이
화살표 설정을 포함한 5형식이며 기존 1~4형식도 읽는다. Frame은 배치 정보이고,
재사용 가능한 Node Group/interface는 Frame과 별도 의미 모델이다. 그룹 정의는
Material 도메인의 내부 `LXGraph`와 순서 있는 입출력 소켓, 내부 핀 참조와 안정
interface ID를 소유한다. 인스턴스 핀은 이 ID에 대응한다. 예제의 Library와
Add 메뉴에서 그룹 인스턴스를 추가할 수 있다. `.lxg` 8은 정의·내부 그래프·
인스턴스·링크를 함께 저장한다. 인스턴스를 선택해 `Edit Group`을 누르면 내부
그래프 초안을 편집한다. 노드 추가·이동·연결·값 수정 후 `Apply Group`으로 부모
문서에 한 번의 Undo 변경을 확정하거나 `Cancel`로 버린다. 왼쪽 Interface에서
내부 핀을 입력·출력으로 노출하고 표시 이름·안정 identifier·내부 핀 대응·순서를
바꾸거나 소켓을 제거할 수 있다. 기존 소켓 ID와 인스턴스 핀 ID는 유지하고,
새 소켓에는 ID를 발급해 모든 인스턴스에 추가한다. 외부 링크가 붙은 소켓 제거·
방향/타입 변경과 노출된 내부 핀 삭제는 적용을 거부한다. 그룹 화면은 Material 창의 기본·타입별
스타일을 따르며, 문서 루트와 겹칠 수 있는 개별 ID 스타일은 적용하지 않는다.
`LX_GROUP_INPUT`/`LX_GROUP_OUTPUT` 경계 노드를 사용하는 그룹에서는 외부 입력을
내부 출력 핀에, 외부 출력을 내부 입력 핀에 대응시킨다. 두 노드 사이의 연결은
일반 내부 링크다. Interface 패널에서 타입별 기본값을 지정하고 경계 소켓을
추가·제거·이름·순서 변경할 수 있다. 기존 직접 핀 대응 그룹도 유지한다.
Material 캔버스에서 노드를 선택한 뒤 `Group selection` 또는 `Ctrl+G`로 새
그룹을 만든다. 선택 내부의 링크는 그룹 본문으로 옮기고, 외부와 연결된 핀은
Group Input/Output 경계 소켓으로 바꾼다. 연결되지 않은 입력·출력도 interface에
노출한다. 외부 링크 ID는 중복 fanout 링크를 하나로 통합하는 경우를 제외하고
유지한다. 선택 노드의 Frame 배치도 유지하고 생성 전체를
문서 명령 한 번과 Undo 한 번으로 처리한다. 독립 검사는 공유 외부 입력의 fanout,
잘못된 선택의 무변경 거부, Undo/Redo, 저장·재개방, 숨긴 ImGui 버튼 입력을
확인한다. 선택에 그룹 인스턴스가 있으면 해당 정의를 새 그룹의 내부 그래프에
포함해 중첩한다. 그룹 편집 화면에서도 내부 그룹을 추가하거나 선택 노드를 다시
그룹으로 묶을 수 있다. 문서 루트에 있는 정의와 중첩 복사본은 같은 정의·interface
ID를 사용하며, 루트 정의의 수정은 중첩 인스턴스까지 한 문서 변경으로 전파된다.
저장과 검증은 서로 다른 복사본, 정의 참조 순환, 소유 그래프 순환 및 16단계
초과를 거부한다. 그룹은 여전히 내부 그래프에 직렬화된 복사본을 보관하며,
Material 평가·컴파일은 아직 없다.
노드 복사본은 원본과 대상 Frame의 ID와 배치가 일치할 때만 소속을 유지한다.
다른 문서에 같은 숫자 ID의 Frame이 있어도 배치가 다르면 그 Frame에 붙지 않는다.

Behavior Tree 예제는 같은 캔버스에서 위아래 핀과 아래 방향 직선 화살표, 중성 회색
외곽 노드, 색상별 내부 카드를 쓴다. `LXNodeItemKind::Card`와 역할별
`LXItemStyle`로 장식자·서비스·작업 카드의 색과 텍스트를 바꿀 수 있다.
이것은 표현 fixture이며 기존 `.bt` 파일/BT 런타임 adapter는 아직 연결하지 않았다.

노드 내부 항목은 문자열 한 줄로 제한하지 않는다. `LXNodeItemRegistry`가 노드 타입과
속성 키에 따라 텍스트, 숫자 슬라이더, 색상 선택기, 텍스처 미리보기를 지정한다.
Multiply의 Factor 슬라이더는 Factor 입력 핀과 **같은 행**에 있다. 해당 핀이
연결되면 기본값 슬라이더 대신 `Linked`를 표시한다. Tint Color의 색상 버튼은
RGB·색조 선택기를 열며 Apply 시 그래프 값과 Undo 이력을 한 번만 갱신한다.
슬라이더도 드래그가 끝날 때 값과 Undo 이력을 갱신한다.

텍스처 미리보기는 `SetTextureResolver`로 도메인의 실제 `ImTextureID`를 받는다.
현재 독립 예제에는 실제 텍스처 자산 로더가 없으므로 `T_BaseColor`와 `T_Normal`은
서로 구별되는 **예시 썸네일**로 표시한다. 이를 실제 자산 샘플이라고 해석하면 안 된다.
속성 저장값은 현재 예제의 `.lxg` 안에서 문자열이며, 항목 종류와 표시 범위는
편집 UI 계층에 있다.

코드 포맷은 저장소의 `.clang-format`과 VS18에 포함된 clang-format을 따른다:

```powershell
$formatter = 'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin\clang-format.exe'
& $formatter --dry-run --Werror Lattice\Core\LXGraph.h Lattice\Core\LXGraph.cpp Lattice\Core\LXNodeDefinition.h Lattice\Core\LXNodeDefinition.cpp Lattice\Core\LXDocument.h Lattice\Core\LXDocument.cpp Lattice\ImGui\LXCanvas.h Lattice\ImGui\LXCanvas.cpp Lattice\ImGui\LXNodeItems.h Lattice\ImGui\LXStyle.h Lattice\ImGui\LXStyle.cpp Tools\LatticeExample\main.cpp
```

## 2026-09-24 조작 검증

- Debug·Release 독립 빌드와 양쪽 `--self-test` 통과. 타입 불일치, 자기 연결,
  순환 연결, Undo/Redo, 저장·재개방, 핀 행 위치, 스타일 우선순위·sidecar 왕복을
  검사한다.
- Animation FSM 예제의 제목 전용 상태, 숨긴 핀의 클릭 연결, 양방향 전이와
  가운데 화살표 스타일, 그래프·창별 스타일 저장 왕복 및 이전 LXS 4 읽기를 검사한다.
- 숨긴 ImGui 입력 이벤트로 핀 클릭 연결·타입 거부·핀에서 목표 핀까지 드래그
  연결, 노드 이동·Ctrl+Z/Y, Ctrl 다중 선택·묶음 이동·묶음 삭제와 각각의 단일
  Undo, 휠 확대·중간 버튼 pan·Delete를 100%·150% 배율에서 검사한다. Factor
  슬라이더와 색상 버튼·헤더 접힘도 같은 방식으로 확인한다.
- Library 검색창에 `normal`을 입력해 목록이 두 항목으로 좁혀지고 Normal Map
  클릭으로 실제 노드가 추가되는지 검사한다. 동적 핀 UI의 Add/Remove 클릭,
  개수 상한·중복 이름 거부·순서 변경·연결 제거·Undo·저장 왕복과 `.lxg` 2
  형식의 재개방·8 형식 저장을 검사한다.
- Ctrl+C/V로 선택 노드 둘과 그 사이 링크를 새 ID로 복제하고 한 번의 Undo로
  되돌리는지 검사한다. 핀을 빈 공간에 끌어 호환 노드 검색·추가·자동 연결을
  수행하고 한 번의 Undo/Redo로 왕복하는지 검사한다. 중복 입력 이름 진단을
  클릭하면 해당 노드가 선택되고 캔버스가 이동하는지도 확인한다.
- 250개 추가 노드를 포함해 800×600부터 2160×1260까지 여러 크기로 전체 UI를
  그린다. 숨긴 Win32 창을 연결된 각 모니터 안에 만들고 해당 창의 실제 DPI와
  client 크기·모니터 소속을 읽은 뒤 크기 변경과 그 DPI에서의 조작을 확인한다.
  이번 실행에서는 두 모니터 모두 144 DPI, 숨긴 창 client는 1350×960이었다.
  이 창은 표시하거나 활성화하지 않으며 마우스·현재 작업 창 포커스를 바꾸지 않는다.
- `.lxg`·`.lxstyle`의 모든 필드가 동일한 왕복, 한글 경로, 별도 창 스타일,
  임시 파일 검증 후 교체, 정상 파일 백업과 손상된 주 파일의 백업 복구를 검사한다.
- Blender Mix처럼 화면 이름이 반복되는 핀도 서로 다른 내부 식별자를 등록하고
  저장·재개방한다. bool/int/float/vector/color/texture 값의 타입 검사,
  연결된 입력의 수정 거부, Undo/Redo와 저장 왕복을 검사한다. Material 예제의
  Factor 슬라이더·Tint Color 선택기·텍스처 미리보기는 해당 핀의 typed 값을
  사용한다. Property Editor에는 bool/int/float/vector/normal/color/texture
  입력을 연결했다. 예전 예제 파일의 문자열 property는 값이 없는 핀의 표시
  대체값으로 읽는다. 슬라이더 조작→문서 명령→저장·재개방은 자동 검사했다.
  Property Editor는 한 노드의 모든 등록 항목을 표시한다. 숨긴 ImGui 입력으로
  bool/int/float/vector/normal/color/texture 항목을 각각 수정하고, 일곱 값의
  문서 명령·저장·재개방 왕복을 확인했다.
- `LXLayout`은 노드의 위치·접힘을 의미 노드 레코드에서 분리한다. `.lxg` 5의
  배치를 6으로 올려 저장·재개방하고, 7에서는 Frame의 경계·노드 소속과
  DPI 독립 화면 중심·확대율을 저장한다. 배치 누락 파일은 거부한다.
  선택/복사/붙여넣기, 이동·접힘 Undo/Redo와 핀/연결선 위치는 같은 배치를
  사용한다. Frame 생성·미리보기 이동·삭제·소속 해제와 100%/150% DPI 화면
  복원, 창 크기·DPI 변경 후 중심 유지 및 pan/zoom 저장을 숨긴 ImGui 입력과
  저장 왕복으로 검사했다.
- `.lxg` 8의 Node Group 정의·내부 그래프·안정 interface ID와 인스턴스 핀·
  외부 링크를 저장→재개방해 전체 값이 동일한지 검사했다. 잘못된 interface,
  없는 그룹 및 손상된 저장 파일을 거부하고 Undo/Redo·같은 문서
  복사/붙여넣기를 확인했다. Library 항목 클릭으로 인스턴스 생성과 revision 증가를
  숨긴 ImGui 입력으로 검증했다. `.lxg` 7 파일의 재저장 결과가 8인지도 확인했다.
- 그룹 인스턴스를 선택해 다시 그룹화한 뒤 내부 정의·핀 대응·외부 링크·
  단일 Undo/Redo와 저장→재개방을 검사했다. 그룹 편집 화면에서 내부 그룹 추가와
  재그룹화·적용을 숨긴 ImGui 입력으로 확인했다. 루트 정의의 이름·본문·interface
  수정과 새 소켓을 중첩 복사본·인스턴스에 전파하고 Undo/Redo·저장 왕복을 검사했다.
  어긋난 복사본의 저장, 중첩 연결 소켓의 제거, 정의 참조 순환, 소유 순환과
  16단계 초과 생성은 무변경 거부한다.
- 그룹 초안을 바꾸고 `Apply Group`을 눌러 부모 문서의 revision·dirty가 한 번만
  바뀌는지 검사했다. 두 인스턴스의 interface ID와 외부 링크가 유지되고 Undo/Redo·
  저장→재개방 뒤 내부 그래프 배치가 동일한지 확인했다. 숨긴 ImGui 입력으로
  `Edit Group` 진입, 적용, 잘못된 초안의 적용 거부와 취소를 확인했다. 그룹 정의만
  있는 새 문서도 dirty로 표시한다.
- 개발자가 지정한 interface 입력·출력의 이름·identifier·순서·내부 핀 대응을
  `LXUpdateGroup`으로 갱신하고, 새 입력을 두 인스턴스에 추가하며 기존 핀 ID와
  링크가 유지되는지 확인했다. 연결된 소켓 제거는 무변경 거부하고, 연결되지 않은
  소켓 제거와 Undo/Redo·저장 재개방을 검사했다. 편집 화면에서 내부 `Factor`
  핀을 입력으로, `Tint Color / Color` 핀을 출력으로 노출하는 클릭과 출력 추가 뒤
  저장·재개방을 숨긴 ImGui 입력으로 확인했다.
- Group Input/Output 경계 노드의 반대 방향 핀과 내부 링크, 타입별 기본값 변경,
  인스턴스의 개별 값 유지, 경계 핀·인스턴스 핀 순서 변경, 연결 없는 소켓 제거,
  Undo/Redo·저장 재개방을 검사했다. 숨긴 ImGui 입력으로 경계 소켓 입력·출력
  추가와 Color 기본값 지정→부모 문서 적용→저장 재개방을 확인했다.
- Material UI의 연결·거부·드래그·Undo/Redo·다중 선택·삭제·복사/붙여넣기와
  Library 노드 생성은 `LXDocument`를 실제로 연결한 숨긴 ImGui 입력 검사도
  통과했다. 유효한 변경만 revision을 올리고, 드래그 미리보기는 revision을
  유지하다 놓을 때 한 번 올린다.
- 숨긴 창의 DX11 back buffer를 일반·접힘·Behavior Tree PNG로 각각 저장하고
  모두 1440×840 픽셀인지 확인했다.
- Release 창 실행과 DPI 150% 환경의 보이는 창 경계 캡처 확인.
- 실제 UI에서 노드 이동·Save·재시작·재개방 시 좌표 `(70, 35) → (100, 75)` 보존.
  타입이 다른 포트 연결은 `Pin types differ`로 거부되며 링크 수는 0을 유지했다.
  호환 포트 연결 후 저장 파일에는 링크 1개가 기록됐다.

위의 실제 UI 마우스 조작 증거는 스타일 변경 전 버전에서 수집했다. 현재 스타일의
다중 선택·진단 이동 등은 **숨긴 ImGui 입력 시뮬레이션**으로 확인했다.
LX-1의 definition registry·domain 검증·unknown
node 보존, 별도 `LXLayout`·공용 문서 명령 및 Material 예제 UI 연결은 자체 검사를
통과했다. 기존 BT·Animator 자산 변환과 fixture는 범위에서 제외했다.

## 2026-09-27 보이는 창 조작 검증

- Release 예제를 실제 창으로 열고 크기를 변경했다. Blender 스타일 Material
  그래프에서 노드 헤더 드래그, 접힌 Base Color Texture의 핀·연결선 위치,
  Library 검색과 노드 생성, 핀 클릭 연결 및 핀 드래그 연결을 확인했다.
  Texture→Color 연결은 `Pin types differ`로 거부되며 링크 수가 유지됐다.
  Color→Color 연결은 링크 수를 6→7로 늘렸다. Library에서 추가한 Multiply는
  기본 fixture의 Tint Color와 일부 겹쳐 생성됐으므로 배치 개선은 남아 있다.
- 새 Multiply를 이동한 뒤 `Ctrl+Z`로 이전 위치, `Ctrl+Y`로 이동 위치를
  복구했다. 그래프와 Material 창 스타일을 각각 UI 버튼으로 저장했다.
  프로세스를 종료하고 다시 실행했을 때 접힌 상태·노드 8개·링크 7개·새 노드
  위치가 복원됐다. 외부 스타일 파일도 존재했다. 검사 파일은
  `Build/LatticeExample/verification-20260927/`에 보관했다.
- 핀을 빈 공간으로 끌자 호환 노드 검색 메뉴가 열렸다. Multiply를 선택하면
  노드가 생성되고 원래 Color 핀과 자동 연결돼 노드 7→8개, 링크 6→7개가 됐다.
  선택 노드의 `Ctrl+C`·`Ctrl+V`는 복사본을 만들고, `Ctrl+Z` 한 번으로
  붙여넣기를 되돌렸다.
- Material 창에서 격자를 끄고 `Save Window Style`을 누른 뒤 프로세스를
  재실행해 격자가 꺼진 상태로 복원되는 것을 확인했다. 해당 외부 스타일 파일은
  검사 폴더로 옮겨 기본 예제 화면은 원래 격자 설정을 사용한다.
- Animation FSM 창에서 Idle↔Move의 가까운 포트가 수평 직선으로 이어지고,
  Move→Attack 및 Attack→Idle 전이가 상태 외곽 원형 포트에 붙는 것을
  보이는 화면으로 확인했다.
- 경로 인자 없이 Computer Use로 실행하면 시작 작업 폴더가 쓰기 불가능해
  `Save`가 `Cannot open output file`로 실패했다. 기본 문서 경로를 실행 파일
  폴더로 고친 뒤 같은 UI 조작으로 저장·재시작·재개방에 성공했다. 수정본의
  Debug/Release 빌드, 양쪽 `--self-test`와 clang-format 검사를 통과했다.

## 2026-09-28 남은 조작 검증

- 정의가 없는 노드 하나를 포함한 검사 그래프를 실제 창으로 열었다. 오른쪽
  `Problems 1`의 `unknown_node` 항목을 클릭하자 해당 Base Color Texture가
  선택되고 캔버스 중심 쪽으로 이동했으며, 왼쪽 Property Editor에도 해당
  노드의 읽기 전용 상태가 표시됐다. 검사 그래프는
  `Build/LatticeExample/verification-20260927/UnknownNodeNavigation.lxg`에
  보관했다.
- Library 노드의 고정 오프셋 배치를 화면 안의 빈 공간 탐색으로 바꿨다.
  좁은 실제 창에서 Multiply를 두 번 연속 추가했을 때 첫 노드는 기존 노드와
  겹치지 않았고, 두 번째는 빈 공간에 생성되면서 화면이 이동해 새 노드가
  보였다. 그래프를 저장하고 프로세스를 재실행했을 때 노드 9개·링크 6개와
  이동한 화면 위치가 복원됐다. 검사 그래프는
  `Build/LatticeExample/verification-20260928/LibraryPlacement.lxg`에 보관했다.
- 마지막 수정 뒤 Debug/Release 빌드, 양쪽 `--self-test`와 clang-format
  검사를 통과했다.

Ctrl+클릭 다중 선택과 선택한 노드들의 동시 이동은 사용자가 실제 창에서
직접 조작해 정상 동작을 확인했다. 별도로 숨긴 ImGui 제스처 자체 검사도
통과했다. LX-2 독립 예제 게이트는 통과했으며, LX-1의 남은 기능과 Editor
제품 연결은 별도 작업이다.
