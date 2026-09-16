# Output Log 저장 경계 정리 — 1·2·3단계

## 1단계: 저장 경계 정리

날짜: 2026-09-13

### 적용 범위

Unity식 Output Log 개선의 첫 단계인 구조화 저장·동시 접근 경계를 구현했다.
반복 집계, Collapse, 목록/상세 UI 전환은 후속 단계다.

기존 `LogSink`는 포맷된 문자열을 `RingBuffer`에 저장했고, `DebugClass`가
friend로 버퍼를 직접 읽거나 지웠다. 쓰기는 spdlog sink mutex로 보호되지만
조회와 Clear는 그 mutex를 사용하지 않았다.

현재 경로:

```text
producer → spdlog → LogSink → LogStore
                 → HtmlFileSink (개별 발생 기록 유지)

DebugClass → LogStore snapshot → MenuBarWindow의 로컬 사본/표시 문자열
```

### 저장 계약

- `LogEntry`: 순번, 레벨, 발생 시각, 스레드 ID, 로거명, 본문 원문,
  호출 파일·함수·라인을 보관한다. spdlog의 빌린 문자열은 sink 호출 안에서 복사한다.
- 본문의 개행·탭·연속 공백·UTF-8·NUL 바이트를 저장 단계에서 가공하지 않는다.
- 출처를 제공하지 않은 로그는 라인 0과 빈 출처를 유지한다. 호출 위치를 추정하지 않는다.
- `DebugClass`가 수명 동안 같은 `LogStore`를 유지하고 `LogSink`도 이를 공유 소유한다.
  UI 조회는 초기화/종료 때 교체되는 sink 포인터를 읽지 않는다.
- `Append`, snapshot, `Clear`, 용량 초과 처리는 동일한 store mutex로 보호한다.
  snapshot은 독립적인 값이며 잠금 밖에서 포맷·렌더한다.
- 변경이 없으면 `ReadSnapshotIfChanged`는 빈 optional을 반환한다. 로그가 변경된 경우에는
  현재 보관 목록 전체를 복사한다. 변경분만 전달하는 API는 이 단계에 없었다(2단계에서
  `ReadDeltaSince`로 착지했다).
- 순번은 store가 기록을 받아들이는 순서다. Clear 후에도 재사용하지 않는다.
  여러 sink에 걸친 전역 기록 순서까지 보장하는 번호는 아니다.
- `Clear`는 같은 잠금 아래 내용을 지우고 세대/revision을 갱신한다.
  Clear 뒤 들어온 기록은 유지한다. 이전 snapshot도 읽기 가능한 독립 사본으로 남는다.
- mutable reference를 반환하던 `WriteBackLogMessage`와 사용하지 않는 마지막 문자열 API,
  `get_entries`, `IsClear/toggleClear` 화면 신호를 제거했다.

### 보관 한도

`LogStoreLimits`의 초기값은 기존 최근 500건과 문자열 내용 합계 8 MiB다.
8 MiB는 이번 단계의 초기 메모리 보호값이며 실측에 따른 성능 목표가 아니다.
문자열 내용 합계는 본문·로거명·파일·함수의 바이트 수다. 컨테이너/allocator 오버헤드,
일시적인 입력 복사, UI snapshot과 표시 문자열의 메모리를 포함한 프로세스 전체 한도는 아니다.

- 건수/문자열 합계가 넘으면 오래된 항목 전체를 제거한다.
- 한 항목 자체가 문자열 예산을 넘으면 UI 저장만 거절하고 거절 횟수를 증가시킨다.
  문자열을 잘라 저장하지 않으며 다른 sink는 원본을 계속 전달받는다.
- 제거/거절 횟수는 snapshot과 화면에 노출하며 Clear에서 초기화한다.
- 그룹 보존과 발생 이력을 분리하는 정책은 2단계에서 적용한다. 현재는 반복 로그도
  개별 항목이므로 반복 출력이 이전 항목을 밀어내는 특성은 남아 있다.

### 기존 화면 연결

- `OutputLogText.h`가 구조화 필드를 기존 spdlog 기본 표시 형식으로 변환한다.
  저장소에는 화면 표시 문자열을 넣지 않는다.
- 화면은 revision이 변경될 때만 snapshot/표시 문자열을 교체한다.
- 선택과 ImGui 행 ID는 배열 인덱스 대신 발생 순번을 사용한다.
  선택 항목이 제거되거나 Clear되면 선택을 해제한다.
- 기존 `WordWrapText(..., 120)`, `35 * 개행 수`, 경로 정규식,
  Trace 필터 예외는 화면 교체 단계에 남겨 두었다. 문자열 배치 개선 완료를 의미하지 않는다.

### 검증

회귀 진입점: [verify-log-storage.ps1](../../Tools/regression/verify-log-storage.ps1)

```powershell
./Tools/regression/verify-log-storage.ps1 -Configuration All
```

VS18 x64로 저장소·실제 sink·Editor 표시 어댑터를 포함한 독립 실행 파일을 컴파일한다.
설치된 spdlog/fmt 헤더를 사용하며 테스트 실행 파일은 header-only 방식이다.
`LogSystem.h`, `MenuBarWindow.h`도 포함해 변경된 공개 헤더의 컴파일을 확인한다.
에디터 전체 링크나 `MenuBarWindow.cpp` 전체 컴파일을 대신하는 검사는 아니다.

검증 항목:

- 빌린 입력 문자열을 변경한 뒤에도 본문·메타데이터·출처 보존.
- 공백·한글·NUL 원문 보존, 이전 formatter 결과와 표시 문자열 일치.
- 독립 snapshot, 변경 없는 조회, sink 초기화 이전 DebugClass 조회/Clear.
- 건수/바이트 한도, 메타데이터 바이트 포함, 항목 단위 거절, 제거/거절 통계.
- Clear 이후 단조 증가 ID와 이전 snapshot의 유효성.
- 구성별 4개 생산자의 80,000건 기록 + snapshot 조회.
- 구성별 추가 80,000건 기록 + snapshot 조회 + 400회의 동시 Clear.
- 실제 spdlog logger에 UI/HTML sink를 함께 연결하여 Clear·UI 거절 이후에도
  HTML 파일에서 원래 메시지와 발생 통계 확인.

결과: **Debug·Release 모두 `LOG_STORAGE_OK` 통과**. 각 구성에서 160,000건을
기록하고 동시 Clear 400회를 수행했다. 최종 실행의 snapshot 조회 수는
Debug 538/79회, Release 231/189회(각각 Clear 없음/있음)였다.
조회 수는 스케줄링에 따라 달라지며 성능 측정값으로 사용하지 않는다.
프로젝트/필터 XML과 회귀 PowerShell 구문 파싱, 변경 범위 `git diff --check`도 통과했다.

