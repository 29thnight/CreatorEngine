# Output Log 저장 경계 정리 — 1단계

날짜: 2026-09-13

## 적용 범위

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

## 저장 계약

- `LogEntry`: 순번, 레벨, 발생 시각, 스레드 ID, 로거명, 본문 원문,
  호출 파일·함수·라인을 보관한다. spdlog의 빌린 문자열은 sink 호출 안에서 복사한다.
- 본문의 개행·탭·연속 공백·UTF-8·NUL 바이트를 저장 단계에서 가공하지 않는다.
- 출처를 제공하지 않은 로그는 라인 0과 빈 출처를 유지한다. 호출 위치를 추정하지 않는다.
- `DebugClass`가 수명 동안 같은 `LogStore`를 유지하고 `LogSink`도 이를 공유 소유한다.
  UI 조회는 초기화/종료 때 교체되는 sink 포인터를 읽지 않는다.
- `Append`, snapshot, `Clear`, 용량 초과 처리는 동일한 store mutex로 보호한다.
  snapshot은 독립적인 값이며 잠금 밖에서 포맷·렌더한다.
- 변경이 없으면 `ReadSnapshotIfChanged`는 빈 optional을 반환한다. 로그가 변경된 경우에는
  현재 보관 목록 전체를 복사한다. **변경분만 전달하는 API는 아직 구현하지 않았다.**
- 순번은 store가 기록을 받아들이는 순서다. Clear 후에도 재사용하지 않는다.
  여러 sink에 걸친 전역 기록 순서까지 보장하는 번호는 아니다.
- `Clear`는 같은 잠금 아래 내용을 지우고 세대/revision을 갱신한다.
  Clear 뒤 들어온 기록은 유지한다. 이전 snapshot도 읽기 가능한 독립 사본으로 남는다.
- mutable reference를 반환하던 `WriteBackLogMessage`와 사용하지 않는 마지막 문자열 API,
  `get_entries`, `IsClear/toggleClear` 화면 신호를 제거했다.

## 보관 한도

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

## 기존 화면 연결

- `OutputLogText.h`가 구조화 필드를 기존 spdlog 기본 표시 형식으로 변환한다.
  저장소에는 화면 표시 문자열을 넣지 않는다.
- 화면은 revision이 변경될 때만 snapshot/표시 문자열을 교체한다.
- 선택과 ImGui 행 ID는 배열 인덱스 대신 발생 순번을 사용한다.
  선택 항목이 제거되거나 Clear되면 선택을 해제한다.
- 기존 `WordWrapText(..., 120)`, `35 * 개행 수`, 경로 정규식,
  Trace 필터 예외는 화면 교체 단계에 남겨 두었다. 문자열 배치 개선 완료를 의미하지 않는다.

## 검증

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

## 다음 단계

1. 그룹 키, 그룹 보존 횟수, 최근 발생 이력, 퇴거 정책과 변경분 조회를 구현한다.
2. Collapse·검색·목록/상세·실측 폭·clipper로 화면을 교체한다.
3. C++/C# 출처 전달과 producer별 수집 경계를 보강한다. 특히 `DebugStreamBuf`의
   공유 문자열 동기화는 이번 store 잠금으로 해결되지 않는다.

현재 검증은 저장소/연결부 독립 회귀와 정적 확인에 한정한다.
Editor 전체 빌드, DX12/Vulkan 실행, 실제 로그 창의 시각·입력 검증은 수행하지 않았다.
