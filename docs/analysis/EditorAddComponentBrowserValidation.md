# Add Component 검색·분류와 C# 생성/부착 검증

<!-- master-doc-review:start -->
## 2026-10-20 master 구현 대조

기준: `27de3f5a02aadde7456aab680db3184c36ee735e`. 대시보드는 변경하지 않는다. 아래 현행 대조와 소스 선언이 이전 시점의 구조·미구현 표기보다 우선한다.

[전체 대조·링크 정리 원장](MasterDocumentationAudit20261020.html)

### 현행 계약과 이 문서의 적용 범위

- 창·메뉴/명령과 scene 편집은 값/세대 핸들을 경계로 전달한다. 실제 Component 변경·dirty publication·Undo 수용을 분리해 확인한다.
- Workspace W9 설정/프로필·Rendering 진단과 완료된 PBR-W9는 다른 작업이다. 미구현 설정을 동작하는 제품 스위치로 표시하지 않는다.

### 해당 구현에서 확인한 선언·소유 경계

- [Editor/EngineEntry/EditorComponentCatalog.h:26](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineEntry/EditorComponentCatalog.h#L26) — `if (i && std::isupper(value)`
- [Editor/EngineEntry/EditorComponentCatalog.h:27](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineEntry/EditorComponentCatalog.h#L27) — `&& (std::islower(static_cast<unsigned char>(type[i - 1]))`
- [Editor/EngineEntry/EditorComponentCatalog.h:28](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineEntry/EditorComponentCatalog.h#L28) — `|| (i + 1 < type.size() && std::isupper(static_cast<unsigned char>(type[i - 1]))`
- [Tools/regression/verify-editor-theme.ps1:16](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-editor-theme.ps1#L16) — `$Exe = [IO.Path]::GetFullPath($Exe)`
- [Tools/regression/verify-editor-theme.ps1:17](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-editor-theme.ps1#L17) — `$Work = [IO.Path]::GetFullPath($Work)`
- [Tools/regression/verify-editor-theme.ps1:36](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-editor-theme.ps1#L36) — `$settingsBytes = [IO.File]::ReadAllBytes($settingsPath)`
- [Tools/regression/verify-cli-script-reload.ps1:1](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-cli-script-reload.ps1#L1) — `[CmdletBinding()]`
- [Tools/regression/verify-cli-script-reload.ps1:63](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-cli-script-reload.ps1#L63) — `$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))`
- [Tools/regression/verify-cli-script-reload.ps1:174](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-cli-script-reload.ps1#L174) — `$failures.Add("identity-after-failure : ReloadProbeBefore 의 ScriptComponent 가 $($probe1.Count) 개다(기대 1)")`
- [Tools/regression/verify-editor-text-fallback.ps1:5](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-editor-text-fallback.ps1#L5) — `$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))`
- [Tools/regression/verify-editor-text-fallback.ps1:19](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-editor-text-fallback.ps1#L19) — `$korean = Join-Path ([Environment]::GetFolderPath('Fonts')) 'malgun.ttf'`
- [Tools/regression/verify-editor-text-fallback.ps1:21](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-editor-text-fallback.ps1#L21) — `$emoji = Join-Path ([Environment]::GetFolderPath('Fonts')) 'seguiemj.ttf'`
- [Tools/regression/verify-script-add-awake-once.ps1:14](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-script-add-awake-once.ps1#L14) — `$Exe = [IO.Path]::GetFullPath($Exe)`
- [Tools/regression/verify-script-add-awake-once.ps1:15](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-script-add-awake-once.ps1#L15) — `$Work = [IO.Path]::GetFullPath($Work)`
- [Tools/regression/verify-script-add-awake-once.ps1:17](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-script-add-awake-once.ps1#L17) — `$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))`
- [Tools/regression/verify-editor-component-catalog.ps1:1](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-editor-component-catalog.ps1#L1) — `[CmdletBinding()]`
- [Tools/regression/verify-editor-component-catalog.ps1:5](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-editor-component-catalog.ps1#L5) — `$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))`
- [Tools/regression/verify-editor-component-browser.ps1:9](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-editor-component-browser.ps1#L9) — `$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))`
- [Tools/regression/verify-editor-component-browser.ps1:10](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-editor-component-browser.ps1#L10) — `$Exe = [IO.Path]::GetFullPath($Exe)`
- [Tools/regression/verify-editor-component-browser.ps1:11](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-editor-component-browser.ps1#L11) — `$Work = [IO.Path]::GetFullPath($Work)`
- [ScriptCore/Audio.cs:8](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/ScriptCore/Audio.cs#L8) — `public readonly struct AudioAssetId : IEquatable<AudioAssetId>`
- [ScriptCore/Audio.cs:46](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/ScriptCore/Audio.cs#L46) — `public readonly struct AudioParameter`
- [ScriptCore/Audio.cs:83](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/ScriptCore/Audio.cs#L83) — `public struct AudioPlaySettings`

### 이전 근거의 유효 범위

기존 세부 계약은 위 현행 구현과 모순되지 않는 범위에서 유지한다. 과거 수치·실행 통과·해시·장치 조건은 당시 기록이다. 로컬 산출물이 사라진 항목은 링크 대신 경로와 미보존 상태를 표시하며, 현재 master의 재실행 통과로 승격하지 않는다.

<!-- master-doc-review:end -->

<details data-master-history><summary>세부 계약·원래 분석과 당시 측정 기록</summary>

2026-09-13. 사용자 요청 범위는 기존 C# 검색·부착과 **새 스크립트 생성 → 컴파일 → 자동 부착**이다.

## 사용 흐름

- Add Component를 열면 검색란에 포커스를 둔다. 검색하지 않을 때는 Rendering, Physics,
  Animation, Audio, AI, Input, UI, Scripts, Other 카테고리를 탐색한다.
- 검색은 표시 이름·원래 타입 이름·카테고리를 대상으로 대소문자 없이 모든 검색 단어를 찾는다.
  카테고리 안에서 검색해도 전체 컴포넌트를 검색한다. 뒤로 버튼으로 루트로 돌아온다.
- 이미 붙은 네이티브 컴포넌트는 `(Added)`로 비활성화한다. C# 스크립트는 `(Script)`로 구분하며
  한 엔티티에 여러 개를 붙일 수 있다. 방향키·Enter와 마우스 선택을 지원한다.
- Inspector에는 `ScriptComponent` 대신 스크립트별 이름과 읽기 전용 Script 참조를 표시한다.
  패널 ID는 네이티브 컴포넌트 인스턴스별로 분리한다.
- New Script에서 이름을 입력하고 Create and Add를 누르면 프로젝트의
  `Assets/Script/<Name>.cs`와 메타데이터를 생성한다. 템플릿은 엔진 Component를 상속하는
  `sealed partial` C# 클래스이며 `OnBeginSimulation` 진입점을 포함한다.
- 별도 .NET 빌드 프로세스를 비동기로 실행한다. 성공하면 기존 인스턴스의 필드를 보존해
  CoreCLR 스크립트 어셈블리를 교체하고 요청 당시 엔티티에 부착한다.
  팝업을 닫거나 다른 엔티티를 선택해도 대상은 바뀌지 않는다.
- 진행/실패 상태, 취소, Retry compilation, Open script, Build log를 Inspector에 표시한다.
  컴파일 실패·취소 시 소스는 남고 빈 컴포넌트를 붙이지 않는다. Play 중 새 생성은 차단하며,
  컴파일 도중 Play에 들어가면 Stop 이후에 부착한다.

Unity의 [컴포넌트 검색·카테고리 탐색](https://docs.unity3d.com/cn/2021.3/Manual/UsingComponents.html)과
[스크립트 컴포넌트 생성](https://docs.unity3d.com/cn/2022.2/Manual/CreatingComponents.html)을
상호작용 참고로 사용했다. 엔진의 기존 ComponentFactory·CoreCLR·Undo 경로를 사용한다.

## 구현 경계

- `EditorComponentCatalog.h`: 이름 정리, 분류, 검색, 결정적 정렬. 새 네이티브 타입도 Other에서 검색 가능하다.
- `InspectorWindow`: 카탈로그는 팝업을 열 때 구축한다. 목록 내부만 스크롤하고 생성 버튼은 하단에 둔다.
- `EditorObjectOperations::AddManagedScript`: GUI/CLI 공통 검증·부착·Undo/Redo.
  미등록 타입은 변경 전에 거부한다. 엔티티와 상위 계층의 편집 잠금을 확인한다.
- `EditorScriptAuthoring`: 원래 EntityHandle을 보존하고 완료 시 수명·잠금을 다시 확인한다.
  UI와 별개로 EditorMain의 씬 구조 잠금 안에서 완료를 반영한다.
  종료·취소는 이 요청이 시작한 컴파일러 Job에만 적용한다.
- 재로드는 현재 씬의 첫 ScriptComponent에 한정하지 않고 모든 로드된 씬의 각 스크립트를 처리한다.
  새 어셈블리 거부 시 기존 인스턴스를 유지하는 계약도 보존한다.
- 소스는 덮어쓰지 않는다. 잘못된 식별자, C# 예약어, Windows 예약 파일명, 이미 등록된 클래스는 거부한다.
  현재 ScriptGenerator의 저장 키는 짧은 클래스 이름이다. 네임스페이스가 달라도 같은 이름의
  등록 스크립트를 새로 만들 수 없다.
- `GameScripts.csproj`는 `CreatorScriptSourceRoot` 아래 C# 소스를 포함한다.
  현재 프로젝트 배치의 `GameScripts/GameScripts.csproj`와 설치된 .NET SDK를 사용한다.
- 기존 스크립트 소스의 전역 자동 변경 감시, Unity의 AddComponentMenu 속성 및 사용자 지정 분류는
  이번 범위가 아니다. 스크립트 카테고리는 Scripts이며 기존 빈 스크립트의 복구 UI는 유지한다.
- Undo는 엔티티 부착을 취소한다. 생성한 소스 파일까지 삭제하지 않는다.

## 검증

| 검사 | 결과와 증거 |
|---|---|
| Debug Editor | VS18/v145 빌드 통과. `Artifacts/phase21-add-component/build-final-debug.log` |
| 검색·분류 | `verify-editor-component-catalog.ps1`, 33 checks 통과 |
| 생성·컴파일·부착 | `verify-editor-component-browser.ps1`, 415 checks / 189 commands 통과. `authoring-gate-2/summary.json` |
| 기존 재로드 계약 | `verify-cli-script-reload.ps1` 통과. 새 어셈블리 거부 시 기존 ID 보존, 이후 추가·정상 재로드, Play 상태 ID 보존 |
| 초기화 1회 | `verify-script-add-awake-once.ps1` 통과. 잘못된 타입 부착 0개, 유효한 Bobber의 Play 초기화 시도 1회 |
| 실제 DX12 UI | 카테고리·검색·이미 추가 상태, 방향키/Enter 부착, 스크립트 이름별 Inspector, 새 스크립트 폼 확인 |

415는 명령 응답과 비동기 완료 폴링을 포함하는 assertion 수이며 서로 다른 시나리오 수가 아니다.

2026-09-17 재검증: 같은 날(9-13) 들어온 엔진 배포 전환 뒤로 소스 체크아웃 에디터가 스크립트를 발행
배포본 스냅샷으로 컴파일해 생성 게이트가 붉어져 있었다(핀 부재 → 낡은 ScriptCore 덮어쓰기 거부).
체크아웃 에디터는 다시 `GameScripts/GameScripts.csproj`로 컴파일하고, 배포본 안에서 뜬 에디터만
`CreatorBuildTool compile-game`을 쓴다. 생성 게이트 251 checks / 110 commands 통과.
생성 게이트는 다음 동작을 검사했다.

- 잘못된 이름·중복 소스 거부, 미등록 타입이 빈 컴포넌트나 Undo 기록을 남기지 않음.
- 같은 Bobber 스크립트 두 인스턴스의 서로 다른 필드 값이 재로드 후 보존됨.
- 파일/메타 생성, 실제 컴파일, 정확히 한 번 부착, 부착 Undo/Redo.
- 컴파일 중 선택 변경이 대상을 바꾸지 않음.
- 의도적 컴파일 오류 후 소스 유지·부착 없음, 오류 수정 후 재시도 성공.
- 생성 전 잠금, 컴파일 후 잠금, 취소, 원래 대상 삭제 시 부착 차단.

새 스크립트 생성 게이트가 만든 소스/메타는 검증 폴더로 이동했고 원래 관리 어셈블리는
해시 확인 후 복원했다. 실제 UI 확인에서도 사용자 씬을 저장하지 않았고 EngineSettings와
imgui.ini를 시작 전 바이트로 복원한 뒤 SHA-256 일치를 확인했다.

최종 화면: 카테고리 (`../../Artifacts/phase21-add-component/ui-categories.png` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조),
Rendering과 추가 상태 (`../../Artifacts/phase21-add-component/ui-rendering.png` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조),
검색 (`../../Artifacts/phase21-add-component/ui-search.png` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조),
새 스크립트 (`../../Artifacts/phase21-add-component/ui-new-script.png` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조),
이름별 스크립트 패널 (`../../Artifacts/phase21-add-component/ui-script-attached.png` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조).
방향키 선택 후 검색 입력 유지와 폼 이름 자동 포커스도 확인했다.
이 최초 검증에서는 노출 필드의 기존 드로어를 유지했다. 배치·한글 글리프 후속 수정은 아래에 기록한다.
화면과 로그는 `Artifacts/phase21-add-component/`에 둔다. 이 검증은 DX12 기준이며
Vulkan과 전체 W2-I 폭/DPI 행렬을 완료한 것으로 계산하지 않는다. W2-I 상태·공수는 유지한다.

## 2026-09-13 후속: 아이콘·한글·스크립트 속성 배치

Add Component의 카테고리와 검색 결과에 기존 Microsoft Fluent Emoji 이미지를 연결했다.
Rendering은 상자, Physics는 번개, Audio는 스피커, Input은 게임패드, Scripts는 두루마리 등으로
구분하며 Camera·Light·Terrain·Texture·Material 항목은 해당 이미지로 표시한다.
행 선택·키보드 탐색은 기존 Selectable이 담당하고 이미지·이름·카테고리 화살표만 같은 행 안에 그린다.
이미지가 준비되지 않으면 Material Symbols를 사용한다.

Bobber의 `?`는 `[SerializeField(DisplayName = "가로 흔들기")]`로 노출한 bool 필드였다.
관리 코드의 UTF-8 이름은 정상이고, 기본 Inter에 한글 글리프가 없는 것이 원인이다.
메뉴용으로 별도 적재한 한글 폰트는 Inspector 본문을 보완하지 않았다.
`EditorFontResources`가 각 본문 폰트에 시스템 한글 폰트를 병합하도록 수정했다.
현재 Windows에서는 Malgun Gothic을 사용하며 Latin 글꼴 메트릭과 Material Symbols의 PUA 영역은 유지한다.

`DrawManagedScripts`의 Script 참조와 float·int·bool·Float3·string·Entity 필드를
공통 `begin_property_line`의 왼쪽 라벨/오른쪽 값 열로 옮겼다. Float3은 네이티브와 같은 축 위젯과
좁은 폭의 세로 배치 정책을 사용한다. Entity 참조와 해제 버튼은 값 열 안에서 폭을 나누고,
문자열은 고정 256바이트 버퍼 대신 ImGui의 std::string 입력으로 편집한다.
기존 CaptureFields 및 엔티티 편집 잠금 경로는 유지한다.

| 검사 | 결과와 증거 |
|---|---|
| Debug Editor | VS18/v145 빌드 통과. `Artifacts/phase21-component-polish/build-debug.log` |
| 실제 폰트 병합 | `verify-editor-text-fallback.ps1` 366 checks 통과. 12/16/24/32px 한글 실글리프·Latin 메트릭·66개 Material 역할 검사 |
| DX12 테마 | `verify-editor-theme.ps1 -Backends dx12` 3회 시작, 64 checks 통과. 사용자 배율 100→150→100%, OS DPI 150% 관측 |
| 실제 UI | 카테고리/Rendering/검색 아이콘, Bobber 한글 체크박스 토글, FollowTarget 한글 문자열·Float3 X=3 직접 입력, SoundProbe 문자열/정수 직접 편집 확인 |
| 폭 변화 | 창 확대/복원으로 Inspector의 Float3 세로→가로→세로 배치와 수정 값 보존 확인 |

UI 증거: 카테고리 (`../../Artifacts/phase21-component-polish/ui-categories.png` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조),
Rendering (`../../Artifacts/phase21-component-polish/ui-rendering.png` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조),
검색 (`../../Artifacts/phase21-component-polish/ui-search.png` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조),
한글 체크박스 (`../../Artifacts/phase21-component-polish/ui-bobber-korean.png` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조),
좁은 폭 (`../../Artifacts/phase21-component-polish/ui-script-stacked.png` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조),
넓은 폭 (`../../Artifacts/phase21-component-polish/ui-script-wide.png` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조),
문자열·정수 편집 (`../../Artifacts/phase21-component-polish/ui-script-string-int.png` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조),
폭 복원 (`../../Artifacts/phase21-component-polish/ui-script-width-return.png` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조).
오브젝트 참조의 빈 상태·해제 비활성화 배치는 확인했다. 자동화의 drag 동작으로 값 변경을 확인하지
못했으므로 참조 드롭과 숫자 드래그를 이번 UI 통과 실적으로 세지 않는다. 직접 숫자 입력은 확인했다.
전체 폭/DPI 행렬과 Vulkan은 이번 검증에 포함하지 않았다.
UI 검증에서 씬은 저장하지 않았으며 종료 후 EngineSettings와 imgui.ini를 시작 전 바이트로
복원했다. 두 파일의 SHA-256 일치는 `Artifacts/phase21-component-polish/ui-restoration.json`에 기록했다.

검증 도중 나타난 `text_fallback_probe.exe`의 CRT abort 창은 에디터 제품 실행이 아닌
새 폰트 검증 코드의 실패였다. ImGui 컨텍스트 초기화와 예외 처리를 추가하고,
제외 범위를 무시하는 `IsGlyphInFont` 대신 실제 표시되는 baked glyph로 아이콘 영역을 검사하도록
수정했다. 최종 실행은 366개 검사를 통과하며 실패도 콘솔 오류와 종료 코드로 보고한다.

</details>