## 2단계: 반복 집계

날짜: 2026-09-15

### 적용 범위

저장소를 "발생 목록"에서 **정체성(그룹) + 발생 이력**으로 바꿨다.
Editor 트리는 한 줄도 건드리지 않았다 — `verify-editor-widget-clipping`,
`verify-editor-keyboard-nav`, `verify-editor-state-matrix` 셋이 Editor 의
`.cpp` 를 닫힌 집합으로 훑기 때문에, 화면 교체는 그 게이트들과 함께 움직여야
한다(3단계). 그래서 현재 로그 창은 코드 변경 없이 그대로 돈다.

신규 [LogGroup.h](../../Engine/Utility_Framework/LogGroup.h)가 `LogGroup`과
`LogOccurrence`를 담고 `LogStore`가 둘을 소유한다. `LogEntry`에는 store가
채우는 `groupId`가 늘었다(생산자는 비워 둔다).

### 묶음 계약

- 그룹 키는 **레벨 + 로거명 + 본문 원문 + 호출 파일·함수·라인** 여섯 축이다.
- 시각·스레드·순번은 발생 정보이며 키에 넣지 않는다. 넣으면 프레임을 넘는
  반복이 합쳐지지 않는다.
- 숫자·주소·공백을 정규화하지 않는다. `HP=10`과 `HP=20`은 다른 그룹이다.
- 본문의 NUL 이후 바이트도 키의 일부다. 해시가 `std::hash<std::string>`이라
  길이를 끝까지 읽는다.
- 해시가 같다고 같은 그룹이 아니다. 후보를 실제 키로 다시 대조한다.
- `groupId`는 재사용하지 않는다. 퇴거된 정체성이 다시 찍히면 **새 id로 시작**하고
  누적 횟수도 1부터 센다. 화면의 낡은 선택이 다른 로그에 붙지 못한다.

### 보관 정책

`LogStoreLimits`의 축이 셋으로 늘었다.

| 한도 | 초기값 | 대상 |
|---|---|---|
| `maxOccurrences` | 500 | 발생 이력 건수 |
| `maxGroups` | 500 | 보관 중인 정체성 수 |
| `maxTextBytes` | 8 MiB | **그룹** 문자열 합계 |

- 이력이 넘치면 가장 오래된 발생부터 버린다. 이때 `totalCount`는 줄지 않는다 —
  100번 반복한 로그가 이력 4건만 남아도 ×100을 계속 보고한다. 1단계에 남아 있던
  "반복 출력이 이전 항목을 밀어낸다"는 특성이 여기서 닫힌다.
- 그룹은 **보관 이력이 0인 것만** 퇴거한다. 그래서 퇴거가 이력 목록을 훑지 않고,
  "보관된 발생의 그룹은 반드시 존재한다"가 불변식으로 선다.
- 그룹·문자열 예산이 모자라면 가장 오래된 발생을 버려 그룹을 놀게 만든 뒤 퇴거한다.
- 한 항목 자체가 문자열 예산을 넘으면 저장만 거절한다(1단계와 같다).
- 반복 본문은 그룹에 한 번만 있으므로 `textBytes`는 반복으로 늘지 않는다.
- 초기값 8 MiB는 여전히 메모리 보호값이며 실측에 따른 성능 목표가 아니다.

### 변경분 조회

`ReadDeltaSince(LogCursor)`를 더했다. 커서는 `revision`·`clearGeneration`·
`lastSequence` 셋이다.

- 변경이 없으면 빈 optional을 반환한다.
- 바뀐 그룹과 새 발생만 싣는다. 퇴거된 그룹은 `removedGroups`로 알리고,
  사라진 이력은 `oldestRetainedSequence`로 알린다.
- Clear, 상태가 없는 독자, **제거 기록이 밀려난 커서**는 `resynchronized`로
  표시하고 보관 전체를 싣는다. 조용히 어긋나는 대신 다시 받게 한다.
- `ReadSnapshot`/`ReadSnapshotIfChanged`는 그대로 남아 현재 로그 창이 쓴다.
  발생 목록은 그룹의 본문으로 복원한다.

### 검증

```powershell
./Tools/regression/verify-log-storage.ps1 -Configuration All
```

결과: **Debug·Release 모두 `LOG_STORAGE_OK` 통과**. 기존 단정에 더해 묶음 축과
변경분 조회 두 묶음이 늘었다.

- 같은 정체성의 반복이 한 그룹으로 접히고 발생마다 순번·스레드·시각을 유지한다.
- 키 여섯 축을 **선언하고**, 선언한 수만큼 그룹이 늘었는지 센다.
- 시각·스레드를 바꾼 반복은 같은 그룹에 들어간다.
- `HP=10`/`HP=20`과 NUL 뒤 바이트가 다른 두 본문이 각각 다른 그룹이다.
- 이력 4건 한도에서 100번 반복 → 이력 4 · 누적 100 · 퇴거 96.
- 그룹 2개 한도에서 셋째 정체성이 들어오면 가장 오래된 것이 나가고,
  다시 찍히면 더 큰 새 `groupId`로 누적 1부터 시작한다.
- 변경분만 적용하는 독자를 만들어 매 회차마다 전체 snapshot과 대조한다.
  24회 동안 이력 퇴거와 그룹 퇴거를 모두 지나면서 재동기화가 1회(최초)뿐이다.
- Clear와 밀려난 커서는 재동기화로 표시되고, 그 뒤 독자 상태가 전체와 일치한다.

변이 증명(Debug):

| 변이 | 잡은 단정 |
|---|---|
| 그룹 조회의 본문 비교를 뒤집어 묶음을 무력화 | `repeats of one identity collapse into a single group` |
| 그룹 키에 발생 시각을 추가 | 같은 단정 |
| 이력 퇴거가 `totalCount`까지 줄이게 함 | `the repeat count outlives the occurrence buffer` |
| 퇴거된 그룹을 변경분에서 빼먹음 | `reader tracks eviction step by step` |
| 이력 퇴거가 `retainedOccurrences`를 안 줄임 | `byte budget evicts whole entry` |

마지막 변이는 **겨냥한 단정보다 앞선 것이 먼저 울렸다.** retained가 0이 되지
않아 그룹이 영영 놀지 않고, 그 결과 바이트 예산이 아무것도 못 비운 것이라
인과는 맞지만, "retained count matches the history"가 실제로 그 변이를 잡는지는
이 회차로 증명되지 않았다.

### 아직 재지 않은 것

