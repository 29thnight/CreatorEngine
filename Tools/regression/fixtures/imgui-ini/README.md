# `imgui.ini` fixture — 실물 넷과 합성 하나

PHASE 21 W0 후반. 계획서 §1.4와 W3의 legacy 이주가 쓰는 코퍼스다.

## 왜 저장소에 넣는가

**다섯 중 넷은 실물이고, 그 넷이 전부 git 추적 밖이었다.** `Bin/`은 `.gitignore:94`가 막고
있고, 저장소 루트의 `./imgui.ini`와 `Dynamic_CPP/Assets/Scenes/imgui.ini`도 추적되지 않는다.
즉 W3이 쓸 이주 fixture가 **한 기계의 디스크에만** 있었고, 빌드 폴더를 지우는 순간 사라진다.

합성 fixture로 대신할 수 없다. 이 넷의 값어치는 **손으로는 못 만드는 분열**에 있다 —
Content Browser 항목이 아이콘 뒤 공백 2와 1로 갈려 **같은 창이 두 줄**로 남은 상태(§1.4)가
셋에 실려 있다. 그 모양이 실제로 어떤 도크 노드에 붙고 어느 쪽이 떠도는지는 실물만 안다.

## 다섯 벌

| 파일 | 출처 | 채집 | 창 | Content Browser |
|---|---|---:|---:|---|
| `current-debug.ini` | `Bin/x64-Debug/Editor/Saved/Config/` | 2026-09-11 | 16 | 한 줄(공백 1) |
| `legacy-release.ini` | `Bin/x64-Release/Editor/Saved/Config/` | 2026-09-10 | 13 | **두 줄**(공백 2·1) |
| `legacy-repo-root.ini` | 저장소 루트 `./imgui.ini` | 2026-08-09 | 15 | **두 줄** |
| `legacy-scenes-cwd.ini` | `Dynamic_CPP/Assets/Scenes/` | 2026-08-07 | 15 | **두 줄** |
| `damaged-truncated.ini` | **합성** | 2026-09-11 | 16 | 한 줄 |
| `damaged-halfwritten.ini` | **합성** | 2026-09-11 | 3 | 없음 |

가운데 둘은 CWD 기준으로 ini를 저장하던 시절의 유물이다. 에디터가 지금 그 자리에 쓰지는 않지만,
사용자 기계에 남아 있을 수 있는 모양이라 이주 대상이다.

## 손상본 둘이 서로 다른 것을 잰다

앞의 넷은 손대지 않은 실물이고 `damaged-*` 둘만 **합성**이다(실물 손상본은 채집된 적이 없다).
둘로 나눈 이유는 **처음 만든 하나가 아무것도 부수지 못했기 때문**이다.

`damaged-truncated.ini`는 `current-debug.ini`를 끝에서 190바이트 잘랐다. `[Docking][Data]`
한가운데가 `Size`에서 끊겨 **오른쪽 열(Hierarchy·Inspector) 서브트리의 노드 정의가 없다.**
그런데 게이트에 태우니 **멀쩡한 파일과 값이 한 자리도 다르지 않았다**(창 16 · 도크 6 ·
undocked 0 · 노드 7). 이유를 실측했다 — ImGui는 각 `[Window]` 항목에 **그 창의 `DockId=`를
따로 적는다.** 절단이 지운 것은 트리의 모양(`SizeRef`·`Split`)이지 **소속이 아니었고**, ImGui는
살아남은 DockId 아홉으로 노드를 다시 세운다. 즉 이 fixture가 재는 것은 손상이 아니라
**복원력**이다. 그것대로 값이 있으므로 남긴다.

`damaged-halfwritten.ini`는 같은 파일을 **250바이트에서** 잘랐다. 창 항목이 셋만 남고 DockId도
셋뿐이며 `[Docking]` 섹션이 통째로 없다. Hierarchy·Inspector·AssetBundle·Content Browser는
ini에 **아예 없다.** ini가 존재하면 도크 빌더가 돌지 않으므로(`EditorRenderer.cpp`) 그 창들은
붙을 자리를 못 찾는다 — 이것이 "손상된 layout은 사용자 파일을 잃지 않고 기본 preset으로
복구된다"는 W3 판정이 실제로 상대할 상태다. 지금은 복구 경로가 없어 게이트가 값을 **기록만**
한다.

## 이진으로 두는 이유

`.gitattributes`가 이 폴더의 `*.ini`를 `-text`로 잡는다. 저장소 기본값은 `* text=auto`라
커밋할 때 CRLF를 LF로 정규화하는데, 이 파일들은 **캡처한 산출물이지 소스가 아니다.** 아이콘이
비ASCII 바이트로 들어 있고 줄바꿈이 CRLF이며, 그 바이트 그대로가 fixture의 값어치다.
정규화하면 "실물을 그대로 쓴다"는 전제가 깨진다.

## 손대지 말 것

이 파일들은 **갱신 대상이 아니다.** 에디터가 ini 형식을 바꾸거나 창이 늘어도 여기는 그대로
둔다 — 낡은 파일을 만났을 때 무슨 일이 벌어지는지가 재는 대상이기 때문이다. 새 상태를 재고
싶으면 새 파일을 채집해 표에 줄을 더하라.

게이트: `Tools/regression/verify-editor-workspace.ps1`
