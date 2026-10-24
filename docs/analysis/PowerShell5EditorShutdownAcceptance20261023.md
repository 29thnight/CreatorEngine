# PowerShell 5 Editor 종료 실패 — 원인·수정·수용 (2026-10-23)

## 판정

Windows PowerShell5.1.26100.9711과 PowerShell7.6.5에서 Release Editor/Player의 명령 서비스 종료를 수용했다. 전체 F# 저작·핫리로드 gate도 양쪽에서 exit0으로 통과했다. Script API45와 F# 생명주기·핸들·핫리로드 계약은 유지한다.

이번 실패에는 엔진의 연결 회수 지연과 검사 도구의 종료 코드 누락이 함께 있었다. 전체 CLI 소비자 검사는 별도의 기존 래칫 위반7건 때문에 통과하지 못했다. 대상 종료 회귀와 전체 CLI 완료 판정을 구분한다.

## 확인한 원인

### 연결을 깨우기 전에 기다리던 종료 순서

재현 프로세스의 스택에서 메인 스레드는 `Service::Stop`의 accept 스레드 join을 기다렸다. accept 스레드는 종료 직전 `JoinAllWorkers`를 호출했고, 연결 작업자는 `WSAPoll`에서 유휴 제한 시간까지 기다리고 있었다. Stop의 `ShutdownSocket`은 첫 join 뒤에 있어 실행할 수 없었다.

원래 PowerShell5 실행의 감사 로그는285요청이 동일한 클라이언트 포트1개를 재사용했다. 비교한 PowerShell7 실행은295요청·295포트였다. 따라서 PowerShell5의 HTTP 연결 유지 방식이 엔진의 종료 순서 결함을 드러냈다. 동일한 Keep-Alive 연결을 강제로 유지한 회귀는 두 셸에 모두 적용한다. 수집한 대기 스택 · 로컬 기록, 사이트 미포함 (`Build/Verification/PowerShell5Shutdown/stacks.txt`)과 기존 종료 결과 · 로컬 기록, 사이트 미포함 (`Build/Verification/FSharp/Editor-8cb02c794f3f4e44888443da5da366f7/shutdown.json`)를 보존했다.

순서만 수정한 중간 probe에서도 5초 수신 제한의 약4.90초를 기다렸다. 이 환경에서 `shutdown`만으로 이미 대기 중인 WSAPoll이 즉시 풀리지 않았기 때문이다. 수신 루프가 종료 신호를 확인하도록 함께 바꿔야 했다. 최초의 출력 리다이렉션 가설은 대기 스택과 일치하지 않았다.

### 종료한 프로세스의 ExitCode가 null

진단 실행의 대기를120초로 늘렸을 때 Editor는 실제 종료해 최종 추적 `[9]`까지 남겼지만, PowerShell5의 Process.ExitCode는 null이었다. 독립 네이티브 프로그램을 종료 코드0과23으로 실행해도 핸들을 캐시하지 않으면 둘 다 null로 재현됐다. 같은 호출에서 핸들을 미리 확보하면 양쪽 셸 모두0과23을 정확히 구분했다.