- Editor 전체 빌드. 다른 세션이 이 시각 MSBuild 6개로 빌드 중이라 `Build\Obj`
  충돌을 만들지 않으려고 걸지 않았다. 대신 유일한 소비자가 읽는 멤버를 소스로
  대조했다 — `MenuBarWindow.cpp`는 `entries`·`revision`·`evictedEntries`·
  `rejectedEntries`와 `entry.level`·`entry.sequence`만 읽고, `OutputLogText.h`는
  `LogEntry`의 기존 필드만 읽는다. 모두 남아 있다. 게이트가 `MenuBarWindow.h`와
  `LogSystem.h`를 `/W4 /WX`로 컴파일하지만 `MenuBarWindow.cpp` 본문은 아니다.
- 실제 로그 창의 시각·입력. (3단계에서 화면을 교체하며 해소했다.)
- 이 게이트는 여전히 `Tools/regression/run-all.ps1`에 **등록돼 있지 않다.**
  손으로 부를 때만 돈다.

## 3단계: 화면 교체

이 단계가 원래 요청의 첫 문장에 답한다 — *"문자열 자르는 로직이 전혀 근거없는
눈대중으로 작성한 구 로직"*.

### 무엇이 눈대중이었나

옛 화면(`MenuBarWindow::ShowLogWindow()`)은 세 가지를 **폭을 재지 않고** 했다.

| 옛 코드 | 무엇을 가정했나 | 왜 틀렸나 |
|---|---|---|
| `WordWrapText(text, 120)` | 한 줄에 120 **바이트** | 한글은 3바이트라 40자에서 접힌다. 글꼴 폭과 무관하다 |
| `text.substr(0, 100) + "..."` | 100바이트면 한 줄 | UTF-8 중간에서 잘라 깨진 바이트를 낸다 |
| 창 폭을 안 봄 | 창이 늘 같은 폭 | 도킹·리사이즈에 따라 남거나 모자란다 |

지금은 `EllipsizeToWidth(text, maxWidth, measure)` 하나가 한다. `measure`는
`ImGui::CalcTextSize`이고, 자를 자리는 **UTF-8 글자 경계 위에서만** 이진 탐색으로
찾는다. 말줄임표를 포함한 결과가 `maxWidth` 안에 들어온다.

### 적용 범위

| 파일 | 하는 일 |
|---|---|
| `Editor/EngineGUIWindow/OutputLogView.h` | 상태와 판정. ImGui를 모른다 |
| `Editor/EngineGUIWindow/OutputLogWindow.h/.cpp` | 그리기만 한다 |
| `Editor/EngineGUIWindow/MenuBarWindow.cpp/.h` | 로그 본문 154줄과 `WordWrapText`를 뺐다(−186줄) |
| `Editor/EditorWindow/Windows/EditorToolboxWindows.h` | 창 첫 크기 선언 |

화면 상태를 `OutputLogView.h`에 따로 두는 이유는 게이트 때문이다. 판정이 ImGui
호출에 섞여 있으면 창을 띄우지 않고는 잴 수 없고, 창을 띄우는 게이트는 프로젝트에
하나뿐인 endpoint 파일을 독점해 다른 게이트와 같이 못 돈다. 지금은 말줄임·필터·
검색·Collapse·선택 수명을 전부 `/W4 /WX` 단독 컴파일 프로브에서 잰다.

### 화면 계약

- **Collapse** — 같은 정체성의 반복을 한 행으로 접고 오른쪽에 누적 수를 적는다.
  배지는 `totalCount`다. `retainedOccurrences`가 아니다 — 이력 버퍼가 밀려도
  "몇 번 찍혔는지"는 남아야 한다.
- **수준 필터** — `group.level < minimumLevel`이면 거른다. 옛 화면은 Trace를 어떤
  설정에서도 흘렸다. 그 결함을 고쳤다.
- **검색** — 평탄화 **이전의 원문**을 본다. 화면이 개행을 공백으로 바꿔 보여주는
  것은 표시 사정이고, 찾는 사람은 자기가 찍은 글자를 찾는다.
- **clipper** — `ImGuiListClipper`로 보이는 행만 낸다. 행 목록은 평탄하다.
- **선택 수명** — 고른 그룹이 퇴거되면 선택을 비운다. 그러지 않으면 상세 창이
  없는 그룹을 가리킨다.
- **출처** — 없으면 `source: none (the producer did not supply a call site)`라고
  **적는다.** 지금 이 엔진의 로그는 전부 출처가 없다. 숨기면 4단계가 무엇을
  고쳐야 하는지 흐려진다.
- **파일 열기** — 상세 창의 버튼으로만 연다. 옛 화면은 **아무 줄이나 한 번 누르면**
  본문에서 Windows 경로를 찾아 편집기를 띄웠다. 읽으려고 고른 것만으로 열렸다.

### 그리기 규약

표준 위젯만 쓴다 — `AddText`·`RenderTextClipped`·`ItemAdd`를 직접 부르지 않는다.
그래야 클리핑과 nav 커서를 ImGui가 맡고, W2의 custom draw 계약
(`verify-editor-widget-clipping`·`verify-editor-keyboard-nav`)이 요구하는 신고를
따로 들 필요가 없다. 행은 라벨이 빈 `Selectable("##row", ...)`로 깔고 본문을 그
위에 낸다 — `Selectable`은 라벨 안의 `##`를 식별자 구분자로 읽는데, 로그 본문에
`##`가 들어오는 일이 실제로 있다.

### 겹쳐 그리려고 `SetCursorPos`로 돌아가면 안 된다

첫 판은 행 위에 본문을 겹치려고 `SetCursorPos`로 행 머리로 돌아갔다가, 다음 행
자리를 `SetCursorPos(rowStart.y + rowHeight)`로 직접 놓았다. 컴파일도 되고 그림도
맞았지만 ImGui가 **매 프레임** 오류를 냈다.

```
In window ' Log###Editor.OutputLog/OutputLogRows_...':
Code uses SetCursorPos()/SetCursorScreenPos() to extend window/parent boundaries.
Please submit an item e.g. Dummy() afterwards in order to grow window/parent boundaries.
```

`SetCursorPos`는 커서만 옮기고 창의 content 크기를 키우지 않는다. ImGui는 항목
제출 없이 커서가 경계 밖으로 나간 채 `EndChild`에 닿으면 그것을 오류로 신고한다.

고친 방식은 `Dummy()`를 덧붙이는 쪽이 아니다 — 그러면 그 항목이 다시 보폭을
바꾼다. 행 머리로 돌아가는 일을 `SameLine(rowStartX)`가 하게 했다. `SameLine`은
앞 줄의 높이를 그대로 물려받으므로 경계도 보폭도 건드리지 않는다. 그리고 목록
안에서만 `ItemSpacing.y`를 0으로 눌러, `Selectable`이 차지하는 높이와 다음 행이
시작하는 자리를 둘 다 `rowHeight`로 맞췄다 — clipper에 알려 준 보폭과 실제
보폭이 갈리면 **스크롤해야만** 드러난다.

