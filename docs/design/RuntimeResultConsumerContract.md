# CLI 소비자의 구조화된 결과 계약

2026-10-23 로컬 구현 기준. 명령의 성공 여부는 기존 `CommandResult` JSONL의 `status/code/data`로 판정한다. 종료·저장·렌더 큐처럼 명령 반환 뒤에 일어나는 관측은 별도 런타임 이벤트로 판정한다. 사람용 `message`와 한국어 로그는 진단 자료다.

## 런타임 이벤트

`RuntimeResults.h`가 UTF-8 JSONL을 기록한다. 각 행은 다음 필드를 가진다.

| 필드 | 계약 |
|---|---|
| `schemaVersion` | 정수 `1` |
| `processId` | 생산자 프로세스의 양수 PID |
| `sequence` | 세션 내 `1`부터 연속 증가하는 정수 |
| `event` | 아래 표의 안정된 식별자 |
| `data` | 이벤트별 자료 객체 |

| 이벤트 | 자료와 발생 지점 |
|---|---|
| `session.started` | 경로·로그 초기화 뒤 세션 시작 |
| `editor.quit_requested` | 실제 종료 분기에서 `source: cli/native` |
| `autosave.flush` | 강제 저장 요청의 `path`, 정수 `ageMs` |
| `autosave.completed` | 실제 쓰기 결과의 `path`, 불리언 `succeeded/conflict` |
| `rhi.backend_shutdown` | DX12/Vulkan의 실제 drain 결과: `backend/clean/pendingTasks/pendingBatches/pendingRetirements` |
| `editor.finalized` | Editor Finalize 반환 직전 |
| `jobs.shutdown` | 엔진 작업자 종료 호출이 반환한 뒤 |
| `runtime.finalized` | 공통 런타임 Finalize가 반환한 뒤 |
| `player.text_parser` | 기존 cooked-only 관측 시점의 정수 `calls` |

기본 경로는 호스트의 `Log/runtime-results.jsonl`이다. `CREATOR_RUNTIME_RESULTS_FILE`에 절대 경로를 주면 그 파일을 사용한다. 검증기는 프로세스별 새 경로를 주어 Player의 정상 임시 디렉터리 회수 뒤에도 기록을 읽는다. 생산자는 기존 파일을 덮지 않는다. 검증기는 PID 혼합·순번 초기화·누락·중복을 거부한다.

각 쓰기는 잠금 안에서 직렬화하고 파일을 닫는다. 종료 관측은 spdlog 수명에 의존하지 않는다. 쓰기 오류는 stderr에 남으며, 검증기가 누락된 결과를 사람용 로그로 복구하지 않는다. 매 프레임 기록은 추가하지 않는다.

## Player와 F# probe

기존 `[player.smoke]` JSON의 schema 1에 `processId/scriptsLoaded/modelGenerationFailures/meshResolveFailures`와 재로드 상태를 추가했다. 프레임 수·표시 승격·제출 프레임의 기존 필드는 유지한다. `scriptsLoaded`는 CLR 준비 여부와 별도로 실제 스크립트 어셈블리 로드 결과를 나타낸다.

`PackageSmokeProbe.fs`의 `[package.lifecycle]` JSON은 PID와 `event: package.begin`을 기록한다. 재로드 검증은 정확히 두 개의 Begin 결과와 목적 씬의 활성화·시뮬레이션·렌더 제출을 함께 요구한다.

`Phase22ProductAudioProbe.fs`의 `[audio.acceptance]` JSON은 실제 완료된 재생/완료 수와 GC 값 핸들·World/Session·부착/위치 재생 관측을 기록한다. 소비자는 이전 성공 문구나 고정된 요약 수치를 대신 사용하지 않는다. 기존 사람용 표식은 다른 진단 호출자를 위해 유지한다.

## 소비자와 거부 기준

`Tools/regression/RuntimeResults.ps1`은 두 셸에서 같은 판독기를 제공한다. 필수 자료의 누락, 자료형 변경, 다른 PID/schema, 중복 태그, 불완전한 종료, 남은 큐, 늦은 flush, 잘못된 저장 결과는 오류다. Windows 경로는 절대 경로 정규화 뒤 대소문자를 구분하지 않고 같은 파일인지 대조한다. 강제 저장 경로는 `종료 요청 → flush → 쓰기 결과 → Editor Finalize → 작업자 종료 → runtime Finalize`를 요구한다.

`Write-CommandScript`는 두 셸에서 BOM 없는 UTF-8 명령 입력을 만든다. 명령 소비자는 반환 행 수와 명령 이름/순서를 입력과 대조한다.

`Start-ResultProcess`는 PowerShell 5에서도 자식 환경을 설정한 뒤 부모 환경을 복원하고, 자식이 끝나기 전에 프로세스 핸들을 확보한다. null 종료 코드를 성공으로 처리하지 않는다.

새 필드는 추가할 수 있으나 기존 필드 의미·형식을 바꾸면 생산자, 공통 판독기, 실제 소비자, 양 셸의 거부 회귀를 함께 갱신한다. 오래된 로그만 있는 실행은 새 계약의 수용 근거가 아니다.

## 회귀 실행

```powershell
pwsh -File Tools/regression/verify-cli-consumer-contract.ps1
pwsh -File Tools/regression/verify-runtime-result-consumers.ps1
powershell -File Tools/regression/verify-runtime-result-consumers.ps1
```

`cli_consumer.ratchet.json`의 상한 0을 유지한다. CI는 위 세 검사를 필수 실행한다. 판독기·합성 receipt 검사는 제품 실행과 구분하며, 제품별 인자와 근거는 [수용 기록](../analysis/CliConsumerMigrationAcceptance20261023.md)을 따른다.
