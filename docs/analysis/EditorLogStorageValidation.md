# Output Log 저장 경계 정리 — 1·2단계

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
- 실제 로그 창의 시각·입력. 화면은 아직 1단계 그대로이며 그룹을 보여주지 않는다.
- 이 게이트는 여전히 `Tools/regression/run-all.ps1`에 **등록돼 있지 않다.**
  손으로 부를 때만 돈다.

## 다음 단계

1. Collapse·검색·목록/상세·실측 폭·clipper로 화면을 교체한다. Editor 트리를
   건드리므로 `verify-editor-widget-clipping`·`verify-editor-keyboard-nav`·
   `verify-editor-state-matrix`의 닫힌 목록을 함께 고쳐야 한다.
2. C++/C# 출처 전달과 producer별 수집 경계를 보강한다. 특히 `DebugStreamBuf`의
   공유 문자열 동기화는 이번 store 잠금으로 해결되지 않는다.

현재 검증은 저장소/연결부 독립 회귀와 정적 확인에 한정한다.
Editor 전체 빌드, DX12/Vulkan 실행, 실제 로그 창의 시각·입력 검증은 수행하지 않았다.