측정: 같은 자극으로 띄운 에디터 stdout의 `[imgui-error]` 줄 수가 **62 → 0**.

이것은 게이트가 못 잡았다. 게이트는 `OutputLogView.h`만 컴파일하고 ImGui를 띄우지
않으며, 네 에디터 게이트도 이 오류를 세지 않는다. 사람이 창을 열어 본 것이 유일한
관측 수단이었다.

### 창 첫 크기

`initial_size`를 싣지 않으면 ImGui가 **내용에 맞춰** 접는다. 처음 뜬 로그 창이
들고 있는 것은 툴바 한 줄과 로그 몇 줄뿐이라, 자동 크기가 1~2행짜리 창이 됐다.
Auto Scroll이 켜져 있으면 마지막 한 줄만 보이고 나머지는 스크롤 밖으로 밀린다.
선언에 크기를 실었다 — 형제 창들이 크기를 선언하는 그 자리다. 행 하나가 두 줄
짜리 카드가 된 뒤 `1180x640` 이다.

### 검증

```powershell
./Tools/regression/verify-log-storage.ps1 -Configuration Debug
```

결과: **`LOG_STORAGE_OK`**. `PASS presentation: 글자 폭 말줄임, 행 평탄화, 수준
필터, 검색, Collapse, 선택 수명`이 늘었다.

말줄임은 **자 둘**로 잰다. 글자 폭 자(글자당 고정 폭)와 바이트 자(바이트당 3px).
자가 하나뿐이면 글자 경계와 바이트 경계가 같은 자리에서 갈라져, 경계를 무시하는
구현도 같은 답을 내 통과한다(아래 P1 참조).

에디터 축도 전부 통과했다.

| 무엇 | 결과 |
|---|---|
| Editor Release 빌드 | exit 0 |
| `--exec editor.selftest`(선언 자가 검사) | exit 0 |
| `verify-editor-declaration-wiring` | exit 0 |
| `verify-editor-widget-clipping` | exit 0 |
| `verify-editor-keyboard-nav` | exit 0 |
| `verify-editor-state-matrix` | exit 0 |

변이 증명(Debug, 여섯 전부 겨냥한 단정이 잡았다):

| 변이 | 잡은 단정 |
|---|---|
| P1 UTF-8 경계 대신 바이트 단위로 자른다 | `바이트 자 아래에서도 글자 경계까지만 담는다` |
| P2 Trace를 언제나 흘린다(옛 결함 복원) | `고른 수준 미만은 전부 걸러진다` |
| P3 검색이 평탄화한 것을 본다 | `검색이 보는 것은 평탄화 이전의 원문이다` |
| P4 배지에 `totalCount` 대신 보관 이력을 싣는다 | `배지는 누적 20이다 — 보관 이력 3이 아니다` |
| P5 퇴거된 그룹의 선택을 안 놓는다 | `퇴거된 그룹의 선택은 비워진다` |
| P6 변경분이 들어와도 행을 무효화하지 않는다 | `필터가 같아도 변경분이 들어오면 행을 다시 센다` |

**P1과 P4는 처음에 자극조차 되지 않았다.** 고친 것은 구현이 아니라 fixture다.

- P1: 글자 폭 자 하나만 있을 때는 글자 경계와 바이트 경계가 같은 자리를 가리켜
  바이트 단위 구현도 같은 답을 냈다. 둘이 실제로 갈라지는 바이트 자를 더했다
  (한글 30바이트, 폭 40 → 옳은 답 9바이트/12, 바이트 단위 10바이트/13).
- P4: 쓰던 저장소가 넉넉해 `totalCount == retainedOccurrences`였다. 둘이 갈라지는
  좁은 저장소(이력 3, 20회 반복)를 더해 배지가 20인지 3인지 물었다.

### 실제 화면 확인

게이트는 창을 띄우지 않는다. Release 에디터를 스크립트로 띄워 캡처했다.

```
window.resize 2560 1440 ×9   (같은 본문·같은 수준의 경고를 아홉 번 만든다)
editor.window ###Editor.OutputLog open / focus
editor.nav pointer <x> <y> / press / release   (행 하나를 고른다)
```

관측된 것: 반복 아홉 개가 한 행으로 접히고 오른쪽 배지가 **9**, 수준별 아이콘과
색(warn 노랑 · info 기본 · trace 흐림), 오른쪽 끝의 실측 폭 말줄임, 머물면 뜨는
전문 툴팁, 고른 행의 상세 창(Copy · 수준 · logger · `total`/`retained` ·
`source: none`).

### 아직 재지 않은 것

- **ImGui 오류를 세는 자가 없다.** 위의 62 → 0은 손으로 stdout을 센 것이다.
  제품에 `[imgui-error]` 계수기가 없어 게이트로 만들지 못했다. 같은 부류의 결함이
  다시 들어와도 아무 게이트가 붉어지지 않는다.
- 픽셀 골든 없음. 위 캡처는 사람이 본 것이지 재는 자가 아니다.
- 이 게이트는 여전히 `Tools/regression/run-all.ps1`에 **등록돼 있지 않다.**
- `OutputLogText.h`는 이제 제품 소비자가 0이고 게이트만 부른다. 처분은 4단계로.

## 3단계 보강: 카드 목록과 수준별 누적

한 줄짜리 목록으로는 **무엇이 얼마나 심각한지**가 한눈에 안 들어왔다. 글자 색
하나로만 가르면 수백 줄 중에서 오류가 눈에 띄지 않는다.

### 행 하나가 카드다

```
┌──────────────────────────────────────────────────────────┐
│▌⚠  [CLI] 창 크기 변경(클램프됨): 요청 2560x1440 -> …   9  │
│    Warning · multi_logger · 17:26:25.580                 │
└──────────────────────────────────────────────────────────┘
```

- 배경은 수준 색을 옅게 깐다(info 4.5% · warn 13% · error 16%).
- 왼쪽 3px 강조 띠. 옅은 배경만으로는 info 와 trace 가 안 갈린다.
- 둘째 줄에 `수준 · 로거 · 시:분:초.밀리`. 전에는 어디에도 없던 값이다.
- 선택은 강조색 배경 + 테두리.

