# CLI 소비자 래칫 7파일 이주 수용

> 후속: 아래 실행 당시 남긴 애니메이션 쿠커 차단의 원인·수정과 새 제품 실행은 [AnimationCookAcceptance20261023](AnimationCookAcceptance20261023.md)을 따른다. 이 문서의 이전 실행 결과와 동명 JSON의 신원은 당시 기록으로 보존한다.

2026-10-23 로컬 작업본 기준. `cli_consumer.ratchet.json`을 바꾸지 않고 **상한0 / 관측0**을 확인했다. 이전 종료 수용에서 관측한 `7 > 0`의 후속 작업이다. 전체 LC9 완료로 승격하지 않으며 일반 GUI·서비스/Shipping 잔여와 구분한다.

정확한 소스·바이너리 해시, 실행별 PID/결과/로그 경로는 [동명 JSON](CliConsumerMigrationAcceptance20261023.json)에 기록했다. [결과 계약](../design/RuntimeResultConsumerContract.md)이 현재 생산자·소비자 경계다.

## 변경한 소비자

| 소비자 | 현재 판정 |
|---|---|
| `verify-animation-player.ps1` | Player smoke JSON과 모델 generation/메시 해석 실패 계수, runtime 종료·파서 이벤트. 기존 HTTP 애니메이션 표본 판정 보존 |
| `verify-audio-player-exits.ps1` | F# 오디오 JSON의 실제 재생/완료·형식별 수치와 scope/GC/공간 재생 결과, Player smoke와 종료 이벤트. 요약에 고정 수치를 대신 적지 않음 |
| `verify-content-browser-navigation.ps1` | 기존 명령 JSON을 공통 판독기로 읽고 입력 명령 이름/순서를 대조. 이 파일의 래칫 적발은 소스 검사와 같은 줄에 있던 한국어 진단 문장의 오탐도 포함했으며, 소스 단정은 보존 |
| `verify-player-job-lifecycle.ps1` | 작업자/runtime 종료 이벤트, 목적 씬 활성화·시뮬레이션·렌더 제출 smoke JSON, F# Begin JSON 두 건 |
| `verify-texture-auto-ui-shutdown.ps1` | 명령 receipt와 PID/순번 이벤트의 요청→flush→충돌 거부→Finalize, 기존 디스크 보존 검사 |
| `verify-texture-pending-native-close.ps1` | native 종료 원인과 flush/쓰기 결과/종료 순서, 기존 디스크·GUID·씬 보존 검사 |
| `verify-texture-auto-ui-soak.ps1` | 기존 누적 횟수·시간·최종 상태 자료와 Editor/작업자/runtime 종료 및 RHI 큐 정리 이벤트 |

사람용 stdout을 다른 언어의 문자열 매칭으로 바꾸지 않았다. Editor 저장 결과, 실제 DX12/Vulkan drain, 공통 작업자 종료를 JSONL 생산자에 연결했다. 기존 사람용 표식과 명령 인자는 유지한다. Script API45·OnTickSimulate·생명주기/핸들 소유 계약은 바꾸지 않는다.

## 기존 호출자에서 확인한 수정

- Player 작업자 검증기는 패키지의 `Resources/Environment`를 빠뜨려 실제 렌더 준비가 실패했고, 현재 기본 프로젝트의 GameScripts로 fixture 스크립트를 덮었다. 전체 패키지 closure와 fixture별 Scripts를 보존한 뒤 재로드/정상 종료를 통과했다. 현재 native/common 관리 런타임 overlay는 유지하며 새 패키지 인증으로 확대하지 않는다.
- 브라우저의 isolated A/B 프로젝트에 설정·셰이더·F# 소스 순서가 없어 프로젝트 수용이 실패했다. 각 프로젝트에 필요한 입력을 복사했다. 작은 프로젝트에서도 점진 열거를 실제 자극하도록 트리의 즉시 자식보다 한 단계 아래에 미방문 폴더를 둔다.
- PowerShell5가 UTF-8 명령 파일에 넣은 BOM 때문에 첫 `wait`가 `command.unknown`으로 실패했다. 공통 writer로 BOM 없는 UTF-8을 만들고 행 수·명령 이름/순서를 대조한다. 자식 프로세스 핸들 캐시와 종료 코드 null 거부도 유지한다.
- 브라우저의 Material Graph 수신부·shared texture getter·큐 기반 sprite 편집·폴리지 stable ID 보존을 현재 소스에 맞춰 검사한다. 기존 worker publication mutex를 GT 큐 mutex로 잘못 읽던 별도 텍스처 소스 검사는 admission 범위를 정확히 검사하도록 고쳤다. 제품 잠금은 제거하지 않았다.