이는 PowerShell 저장소의 [Start-Process 종료 코드 누락 이슈와 핸들 캐시 방법](https://github.com/PowerShell/PowerShell/issues/5421)에 대응한다. null을 성공으로 간주하는 우회는 하지 않았다.

## 수정

- AcceptLoop는 새로운 연결 수락만 끝내고 반환한다. Stop이 모든 연결의 I/O를 중단한 다음 작업자를 join하고, 리스너·endpoint·토큰을 정리한다.
- 연결별 상태가 중단과 최종 close를 mutex로 직렬화한다. 실제 close는 작업자가 담당하고 상태를 무효화하여 이미 닫힌 소켓 번호가 재사용돼도 다른 연결을 중단하지 않게 한다. 정상·파싱 실패·gateway 예외 경로 모두 같은 정리를 거친다.
- Receive는 기존 유휴/요청 제한 시간 안에서 최대100ms씩 poll하며 종료 플래그를 확인한다. 30초 유휴 제한과10초 요청 제한을 줄이지 않는다. 느린 정상 요청이100ms를 넘어도 성공하는 회귀를 포함했다.
- F# 저작·기존 조기 종료 gate는 시작 직후 Process.Handle을 확보한다. 저작 gate는 실제 Play/Stop committed 상태·종료 시간·exitCode·endpoint 제거를 검사하고, 시작/핸들 획득 실패도 관리 출력 복원을 거친다.
- 한글을 포함한 기존 조기 종료 스크립트는 UTF-8 BOM으로 저장하여 PowerShell5의 기본 문자 해석에서도 구문 분석되게 했다. 숨김 실행과 격리 프로젝트·JSON 결과도 연결했다.

## 검증

| 검사 | 결과 |
|---|---|
| 실제 Winsock/HTTP 서비스 | Debug/Release 각각17조건·187검사 통과. 제어 gateway를 사용한 독립 probe이며 GPU 제품 검사는 별도 |
| 종료 조건 | Keep-Alive·아무것도 보내지 않은 연결·부분/느린 요청·완료/오류 연결·gateway 예외·막힌 송신·대기 이벤트 스트림·동일 서비스 재시작8회 |
| 수정 전 canary | 고정 baseline `0847ceeb2ec99d1b4edbdfd9f7f8a94bd773f915`의 서비스로 같은 Keep-Alive 기준을 넘겨 실패함을 확인. 주 작업 트리를 되돌리지 않고 별도 소스 사본 사용 |
| 독립 서비스 Stop | 두 구성 전체 최대140.581ms. 설정한5,000ms 유휴 제한과 구분하며 판정 예산은1,000ms |
| 실제 Editor/Player | 셸2종 × 제품2종 × 반복2회 =8회 exit0. 실행마다 Keep-Alive3개·무전송1개·부분 요청1개를 살아 있게 유지한 채 종료 |
| 실제 호스트 종료 | 8회 최대2,353.367ms, 예산5초. endpoint 제거·Editor 최종 추적·Player 표시/렌더 소비자의 pending0/balanced1 확인. Player 패키지 입력 불변 |
| 전체 F# 저작 gate | PowerShell5/7 모두 생성·훅 없는 Play/Stop·핫리로드·필드37 보존·실패 시 이전 코드/인스턴스 보존·복구 통과. 고정틱51/50회, 종료423.248/500.628ms·exit0 |
| 첫 프레임 전 종료 | 셸별3회, 합6회 exit0. 사용자 기본 프로젝트 대신 격리 프로젝트 사용 |
| 네이티브 종료 코드 | 셸별0/23 정확히 확인. 별도의 핸들 미캐시 PowerShell5 canary는 두 코드 모두 null로 재현 |
| 검증기 구문 | 공유 PowerShell 스크립트5개를 두 셸에서 구문 분석 통과 |
| 빌드 | Release Editor·Player와 기본 F# GameScripts 출력 복원 빌드 통과 |

기계 판정·파일 해시는 [동명 JSON](PowerShell5EditorShutdownAcceptance20261023.json)을 따른다.

주요 제품 근거: Editor/PS5 · 로컬 기록, 사이트 미포함 (`Build/Verification/CommandServiceShutdown/Editor-05c2bcc85a5e4e79b034de6e877db261/result.json`), Editor/PS7 · 로컬 기록, 사이트 미포함 (`Build/Verification/CommandServiceShutdown/Editor-77383e2783c24d8f95e73b6d3227cf0b/result.json`), Player/PS5 · 로컬 기록, 사이트 미포함 (`Build/Verification/CommandServiceShutdown/Player-41aed15c754243df978b4fc598be0e8e/result.json`), Player/PS7 · 로컬 기록, 사이트 미포함 (`Build/Verification/CommandServiceShutdown/Player-8e221651270345a5aa14bb6105aa78a5/result.json`), 저작/PS5 · 로컬 기록, 사이트 미포함 (`Build/Verification/FSharp/Editor-d603cfb8109e4cc29b7f95f4179fd2ca/result.json`), 저작/PS7 · 로컬 기록, 사이트 미포함 (`Build/Verification/FSharp/Editor-e8b281525eec4130bba5b036eecd4e6a/result.json`).

## 배선과 남은 범위

`verify-command-service-shutdown.ps1`은 실제 소켓 회귀와 수정 전 canary를 제공하고, `verify-command-service-shutdown-product.ps1`은 격리한 Editor 또는 cooked Player 사본을 검증한다. 기존 F# 저작 gate와 조기 종료 gate도 그대로 사용할 수 있다. 새 command-service-shutdown workflow에 Debug/Release 서비스 회귀·PowerShell5/7 종료 코드·검증기 구문 검사를 연결했다. 원격 CI는 아직 실행하지 않았다.

기존 `verify-cli-consumer-contract.ps1`은 사람용 판정 문자열을 읽는 기존7파일 때문에 `7 > 0`으로 실패했다. 해당7파일은 모두 HEAD와 동일하며 이번 변경 파일이 아니다. 인증이 모든 라우트보다 앞서는 검사는 통과했다. 래칫 상한은 올리지 않았고 기존 소비자의 이주는 LC9 잔여로 유지한다. 실행 로그 · 로컬 기록, 사이트 미포함 (`Build/powershell5-shutdown-cli-consumer.log`)를 보존했다.

Debug 제품 호스트·Shipping·다른 GPU/플랫폼·일반 GUI·장시간 반복·실제 30/60/144fps 품질의 새 수용을 뜻하지 않는다. 프레임 성능 개선도 주장하지 않는다. 변경은 로컬 작업본이며 커밋·푸시·패키지 공개 게시를 수행하지 않았다.

후속: 위 `7 > 0`은 이 종료 수용 당시의 관측이다. 이후 [CLI 소비자 이주 기록](CliConsumerMigrationAcceptance20261023.md)에서 상한0을 유지하고 위반0을 확인했다. 이 문서의 당시 로그·해시는 소급 변경하지 않는다.