배경·띠·테두리는 그리기 목록(`AddRectFilled`/`AddRect`)으로 깐다. W2 의 custom
draw 계약이 대상으로 삼는 것은 **글자를 직접 그리는 자리**(`RenderText`·`AddText`)
이고 사각형은 거기 들지 않는다. 글자는 전부 표준 위젯이 낸다.

### 보폭은 여전히 ImGui 가 정한다

카드가 두 줄이 되면서 `clipper` 에게 줄 보폭이 `lineOne + 글자높이 + 카드간격`
으로 늘었다. 이 값을 직접 커서에 놓지 않는다 —

- 첫 줄: `Selectable(높이 lineOne)` 이 자리를 잡고, 본문은 `SameLine` 으로 그
  줄에 겹친다.
- 둘째 줄: 자연스러운 다음 줄. 커서를 놓는 호출이 없다.
- 카드 사이 간격: `Dummy(0, gap)`. **항목**이라 ImGui 가 보폭을 세고, 뒤에
  커서를 놓는 호출이 남지 않는다.

측정: 같은 자극에서 `[imgui-error]` **0 줄**(고치기 전 62 줄).

### 카드 전체가 눌린다

첫 줄만 `Selectable` 이면 메타 줄을 눌러도 아무 일이 없다. 둘째 줄에도 같은
동작을 걸되 `ImGuiItemFlags_NoNav` 로 nav 는 끈다 — 카드 하나가 키보드 정거장
둘이 되면 안 된다. 둘 다 `Header*` 색을 투명으로 눌러 카드 배경을 덮지 않는다.

### 툴바가 두 줄이다

첫 줄에 `Clear ┃ Collapse · Auto Scroll ┃ 수준 고르개 · 수준별 누적`, 둘째 줄에
검색. 한 줄에 다 넣었더니 창을 좁혔을 때 검색 상자가 **조용히** 창 밖으로 나갔다
— `SetNextItemWidth` 는 남은 폭이 음수여도 최소값으로 그리므로 잘못을 알리지
않는다.

### 수준별 누적

`💬 11  ⚠ 10  ⛔ 0` 을 두 자리에 낸다.

1. 로그 창 툴바 — 누르면 그 수준을 최소 수준으로 세운다. 세는 자리와 거르는
   자리가 같아야 "이 54 개를 보고 싶다" 가 한 번에 끝난다.
2. 아래 상태 표시줄, **디버그 모드 버튼 왼쪽** — 로그 창을 열지 않아도 무엇이
   쌓였는지 보이고, 누르면 그 창이 열린다.

두 자리가 같은 값을 읽는다. 저장소가 `LogStore::m_levelTotals` 하나를 들고,
`ReadLevelTotals()`(상태 표시줄)와 `LogDelta::levelTotals`(창)가 그것을 물린다.
창이 자기 목록을 세면 두 벌이 되어 한쪽만 맞는 일이 생긴다 — 값의 일치가 아니라
**출처의 동일성**으로 세운다.

계약:

| 무엇 | 어떻게 |
|---|---|
| 버킷 | trace·debug·info → 메시지, warn → 경고, err·critical → 오류 |
| `off` | 어디에도 안 든다 |
| 퇴거 | 누적을 **줄이지 않는다.** 아홉 번 찍힌 것은 둘만 남아도 아홉이다 |
| 거절(예산 초과) | 센다. 찍으려 한 것은 사실이고, 숨기면 없는 것을 찾게 된다 |
| `Clear` | 0 으로 되돌린다 |

### 검증

`PASS level totals: 버킷 분류, 퇴거 뒤 생존, 거절 계상, Clear` 가 늘었다.
에디터 게이트 넷과 Editor Release 빌드는 그대로 exit 0.

변이 증명(Debug, 여섯 전부 잡힘):

| 변이 | 잡은 단정 |
|---|---|
| T1 critical 을 메시지로 센다 | `trace·debug·info 셋이 메시지 하나로 접힌다` |
| T2 `off` 도 메시지로 센다 | `off 는 세지 않는다` |
| T3 퇴거가 누적을 줄인다 | `퇴거는 누적을 줄이지 않는다 — 아홉 번 찍힌 것은 둘만 남아도 아홉이다` |
| T4 거절된 기록을 세지 않는다 | `거절된 기록도 수준별로 센다` |
| T5 `Clear` 가 누적을 남긴다 | `Clear 는 누적을 0 으로 되돌린다` |
| T6 변경분이 누적을 안 싣는다 | `변경분이 실은 누적이 snapshot 과 같다` |

**T3 은 처음에 겨냥한 단정이 아니라 앞선 것에 걸렸다.** 버킷 축과 퇴거 축이 한
저장소(한도 4)를 같이 쓰고 있어서, 퇴거를 건드리는 변이가 버킷 단정 앞에서
드러났다. 그러면 "퇴거는 누적을 줄이지 않는다" 는 **증명되지 않은 채** 남는다.
버킷 축을 넉넉한 저장소(한도 64, `evictedEntries == 0` 을 전제로 단정)로 옮기고
퇴거 축을 따로 세웠다. 그 뒤 T3 은 제 단정에 걸린다.

T1 은 지금도 오류 버킷 단정이 아니라 메시지 버킷 단정에 걸린다. 한 번의 계상이
두 버킷을 동시에 움직이므로 어느 쪽이 울려도 같은 사실을 가리킨다 — 나누지
않았다.

## 4단계 보강: 아무도 안 읽던 두 채널

`Tools/regression/verify-editor-startup-diagnostics.ps1` 를 새로 세웠다.
`run-all.ps1` 에는 **넣지 않았다**(요청).

### 왜

2026-09-16 에 결함 둘이 같은 날 들어왔고 **둘 다 빌드 exit 0** 이었다.

| | 무엇 | 왜 컴파일러가 못 잡나 |
|---|---|---|
| ① | 일괄 치환이 문자열 리터럴 46 자리에 ` << ` 를 박았다. 그중 둘이 `spReflection*_GetTypeLayout` 심볼 이름이라 Slang reflection 적재가 실패했다 | 전부 문자열 |
| ② | 네이티브가 `CreatorScriptApiVersion` 을 24→25 로 올렸는데 `ScriptCore/Native.cs` 의 `ExpectedVersion` 이 24 로 남아 CLR 초기화가 실패했다 | 런타임 비교 |

두 실패 메시지는 **인메모리·HTML 싱크로만** 가고 stdout 에는 안 나온다. 회귀
하네스가 읽을 수단이 아예 없었다 — 사람이 로그 창을 열어 보고 찾았다.

거기에 3단계가 낸 `[imgui-error]` 도 같은 처지였다. 그리기가 매 프레임 규약을
어겼는데 그림은 맞았고 게이트 넷은 전부 초록이었다.

### 재는 것