## 실행한 회귀

| 실행 | 결과 |
|---|---|
| CLI 래칫 PS5/7 | 모두0파일/상한0. ratchet Git blob이 HEAD와 동일. 인증이 라우트보다 먼저인 기존 검사도 통과 |
| 결과 판독 PS5/7 | **각82검사**, 합164. 필수 자료 누락·중복·잘못된 PID/schema/자료형/순서·남은 큐·저장 오류·모델/스크립트 실패·BOM·명령 불일치·자식 exit0/23 포함 |
| 텍스처 receipt 소비자 셋 | 위 회귀에 포함. shutdown3모드/native close2모드/soak1모드의 합성 자료를 실제 소비자에 전달하고, JSONL이 없으면 각각 거부. stdout 파일 없이 실행하여 로그 fallback 부재 확인 |
| Release 브라우저 PS5/7 | **각136단정**, 표본23·A/B/A 프로젝트 전환3회·드롭 수신15곳. 새 프로젝트·최근 항목·이력 격리와 점진 열거를 함께 수용 |
| 브라우저 소스 변이 | 폴더 검사·sprite 경로·폴리지 ID·Material Graph 경로를 깨뜨린4건 모두 거부 |
| Release Player 작업자 PS5/7 | 셸별 재로드/정상 종료, 합4프로세스 exit0. 목적 씬 composition 제출·Begin 두 번·scheduler/runtime 종료·큐0·파서0·정상 runtime 디렉터리 회수 확인 |
| Release 오디오 Player PS5/7 | 셸별1프로세스, 합2프로세스 exit0. 각각 재생/완료100회·resident50/stream50·MP3 25/FLAC25·scope/GC/부착/위치 재생 JSON 수용. 새로 생성한 유효 mono WAV/MP3/FLAC fixture 사용 |
| Release Editor 저장 생산자 | 실제 텍스처 재가져오기 **315검사** 통과. 정상 자동 저장·RHI drain·Editor/작업자/runtime 종료 이벤트 확인. 기존 Image/Sprite·Undo/Redo·prefab·실패 보존 경로 포함 |
| N1 경계 회귀 | codec/진단226검사, runtime860파일/전이 include1015, 변이35 통과. 런타임 결과 writer 추가가 Archive/Parser 경계를 새지 않음 |
| 텍스처 소스 보조 회귀 | 5검사 통과 |
| 스크립트/문서 | 공유 스크립트13개 양 셸 구문 분석 통과. HTML679페이지·스크립트359개·오류0 |
| 빌드/패키지 | Release Editor·Player 빌드, F# Job/Audio fixture의 SDK 컴파일·cook·PAK·smoke 통과 |

`cli-consumer-contract.yml`에 상한0 검사와 두 셸의 판독 회귀를 필수 연결했다. 원격 CI는 실행하지 않았다. 기본 프로젝트 GameScripts 출력은 F# 두 컴포넌트 상태이며 fixture 빌드는 별도의 SDK/프로젝트 출력으로 수행했다.

## 남은 제품 수용과 경계

애니메이션 소비자의 코드는 이주했지만 새 AnimationPlayer 패키지를 만드는 단계에서 `automatic Scene material compilation`의 Slang `E41012` / `spirv_1_3` capability 오류가 발생했다. 기본·명시 DX12 패키지 시도 모두 cook에서 멈췄다. 실제 애니메이션 소비자 제품 재수용은 완료로 기록하지 않는다. 이 작업에서 쿠커/셰이더의 규칙이나 실패 기준은 낮추지 않았다.

실제 Inspector 입력 뒤의 pending shutdown/native close/5분 soak는 이번에 다시 실행하지 않았다. 합성 receipt 소비자 회귀와 Editor 정상 저장 생산자 수용을 그 실행으로 대신하지 않는다. 오디오도 프로세스별 재생/완료100회이며 **100회 프로세스 반복 종료** 수용은 아니다. 오디오 소비자의 기본 `-Cycles 100`은 유지한다.

Debug/Shipping·Vulkan 제품 실행·오디오 장치/청감 품질·일반 GUI 수동 Undo/Redo·성능 개선을 새로 수용한 기록이 아니다. LC9는 이 잔여를 포함하여 `progress`를 유지한다. 변경은 로컬 작업본이며 커밋·푸시·공개 패키지 게시를 수행하지 않았다.