- HTML 로그의 심각 행 수 → 0
- stdout 의 `[imgui-error]` 줄 수 → 0

심각도 집합을 손으로 적지 않는다. `HtmlFileSink.h` 안의 뷰어 스크립트가 이미
정하고 있으므로 거기서 뽑고, 뽑지 못하면 통과시키지 않고 던진다. 값의 일치가
아니라 **출처의 동일성**이다.

### 빈 집합을 성공으로 읽지 않기

"오류 0" 은 로그를 못 읽어도 0 이다. 그래서 자극이 실제로 일어났음을 따로
단정한다 — 로그 창이 실제로 열렸고(결과 행의 `data.stableId`·`data.request` 로
본다. `command` 필드에는 동사만 들어 있다), 표지 씬이 저장됐고, 이 회차가 만든
HTML 로그가 **새로** 생겼고, 그 안에 이 회차의 `scene.save` 줄이 있고, 세션 종료
줄까지 있어 로그가 잘리지 않았다.

행 **개수**로는 재지 않는다. 처음에 `$totalRows -ge 20` 으로 적었다가 실측하니
매 회차 정확히 16 이었다 — 눈대중이었고, 자극이 바뀌면 조용히 낡는다. 내가
일으킨 사건이 보이는지로 바꿨다.

### 변이

| 변이 | 결과 | 관측 |
|---|---|---|
| G0 자식 창을 닫기 직전에 커서를 내용 밖으로 던진다 | 잡힘 | `imgui-error 444 줄` (고유 2 종) |
| G1 행 끝의 `Dummy` 를 `SetCursorPos` 로 바꾼다 | **못 잡음** | `심각 0 · imgui-error 0` |
| G2 `Native.cs` 의 `ExpectedVersion` 을 24 로 되돌린다 | 잡힘 | `[CLR] 관리 초기화 실패 (result=-1) — API 표 버전이 어긋났을 수 있습니다` |
| G3 Slang 심볼 이름을 하나 망가뜨린다 | 잡힘 | `[EnhancedRenderer] 파이프라인 구축 실패: 노드 'Shadow' 초기화 실패: Slang reflection C API 진입점이 없다` |
| G4 게이트 자신이 로그 창을 안 연다 | 잡힘 | `로그 창을 열지 못했다` |
| 대조 (변이 없음) | 초록 | `행 16 · 심각 0 · imgui-error 0`, 9 checks |

G2·G3 이 낸 것은 **2026-09-16 에 실제로 났던 그 문장 그대로**다. 이 게이트가
지키기로 한 두 결함을 각각 제 단정으로 다시 잡았다.

**G4 가 실패한 방식이 요점이다.** 그 회차는 `심각 0 · imgui-error 0` 을 내고도
붉었다. 0 만 봤다면 초록이었을 회차다.

행 수는 회차마다 16·14·17 로 흔들렸다(G2 는 CLR 이 죽어 뒤가 줄고, G3 은 실패
보고가 늘어난다). 처음 쓰려던 `$totalRows -ge 20` 이었다면 **G2 를 행 수로 잡고
심각 행으로는 못 잡은 것**이 되어, 무엇을 재는 게이트인지가 흐려졌을 것이다.

**G1 은 못 잡았고, 왜인지는 안다.** ImGui 1.92.8 의
`ErrorCheckUsingSetCursorPosToExtendParentBoundaries` 는 `End`·`EndChild`·
`EndGroup` 에서 `IsSetPos` 가 **선 채로** 닿았고 커서가 `CursorMaxPos` 를 넘었을
때만 운다. 행 끝의 `SetCursorPos` 는 다음 행이 내는 항목이 지우고, 마지막 행의
것은 `ImGuiListClipper` 의 마지막 `Seek` 이 `CursorMaxPos` 를 **직접 올려**
흡수한다. 그래서 이 변이는 위반이 아니다.

그러면 2026-09-16 의 진짜 결함은 왜 울었나. 그때 관측한 프레임 번호가
71·160·398 로 **띄엄띄엄**이었다. 수동 누산(`rowStart.y + rowHeight` 를 행마다
더해 간다)과 clipper 의 `start + n*stride` 가 부동소수점으로 갈라져, 누산 쪽이
한 ULP 위로 올라간 프레임에서만 울었다는 뜻이다. 즉 **이 축은 결정적이지 않다**
— 한 프레임 단위로는 확률적이고, 회차가 600 프레임쯤 되기 때문에 실무에서
잡히는 것이다. G0 는 그 확률에 기대지 않는 자리라 매번 운다.

`-AllowedErrorSubstrings` 는 비워 두는 것이 정상이다. 채울 때는 왜 그것이
기대되는 오류인지 한 줄 적는다.

## 출처 축: 태그를 걷고, 두 경로를 합치고, 읽을 채널을 냈다

### 논리 태그는 살아 있지도 않았다

`Debug::PrintLog` 는 첫 인자로 논리 태그 `source` 를 받아 메시지 앞에 `[태그] ` 로
붙였다. 호출처 **596 중 592 가 빈 `{}`** 를 넘기고 있었고, 나머지 넷은 전부 중계지
생산자가 아니다(셋은 `LogSystem.h` 안의 전달, 하나는 CLR 브리지). 태그를 채우는
유일한 생산자는 C# 경계인데 그쪽 호출자가 0 이라 `[{}] {}` 분기는 **런타임에 죽어
있었다.** 살아 있지도 않은 축을 위해 592 자리가 `{}` 를 적고 있었다.

태그는 구조 필드도 아니었다 — 문자열 연결이라 로그 창이 따로 열에 낼 수도 걸러낼
수도 없고, 바이트 단위 묶음 키에 메시지로 섞여 들어갔다. `LogStore` 의
`entry.source` 는 `file`/`line`/`function` 이라 **이름만 같고 다른 것**이다.

걷어냈다. 592 자리는 바이트 단위로 고쳤다 — 패턴이 순수 ASCII 라 파일을 디코딩하지
않았다(같은 날 일괄 치환이 CP949 파일 7 개의 한국어 주석 112 줄을 U+FFFD 로 지운
일이 있었고, 그것은 읽기에서 인코딩을 가정했기 때문이다. 읽지 않으면 그 축이 생기지
않는다). 그리고 **옛 오버로드를 지웠다** — 놓친 자리가 조용히 남는 대신 컴파일
오류가 된다. 빌드 exit 0 이 곧 592 자리가 전부 맞다는 뜻이다.

### `std::source_location` 은 CLR 경계를 못 넘는다

C# 에는 대응 장치가 있다. `[CallerFilePath]`·`[CallerLineNumber]`·
`[CallerMemberName]` 은 컴파일러가 호출 지점에서 채운다 —
`std::source_location::current()` 기본 인자와 같은 성격이고 호출자는 아무것도
적지 않는다.

다만 `std::source_location` 은 **값으로 만들 수 없다**(표준이 `current()` 만 준다).
경계 밖에서 온 file/line/function 을 담을 수 없으므로 `spdlog::source_loc` 을 직접
받는 저수준 오버로드를 하나 뒀고 CLR 브리지가 그리로 들어간다. 그 오버로드가
없으면 기본 인자가 재평가돼 모든 관리 로그가 `ClrHost.cpp` 를 자기 출처로 보고한다.
포인터는 호출 동안만 살면 된다 — 로거가 동기라(`async_logger` 가 아니다) 포매팅이
거기서 끝나고, 관리 측 `fixed` 핀이 그 구간을 덮는다.

### ★ 관리 로깅 경로가 둘이었고, 고친 쪽은 안 쓰는 쪽이었다

| 슬롯 | 관리 측 호출 | 출처 |
|---|---|---|
| `Native.Log` → `Api_Log` | **44 자리** (`Component.Log`·`BehaviorTree`·`AniBehavior`·`Bootstrap`·`BlackBoard`…) | `ClrHost.cpp` 한 줄 — 전부 |
| `Native.PrintLog` → `Api_PrintLog` | **0** | `[Caller*]` 를 얹은 쪽 |

즉 `[Caller*]` 작업이 **트래픽이 흐르지 않는 경로**에 들어가 있었고, 실제 관리
로그는 여전히 브리지 자신을 가리키고 있었다. 게이트를 세우려다 알았다 — 자극할
대상이 무엇인지 확인하지 않았으면 안 쓰는 경로에 게이트를 걸 뻔했다.

`Native.Log` 자체가 `[Caller*]` 기본 인자를 받아 `PrintLog` 슬롯으로 가게 합쳤다.
**호출 44 자리의 텍스트는 한 글자도 안 바뀌고** 출처만 붙는다. 표에서 `Log` 슬롯과
`Api_Log` 가 사라져 `CreatorScriptApiVersion` 이 26→27 이고 `ExpectedVersion` 을
같은 커밋에서 올렸다.

래퍼 아홉(`Component`·`BehaviorTree`·`AniBehavior` × `Log`/`LogWarning`/`LogError`)은
자기 `[Caller*]` 를 **전달**하게 했다. 안 그러면 44 자리가 전부 `Component.cs` 한
줄을 가리킨다 — 출처는 **있으므로** "출처가 있는가" 로는 안 걸리는 고장이다.

### 출처를 읽을 채널이 없었다

`HtmlFileSink` 는 시각·수준·tid·메시지만 적고, `set_pattern` 은 어디에도 없고
(spdlog 기본 패턴은 출처를 안 찍는다), CLI 는 `log.flush` 뿐이었다. 출처는
`LogStore` 에 들어가 **로그 창 상세 패널에서 눈으로만** 보였다. 파일로도 안 나가고
게이트가 읽을 길도 없었다.

`<tr>` 에 `data-src="파일:줄"` 을 달았다 — 표 배치를 안 건드려 기계가 읽는다.
사람 쪽으로는 메시지 칸 끝에 흐린 글씨를 붙인다. 경로 전체가 아니라 파일 이름만
싣는다(절대 경로는 빌드 기계마다 달라 잡음이다).

### 변이

기동 진단 게이트에 단정 넷을 더했다(9 → 13 checks).

| 변이 | 결과 | 관측 |
|---|---|---|
| H1 래퍼가 `[Caller*]` 를 전달 안 함 | 잡힘 | `관리 로그가 래퍼 자신을 출처로 싣는다: Component.cs:341` |
| H2 마샬러가 호출 지점을 버림 | 잡힘 | `관리 로그 출처 0 줄 · 고유 0 자리` |
| H3 싱크가 출처를 안 적음 | 잡힘 | 〃 |
| 대조 | 초록 | `Bootstrap.cs:31 · 356 · ManagedLogProbe.cs:26 · 27`, 13 checks |

**H1 이 세 번 만에 잡혔고, 그 과정이 결과보다 값지다.**

1. 첫 회차는 **빌드가 실패**했다. 다른 세션이 `EditorObjectOperations.cpp` 에 쓰는
   자리를 먼저 넣고 `#include` 를 뒤에 넣는 사이 컴파일이 파일을 읽어 `C2653` 이
   났다. 빌드 실패를 "게이트가 못 잡았다" 로 적으면 없는 구멍을 만든다 — 러너가
   '재지 못함' 을 따로 적게 해 뒀다.
2. 둘째 회차는 임시 명령의 따옴표 중첩이 깨져 **빌드가 안 돌았고**, 앞 변이가 구워진
   낡은 바이너리를 쟀다. 산출물 시각 가드를 우회한 것이 원인이다.
3. 셋째 회차에서 제대로 쟀더니 **자극 자체가 그 코드를 안 지났다.** 이 게이트의
   관리 로그는 `Bootstrap.cs` 둘뿐인데 그것은 `Native.Log` 를 **직접** 부른다 —
   `Component.Log` 래퍼를 한 번도 안 지난다.

그래서 `GameScripts/ManagedLogProbe.cs` 를 세웠다. `Component.Log`/`LogWarning` 을
서로 다른 줄에서 부르고, 게이트가 `script.invoke` 로 돌린다. `LogError` 는 쓰지
않는다 — 오류 수준은 같은 게이트의 '심각 행 0' 에 걸려 자기 fixture 가 게이트를
붉게 만든다.

**그리고 단정을 자리 수로 걸면 H1 이 안 걸린다.** H1 회차의 고유 자리는 **여전히
4** 였다(`Bootstrap.cs` 둘 + `Component.cs:341` + `ManagedLogProbe.cs:27`). 래퍼
파일 이름이 출처로 나오는 것 자체를 금지해야 걸린다. 그 앞에는 "`ManagedLogProbe.cs`
가 로그에 있는가" 를 둬서, 래퍼를 안 지난 회차가 빈 집합으로 통과하는 것을 막는다.

## 출처 축 닫기: C++ 래퍼 셋과 `OutputLogText.h`

### 후보는 열여섯이었고 진짜는 셋이었다

앞 절에서 "`PakHelper.h`(39 자리)" 라고 적은 것은 **틀린 셈**이다. 그 파일의 로그
호출 서른아홉 중 대부분은 결함이 아니다. 직접 부르는 줄은 그 줄이 곧 진짜 호출
지점이고, `WriteCensus`·`ApplyGBufferShaderMeta`·`WriteCrashReportArtifacts` 처럼
메시지를 자기가 만드는 함수도 사건이 거기서 일어나므로 그 줄이 찍히는 것이 맞다.

결함은 **매개변수로 받은 메시지를 그대로 넘기면서 `where` 를 생략하는** 함수뿐이다.
그때만 기본 인자가 래퍼 자리에서 채워져 호출자 전부가 한 줄로 몰린다. 저장소를
그 기준으로 훑으니 후보 열여섯 중 셋이었다.

| 래퍼 | 호출자 | 몰리던 자리 |
| --- | --- | --- |
| `ReportSettingsError` | 18 | `EditorSettingsStore.cpp:69` |
| `RuntimeCleanupError` | 13 | `PakHelper.h:33` |
| `RuntimeCleanupInfo` | 1 | `PakHelper.h:41` |

셋 다 `std::source_location where = std::source_location::current()` 를 받아 그대로
넘기게 고쳤다. `DumpHandler.h`·`ComponentTypeUUID.h`·`ProxyCommandQueue.h` 는 목록에
적혀 있었으나 이 기준으로는 래퍼가 아니었다.

### 이 축은 런타임으로 못 잡는다

셋 다 오류 경로거나 pak 런타임 정리 경로라 기동 한 바퀴에서 한 번도 안 돈다.
자극할 수 없는 것을 런타임 단정으로 적으면 **미자극이 초록으로 읽힌다.** 그래서
기동 진단 게이트에 소스 축 단정을 넣었다(13 → 19 checks). 게이트는 이미
`HtmlFileSink.h` 에서 심각도 집합을 뽑고 있어 같은 자리다.

| 변이 | 무엇을 한다 | 결과 |
| --- | --- | --- |
| W1 | 래퍼에서 `source_location` 매개변수를 뗀다 | 잡힘 — `ReportSettingsError 가 … 받지 않는다` |
| W2 | 래퍼 이름을 바꾼다 | 잡힘 — `래퍼 정의를 0 건 찾았다 (기대 1)` |

**W2 가 이 게이트 자신의 결함을 드러냈다.** 처음 쓴 정규식은 이름 뒤의 괄호를 그냥
찾아서 정의가 아니라 **호출 자리**를 잡고 있었다(`return ReportSettingsError("...")`).
그때까지 초록이던 것은 두 파일 모두 정의가 호출보다 앞에 있었던 우연이다. 매개변수에
**타입 선언**이 있는 것만 정의로 인정하게 고쳤다 — 호출은 값이나 식을 넘긴다. 그리고
정의를 찾았는지를 먼저 단정한다. 표가 낡아 0 건이 되면 "`source_location` 이 없다" 가
아니라 아무 일도 안 일어난 채 통과하기 때문이다. W2 회차에서 총 checks 가 19 → 18 로
줄어 단정 하나가 통째로 사라진 것까지 드러난다.

### `OutputLogText.h` 를 걷었다

`FormatOutputLogEntry` 는 저장한 `LogEntry` 를 `spdlog::details::log_msg` 로 되돌려
옛 창이 내던 문자열을 다시 만드는 함수였다. 3단계에서 화면을 카드 목록으로 바꾸며
**비교 대상인 옛 화면이 없어졌고**, 그 뒤로 제품 소비자가 0이다. 카드 UI 의 Copy 는
`group->message` 원문을 쓴다 — `OutputLogView.h` 가 "검색·복사·상세는 언제나 원문을
본다" 고 못 박아 둔 대로다.

`ScriptCore/Debug.cs` 에서 "소비자 0" 을 틀리게 세었던 일이 있어 이번엔 축을 나눠
다시 셌다: 제품 호출 0, `Editor.vcxproj`·`.filters`·`.sln` 등재 0(`ClInclude` 124 건이
전부 손표이고 와일드카드가 없다), 패키징 내보내기 0, 게이트 단정 1. `Debug.cs` 와
결정적으로 다른 점은 이것이 **밖에 내준 표면이 아니라 에디터 내부 헤더**라는 것이다.

남아 있던 단정 하나는 바로 위 필드 단정들에 **통째로 포함된다.** 기본 패턴 `%+` 가
읽는 것과 위 단정이 재는 것을 맞대면 이렇다.

| `%+` 가 읽는 것 | 바로 위 필드 단정 |
| --- | --- |
| time (밀리초 절삭) | `entry.timestamp == timestamp` — 절삭 없음 |
| logger_name | `entry.loggerName == "producer"` |
| level | `entry.level == warn` |
| source.filename (**basename 만**) | `entry.source.file == originalFile` — 전체 경로 |
| source.line | `entry.source.line == 42` |
| payload | `entry.message == original` |

`%+` 는 `thread_id` 와 `funcname` 을 아예 안 읽는데 그 둘도 위에서 재고 있다. 즉 이
단정은 덮는 범위가 더 좁고 정밀도도 더 낮았다. 헤더와 단정을 같이 걷고, 쓰임이 사라진
`#include <spdlog/pattern_formatter.h>` 까지 뺐다. `sink.set_pattern("this pattern must
not become the stored payload")` 는 남겼다 — 싱크의 패턴이 저장 payload 로 새지 않는지는
`entry.message == original` 이 계속 지킨다.

## 다음 단계

목록이 비었다. 네 항목의 행선지는 이렇다.

| 항목 | 결과 |
| --- | --- |
| 1·3 관리 로그 두 경로 합치기·읽을 채널 | `e350cabe` |
| 2 C++ 래퍼 호출처 | `2240a73e` |
| 4 `OutputLogText.h` 처분 | 이 커밋 |

기동 진단 게이트는 `Tools/regression/run-all.ps1` 에 **등재하지 않는다.** 사용자
판단이고, `verify-log-storage.ps1` 과 같은 취급이다. 그러니 이 단정 열아홉은 손으로
부를 때만 돈다 — 자동으로 도는 세트에 없다는 뜻이다.

`ScriptCore/Debug.cs` 는 **처분 대상이 아니다.** 한때 "소비자 0" 으로 적었는데
틀렸다 — 저장소 안에서 호출자를 센 것이고, 게임 스크립트에 공개된 API 에 그 수는
죽었다는 증거가 아니다. 경로도 끝까지 살아 있다.

검증 범위: 저장소·화면 상태의 독립 회귀(`verify-log-storage`, Debug·Release 양쪽),
에디터 선언·클리핑·키보드 탐색·상태 행렬 네 게이트, 기동 진단 게이트 19 checks(변이
G0~G4·H1~H3·W1~W2), Editor Release 빌드, 그리고 창을 띄운 눈 확인. 픽셀 골든과
Vulkan 백엔드에서의 확인은 하지 않았다.
