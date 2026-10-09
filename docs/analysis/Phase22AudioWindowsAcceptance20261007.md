# PHASE 22 Windows 제품 acceptance — 2026-10-07

이 보고서는 VS 2026(v18) MSVC v145 x64로 수행한 현재 Windows 검증을 기록한다.
구현 착지, 독립 native probe, 실제 제품 실행, 실제 장치 관찰을 구별한다.
2026-10-08 최종 정산: **AU0~AU9 및 PHASE22 완료.** 최종 clean native generation의
실제 프로젝트 선택 씬 package, 수정 후 실제 Editor 30분과 유효한 stream/ETW 관찰,
Player 프로세스 정상 종료 100/100회가 통과했다. 최종 종료 절이 현재 판정의 정본이다.
초기 실패 기록을 최종 결과와 혼동하지 않도록 아래 후속 정산을 함께 읽는다.

## 초기 Windows 증거 — 후속 수정·보완은 아래 기록 참조

| 검증 | 결과 | 범위와 증거 |
|---|---|---|
| Windows Debug / Release core·decoder·PCM·software-device | PASS, 각각 240 / 240 | `Build/Validation/Phase22Portable/windows-debug` 및 `windows-release/results.json`. 실제 장치 및 Scene/CLR 제품 acceptance와 별개 |
| 실제 WASAPI 짧은 AirPods Max 실행 | PASS, 307 / 307 | `Build/Validation/Phase22Hardware/airpods-preflight.log`. 10초, 재개방 20회 및 runtime 시작·종료 100회. 물리 Bluetooth 해제와 구별 |
| 실제 장치 resident 30분 부하 | PASS, 307 / 307 | `Phase22Hardware/hardware.log`, `results.json`. 0/1/32/128 voices, Hall reverb, 재개방 20회, 시작·종료 100회 |
| 실제 cooked WAV 2개 / MP3 / FLAC | Debug / Release PASS | `phase22-v18-pak-debug.log` 및 `phase22-v18-pak-release.log`. loose/Pak/암호화 Pak cross-chunk·동시 range·bounded read·변조 거부 및 실제 장치 재생 |
| FMOD source/project/deploy 의존 철거 | PASS, 1,765 checks / 0 failures | `phase22-retirement-current.log`. PE/package 게이트와 별개 |
| Windows 전체 제품 빌드 | Release PASS, Debug 전체 빌드 로그 완료 | `phase22-v18-product-release-final.log` 및 `phase22-v18-product-debug-final.log`. VS18 x64 MSBuild. 앞선 병행 빌드 PDB 충돌 실행은 PASS로 세지 않음. Release의 LNK4020 심볼 경고는 별도 미해결 |
| AirPods Max MP3 Stream 30분 | PASS, 167 / 167 | `Phase22Stream/hardware.log`, `results.json`. 실제 CEAC bounded byte source 위 MP3 loop + resident WAV/FLAC, 1/32/128 voices 및 Hall. 이 soak 자체는 실제 Pak 입력이 아님 |
| Packaged Player / 실제 C# 실행 | 오디오 CLR 단정 PASS, 패키지 전체 FAIL | `Phase22Evidence`에 결과 보존. 100 play/pause/resume/stop, 관리 GC 이후 값 핸들 생존, 완료 100개, world/session/attached/positioned 경로 성공 표지. 전체 package는 managed lifecycle count/order mismatch로 게시 실패. 사용자 요청에 따라 별도 프로젝트 방식 중단 |
| 실제 Editor Play 전이 / AirPods 출력 | 전이 100회 PASS; 수정 후 실제 출력 사용자 확인 PASS | `Phase22Editor/lifecycle.log` 및 HTTP 결과. 현재 Dynamic_CPP 프로젝트를 연 실제 Debug Editor의 Play/Stop 확정 100회. 초기 무음 재현 후 기존 sidecar migration, Windows 캐시 재귀, 편집 모드 native simulation 조기 소비를 수정. VS18 Debug 실제 Editor의 physical voice=1 및 사용자 청각 확인 PASS. 수정 후 actual-audio-100.log / actual-100-summary.json: 실제 재생 100회와 정리 100회 PASS, 서로 다른 유효 핸들 100개, Stop 핸들=0, physical voices=1→0, backend failures=0 |
| 이번 수정의 VS18 Editor Debug / Release | build 및 실제 프로젝트 실행 PASS | Debug actual playback 100회, Release actual playback/cleanup smoke 1회. `phase22-v18-editor-audio-status-debug.log`, `phase22-v18-editor-audio-status-release.log`, `actual-release-smoke.log`, `release-final-status.json` |
| 일반·delay PE import / 배포 파일 | Release PASS, 67 images | `phase22-release-pe.json`. 실행 파일/DLL 일반·delay import 및 금지 DLL 이름 검사. 최종 package 게시 성공과 별개 |
| 실제 Editor 기본 출력 20회 / Bluetooth 재연결 | 출력 전환 20회 PASS; 물리 재연결 1회 사용자 관찰 PASS | `actual-default-switch.log/json`: HDMI↔AirPods, 동일 핸들 433791696896 및 physical voice=1 유지, backend failures=0. 사용자가 실제 Bluetooth 재연결 후 소리 자동 복구를 확인. before/after 핸들 유지와 42개 상태 표본 보존. 20회 물리 분리 검증으로 확대 해석하지 않음 |
| 청취 / loopback / 실제 hardware underrun | 수정 후 Intro.wav 청취 및 재연결 출력 복구 사용자 확인 PASS; loopback/underrun 미실행 | 초기 무음은 수정 전 실패 기록이다. 이후 사용자가 정상 출력과 Bluetooth 재연결 후 자동 복구를 확인했다. 3D/reverb 청각 품질과 실제 driver underrun은 별도 미검증이며, 수집하지 않은 값을 0으로 표시하지 않음 |
| 실제 Scene 전환·DDOL·Editor preview/graph authoring | 미실행 | core/stub Scene 통과로 대체하지 않음 |

## 장치와 30분 resident 관찰값

첫 장치는 `CPM1410I (NVIDIA High Definition Audio)` WASAPI였다. 사용자 요청에 따라
기본 playback endpoint를 `헤드폰 (영웅의 AirPods Max)`로 바꿨고, 별도 짧은 native
실행에서 실제 WASAPI 출력 이름을 확인했다. recording/default communications는
바꾸지 않았다. 변경 시각은 `2026-10-07T12:08:58.4339228Z`이며
`Phase22Hardware/default-device-change.json`에 보존했다.

resident 30분 실행의 처음 endpoint는 HDMI이고 실행 도중 기본 출력이 바뀌었다.
따라서 이를 **AirPods Max 단독 30분 측정**으로 표시하지 않는다. 콜백 180,106개,
p99 상한 3.19ms, 최대 25.8093ms, 반 주기 초과 31회, 한 주기 초과 7회,
private commit peak 7,933,952 bytes를 관찰했다. 최소 callback period 10ms의
절반을 p99가 만족하지만, 최대 지연·초과 횟수도 함께 보존한다. 이 시간에는 제품
컴파일도 진행됐다. 한 주기 초과 횟수는 실제 driver underrun 수가 아니다.

AirPods Max 단독 MP3 stream 30분 실행은 callback 179,992개, p99 상한 3.62ms,
최대 56.9439ms, 반 주기 초과 255회, 한 주기 초과 23회, stream read
14,372,127 bytes와 I/O 실패 0을 기록했다. 167/167 단정과 p99 기준은 통과했지만
관찰된 지연을 숨기거나 실제 청각 품질·driver underrun 0으로 표시하지 않는다.

## 이번에 수정한 결함과 낡은 검증

- Windows loose cooked byte source는 같은 읽기 핸들을 mount 수명 동안 유지하고
  `_SH_DENYWR`로 writer를 거부한다. 기존 경로 재개방 방식에서는 open 이후 파일이
  바뀔 수 있었다. mounted source가 모두 해제되면 쓰기가 다시 가능하고, 이후
  payload 변조는 다시 open할 때 hash 검사로 거부되는 것을 native 테스트로 확인했다.
- Audio authoring source identity는 현재 GUID/cooked-only 정책에서 의도적으로 제거된다.
  audio 원본 경로 count를 기대하던 cook stamp 테스트를 현재 계약에 맞췄다.
- 실제 Stream 구현 이후에도 Stream 적재를 거부해야 한다고 주장하던 회귀를 수정했다.
  이제 실제 장치에서 worker의 bounded streaming byte read와 I/O 실패 0을 확인한다.
- 실제 Editor에서 기존 GUID-only Intro.wav sidecar가 import policy 검사에 실패했다.
  초기 스캔은 기존 `.meta`를 등록하기만 해 importer의 migration 경로를 건너뛰었다.
  `audioClip`이 없는 기존 audio sidecar만 normal validated importer로 갱신하고 GUID를
  유지하도록 수정했다. 현재 형식의 잘못된 policy 및 loop markers는 계속 거부한다.
  VS18 Debug 의존 프로젝트 재빌드와 실제 Dynamic_CPP Editor에서 기존 GUID를
  유지한 갱신 및 catalog policy 오류 해소를 확인했다.
- 캐시의 RemoveDirectory 메서드가 Windows 매크로로 RemoveDirectoryW로 바뀌어
  자기 자신을 재귀 호출하던 결함을 수정했다. RemoveChildDirectory로 이름을 바꾸고
  ::RemoveDirectoryW를 명시했다. VS18 DIA로 실제 startup stack을 대조한 뒤
  실제 Editor의 음원 import와 정상 기동으로 검증했다.
- 네이티브 OnBeginSimulation이 편집 모드에서 먼저 소비되어 playOnStart가 Play에서
  실행되지 않던 결함을 수정했다. 네이티브 초기화와 scene 편입은 편집 모드에 유지하고
  시뮬레이션 훅은 Play가 활성화될 때 드레인한다. 실제 Editor capture에서
  active/physical voices=1, playback instances=1, backend/read failures=0,
  output mode=Device(1), stream read=14,081,900 bytes를 확인했다.
  사용자가 수정 후 AirPods Max에서 정상적으로 들리는 것을 확인했다.
  read-only audio.status 명령으로 캡처 캐시를 거치지 않고 현재 보이스·핸들·오류를
  조회한다. 실제 반복 acceptance는 이 진단을 사용해 100개 고유 핸들과 매회
  재생/정리를 확인했다. 이전 profile.frame 기반 반복 시도는 stale capture 값 때문에
  판정 실패했으므로 제품의 보이스 leak 증거로 사용하지 않는다. 실제 증거는 Phase22Editor의
  simulation-fix-frame.json 및 intro-simulation-fixed.ceprof에 보존했다.

## 사용자 요청에 따른 검증 경로 변경

별도 Phase22Player 테스트 프로젝트는 추가 실행하지 않는다. 결과 로그는
`Build/Validation/Phase22Evidence`에 복사했다. 이전 삭제는 자동 승인 검토에서 차단됐지만, 2026-10-08 확인 시 Phase22Player 폴더는 존재하지 않는다. 이후 검증은 현재 `Dynamic_CPP`
프로젝트의 실제 Editor에서 기존 음원과 임시 in-memory SoundComponent로 수행하며,
사용자 씬 파일을 덮어쓰지 않는다. 임시 엔티티는 검증 후 제거했다. 이전 Release Editor 실행은 Stop 상태 및 active/physical voices와 playback instances=0으로 정리했다. 씬 파일은 저장하지 않았다.

## 증거의 한계

독립 hardware wrapper는 직접 포함한 Audio TU/header/fixture 입력 hash를 보존하지만,
컴파일러가 추적한 완전한 transitive dependency ledger는 아니다. resident soak 컴파일
후에 수정된 `CookedAudioClipSource.h`의 Windows pinning을 resident soak가 검증했다고
주장하지 않는다. 해당 변경의 직접 증거는 이후 컴파일한 cooked-source native gate다.
cloud의 ASan/UBSan 통과는 Windows 제품 build/실장치/CLR/LSan 통과로 바뀌지 않는다.

## 2026-10-08 종료 검증 진행

사용자가 승인한 Release 128 physical voices 기준은 audio runtime update p99 <= 1ms,
부하 전 대비 process private memory 증가 <= 256MiB, 첫 재생 API 응답 <= 500ms다.
실제 측정은 HTTP queue와 operation polling을 포함하며 DAC 첫 샘플 지연으로 확대하지 않는다.
Runtime update는 Editor host가 모든 프레임을 1us histogram에 기록한다. 1.024ms 이상
구간의 p99는 overflow로 실패 처리한다. 승인 전 측정을 사후 통과시키는 기준으로 쓰지 않는다.

- VS18 전체 Debug solution build exit 0: `Phase22Closure/full-debug.log`.
- 실제 Debug Editor lifecycle axis gate PASS: 실행한 gate는 Debug exe이며
  결과는 `Phase22Closure/lifecycle-axis-debug.log`다. Release 통과로 세지 않는다.
- 실제 현재 프로젝트 C# playback 100회·completion 100개, GC 이후 값 핸들 생존,
  pause/resume/stop, world/session/attached/positioned PASS. Intro는 NonSpatial stereo라
  spatial 호출은 별도의 현재 프로젝트 PointMono 음원으로 검증했다.
- 실제 scene transfer: World 재생 종료, Session 재생 유지, DDOL entity 유지 PASS.
  Hot reload는 Session sound의 소유자가 아니므로 기존 Session 재생을 유지했고,
  Editor Stop에서 active voices/playback instances가 0이 되었다.
- 실제 SoundComponent 제거 후 1 -> 0 voice/instance PASS.
- 증거: `Phase22Closure/editor-live.out`, `scene-ddol-hotreload.jsonl`.
- Windows Audio GlitchDetection ETW 초기 수집에는 session header만 있었다.
  provider 양성 계측이 확인되지 않았으므로 underrun 0이나 장치 glitch PASS로 세지 않는다.

최종 Release 빌드와 128 voice budget은 아래 결과로 정산했다. 실제 프로젝트 package,
유효한 driver underrun 관찰과 Editor/Player 프로세스 종료 100회는 아직 완료하지 않았다.


SoundGraph 추가 검증 PASS: 실제 현재 프로젝트 Editor의 presentation owner에서 생성·GUID 보존 저장·재열기와 payload 동일성·validate·preview를 수행했다. actual physical voice/instance=1, panel close 후 0, backend/read failures=0. Commandlet 전용 probe라 일반 HTTP에서 부재인 것은 정상이며 Phase22Closure/authoring-results.jsonl에 성공 상태를 보존했다. 실제 마우스 클릭 검증으로 확대하지 않는다.

### 승인한 성능 예산 — 실제 Release Editor 30분 혼합 측정

`Phase22Closure/budget-2f5baac4eace4be0993be0058f245a7a/summary.json`:
현재 Dynamic_CPP의 resident/stream 혼합, 0/1/32/128 voices, reverb on/off를
1,800.876초 실행했다. 모든 runtime update 1,474,278개를 기록했다.

| 항목 | 관찰 | 승인 기준 | 판정 |
|---|---:|---:|---|
| 전체 혼합 Runtime update p99 상한 | 0.284ms | <= 1ms | 혼합 PASS, 128 구간 분리 재검증 |
| 부하에 따른 private memory 증가 | 73.46MiB | <= 256MiB | PASS |
| 첫 재생 API 응답 | 135.15ms | <= 500ms | PASS |
| Callback p99 / 최대 | 3.60ms / 13.764ms | p99 < buffer 절반 | 최대 지연 별도 보존 |
| Callback 반 주기 초과 | 10회 | 실제 underrun과 구별 | 관찰치 |
| Stream read failures | 0 | 오류 0 | PASS |

Backend failures=0, Device output이며 128 구간에서 physical voices=128이다.
전체 혼합 p99를 128 voice 구간의 p99로 사용하지 않는다. 물리 128 voice인 모든
프레임의 1ms 초과 횟수를 별도 누적하여 nearest-rank p99 기준을 재검증한다.

ETW 원시 `ProcessTrace` reader는 24,638,893 events를 읽었고 Audio provider의
KS base-pin event 41을 2건 확인했다. 두 건은 audiodg PID 5104에서
16:02:25.799Z와 16:05:14.385Z에 발생했고, 측정 폴더 생성 16:05:54Z보다 앞선다.
부하 구간에서 해당 이벤트는 관찰되지 않았으나 session eventsLost=1,516으로
완전한 관측이 아니다. driver underrun=0 판정은 미완료다.
`audio-raw-counts.json`, `audio-glitch-details.jsonl`에 원시 수치를 보존했다.

### 실제 씬 수명 및 Play/Stop 잔류

- DDOL SoundComponent를 포함한 실제 scene transfer에서 component는 유지되고 이전
  World playback은 1 -> 0으로 정리됐다. `Phase22Closure/ddol-component.jsonl`.
- Release Play/Stop 준비 10회 후 100회 PASS. 마지막 thread=47, handle=911로
  준비 실행 후 대비 증가가 각각 0이다. 일시 범위 thread=47..48, handle=911..916은
  숨기지 않는다. `Phase22Closure/residue-summary.json`, `residue-100-clean/`.
- 첫 반복 시도는 DDOL 검증 객체의 playOnStart가 함께 실행되어 예상 1이 아닌 2 보이스로
  실패했다. 검증 객체를 명시적으로 제거하고 위 100회 gate를 다시 실행했다.

### 빌드·배포·패키징

- VS18 전체 Release solution exit 0. Utility와 Player의 Release symbol rebuild 후
  Player LNK4020=0. Editor는 새 IntDir에서 다시 컴파일·링크해 LNK4020=0이다.
  `editor-release-fresh-link.log`에 공유 IntDir 관련 MSB8028 경고가 있어 무경고 빌드로 세지 않는다.
- 실제 Release PE closure 67 images PASS (`pe-closure.json`).
- Engine publication 및 runtime hash/license/provenance 검증 PASS:
  build ID `580dd82b-00cc-4995-91fc-da7928a377cd` (`publish-engine.log`).
- 현재 Dynamic_CPP package는 기존 `SFXs_FIXED/SFX_bard_skill.ogg` 때문에 cook exit 3으로
  fail-closed했다. 기존 package는 유지된다. WAV 변환 후보는 decoded PCM hash가 원본과
  같다. 2026-10-08 사용자 지시에 따라 원본·meta를 Library/AudioSourceArchive에 보관하고 GUID 유지 WAV로 변환했다. clean engine의 실제 프로젝트 오디오 cook은 29 clips PASS. 전체 package는 재질·씬의 미해결 dependency GUID 12개로 cook exit 4이며 Player smoke/종료 100회는 실행하지 못했다.
- 별도 테스트 프로젝트를 새로 만들지 않았다. 현재 프로젝트에 검증용 씬·스크립트·음원을
  추가했고 사용자 기존 씬은 덮어쓰지 않았다. 현재 dirty tree의 full build와 publication은
  clean-checkout gate 통과를 뜻하지 않는다.

실제 Release Editor 프로세스 시작·오디오 재생·Stop·정상 종료 **100/100 PASS**:
`editor-exits-cf6e99b8fc094073aa918c98e2b849bc/summary.jsonl`.
각 실행에서 physical voice=1, Stop 후 voice/instance=0, backend failures=0,
exit code=0, PID 종료를 확인했다. Player 100회와 혼동하지 않는다.
clean engine worktree의 오디오 수정만 담은 snapshot `b443aff5`에서 VS18 전체 Release
build가 재실행에서 exit 0으로 통과했다. 첫 실행의 reflgen 설치 후 재실행 요구는
실패 이력으로 보존했다. 이 snapshot의 기존 Dynamic_CPP Editor smoke(1 -> 0 voice,
exit 0) 및 fresh PE closure 53 images도 PASS다. Engine publication build ID
`ad40df63-c75d-4c40-94e1-41bff25cab35`로 runtime/license/provenance를 확인했다.
clean snapshot의 Game package 및 Player smoke까지 통과한 것으로 확대하지 않는다.

### 물리 128 voice만 집계한 CPU 예산 — PASS

전체 혼합 p99와 별개로, 물리 128 voice인 매 프레임의 update duration을 기록했다.
`budget-397357044d944f50b7be3833cc0b102d/summary.json`:
400.604초 혼합 실행 중 128 구간의 **212,798프레임**, **1ms 초과 33프레임(0.01551%)**으로
nearest-rank p99 <= 1ms를 통과했다. 전체 혼합 p99 상한 0.233ms를 128 구간의 정확한
p99 값으로 사용하지 않는다. 추가 memory 69.95MiB, API 응답 114.11ms도 통과했다.
동일 PID의 이전 zero-voice 관찰 최소치부터 peak까지 포함한 보수적 memory 증가도
131.75MiB로 256MiB 이하이다 (`budget-128-cross-run-bound.json`).
callback p99=4.22ms/max=8.953ms/반 주기 초과=22회, stream read failures=0.
이 짧은 보완 측정을 앞선 30분 soak로 바꾸어 보고하지 않는다.

첫 짧은 재검증은 Play 전의 종료된 Session scope로 재생을 요청해 거부됐다.
runner가 committed Play를 먼저 확인하도록 수정한 뒤 위 실행을 통과했다.
검증 후 Stop 상태의 voice/instance=0을 저장했고 해당 Editor도 정상 종료했다.
Editor stderr의 기존 `[profiler] shutdown abandoned=1 retained=1 ... [RHIThread]`는
보존한다. audio worker leak으로 판정하지 않으며 profiler 전체 무경고 종료로도 세지 않는다.

### 당시 남은 종료 조건 — 후속 정산 전

1. GUID 유지 WAV 변환 완료. 원본·meta는 Library/AudioSourceArchive에 보관했다.
   stereo 44.1kHz Float32, 디코딩된 PCM hash 동일. Assets 내 OGG는 0개다.
2. 선택 씬의 clean engine runtime Game package·Player smoke 통과. 로컬 변경 BuildTool을 포함한 clean snapshot 재검증 및 실제 Player 종료 100회가 남는다. 전체 프로젝트의 미해결 GUID 12개는 별도 콘텐츠 정리 항목이다.
   `verify-audio-player-exits.ps1`의 수정 후 종료 canary 1회 PASS. 종료 100회는 아직 통과하지 않았다.
3. 실제 underrun을 유실 없이 관찰하는 유효한 계측. 기존 ETW는 부하 구간에서 event 41을
   관찰하지 않았지만 전체 eventsLost=1,516이므로 underrun 0 수용으로 세지 않는다.

AU5/AU6/AU7은 완료, PHASE22 전체는 아직 완료하지 않았다.


### 2026-10-08 WAV 적용 및 후속 검증

- SFXs_FIXED/SFX_bard_skill.wav로 변환, GUID 258e717d-89ba-42e9-a4bb-a80c3b32aa26 유지.
- 원본 OGG·meta는 Dynamic_CPP/Library/AudioSourceArchive/SFX_bard_skill-d3ec9068f112479c957510c34a18f3f7에 보관하고 해시를 검산했다.
- 44.1kHz stereo Float32, decoded PCM SHA-256 동일. 활성 Assets OGG 0개.
- 별도 프로젝트 없이 현재 Dynamic_CPP/Assets를 clean engine AssetCooker로 오디오 변환: exit 0, audioClips=29, soundAssets=1. 기존 GUID의 .ceac 산출물 확인.
- 전체 실제 프로젝트 package는 OGG 오류가 사라진 뒤 재질·씬의 12개 미해결 dependency GUID로 cook exit 4. 기존 package 유지, Player 검증 미실행.
- 증거: Build/Validation/Phase22Closure/ogg-migration/applied.json 및 verification.json, audio-wav-cook.log, package-wav-console.log.

### 2026-10-08 선택 씬 패키징

사용자 지시로 기존 Test1/Test2 대신 현재 프로젝트의 Phase22ProductAcceptance.creator를 선택했다. 시작 씬 지정만으로는 전체 프로젝트 cook이 유지되므로 BuildTool에 명시적 --asset-list 입력을 추가했다. 원본 씬·재질을 수정하거나 별도 프로젝트를 만들지 않았다.

- 목록: Build/Validation/Phase22Closure/audio-scene-assets.txt. 선택 씬 1개, 음원 4개(Intro/Spatial/Stream/변환 WAV), 기본 pass shader 파일을 포함한다. 기존 sidecar 자동 포함, 프로젝트 전체 C# 컴파일, 선택 콘텐츠의 dependency 검증은 유지한다.
- actual package: Build/Validation/Phase22Closure/Stage/Game-5a6563fff6b64d3289515c6418685ab3. verification=passed, cooked scene 1, models=0, cooked entries=11, text parser calls=0.
- Player: GT 654 frames, presentation promotions 120, exit 0. AUDIO_CLR_PASS playback/completion 각 100, GC value handle, World/Session, attached/positioned 통과.
- native runtime/cooker/packer는 clean publication ad40df63-c75d-4c40-94e1-41bff25cab35. 패키징 도구는 현재 변경한 로컬 BuildTool이다. 전체 프로젝트 package와 clean snapshot 전체 도구 재빌드 완료로 확대하지 않는다.
- 선택 목록 경로 탈출, 없는 파일, 시작 씬 누락 거부 3/3. BuildTool Release build 경고/오류 0.
- 첫 Player 종료 runner는 PID별 RuntimeData/Log를 읽지 않아 정상 exit 0을 실패로 오판했다. 파일 로그 및 readiness JSON 검증을 추가했다. 이 첫 시도는 통과 100회로 세지 않는다.

수정 후 실제 Player 종료 canary 1/1 PASS (player-exits-613b3753a07d43959c593ac715c4a38e/summary.jsonl). 이 결과는 프로세스 종료 100회 완료가 아니다. 원본 Project/Scenes와 재질은 변경하지 않았다.

### 2026-10-08 최종 clean generation 및 계측 보완

- engine source `3b64ea9621592a85e1e5c0cebf92a231846f0acb`: 관련 오디오 수정과 선택 목록 BuildTool만 포함한 clean checkout. VS18 Release 전체 solution 재빌드 exit 0, PE import 53 images PASS, source/binary retirement 2,100 checks / 0 failures. 첫 reflgen 설치 후 재실행 요구는 실패 이력으로 보존했다.
- 최종 publication `aa012ebe-4428-467e-ab13-f49ee5b71999`: runtime identity/license/provenance PASS. `final-clean-release-retry.log`, `final-clean-release-canary.log`, `final-clean-pe-closure.json`, `final-publish-engine-console.log`, `final-retirement.log`.
- 최종 actual Dynamic_CPP 선택 package `FinalStage/Game-0c3e1057609742bca7b2f8df4fc57964`: 선택 씬 1, WAV 4 + MP3 Stream + FLAC Resident, shader metadata 6. Player 624 GT frames / 120 presentation promotions / text parser 0 / exit 0. 실제 C# 100 playback/completion 중 resident/stream 각 50, MP3/FLAC 각 25. 별도 프로젝트 없음. `final-compressed-package.log`.
- 배포본 Editor는 actual project의 스크립트를 공식 `compile-game`으로 `Dynamic_CPP/Library/Phase22Managed`에 빌드하고 `--managed-root`로 로드했다. 배포본 manifest와 파일은 수정하지 않았다. 초기 기본 경로의 GameScripts 부재로 static invoke/reload가 거부된 기록은 정상 재생으로 세지 않는다.
- stream observer는 mixer가 읽는 private data source를 감싸 MA_BUSY/MA_NO_DATA_AVAILABLE을 계수한다. 원래 resource-manager 소유권과 loop/range/seek forwarding을 유지하고, sound detach/drain 후 observer를 해제한다. 공개 vendor 타입은 추가하지 않았다. `streamPcmReads` 및 `streamStarvationReads`를 owner diagnostics/profiler에 게시했다.
- 정확한 read 경로에서 busy→정상 회복→EOF positive canary 통과. EOF는 starvation이 아니다. 새 VS18 Debug/Release native 회귀 각각 240단정 / 0실패 (`stream-observer-debug-gate.log`, `stream-observer-release-gate.log`).
- 선택적 Windows Audio ETW event 24~49 필터는 실제 audiodg event 33을 canary에서 검출했다. capture/read exit 0, event/buffer loss 0. 이 canary의 event 33은 실제 실패 관찰이고 workload underrun 0 증거가 아니다. `audio-etw-filter-validation.json`, `audio-etw-filter-canary-glitches.jsonl` 계열 증거를 따른다. 필터의 포함 semantics는 [Microsoft EVENT_FILTER_EVENT_ID](https://learn.microsoft.com/en-us/windows/win32/api/evntprov/ns-evntprov-event_filter_event_id)를 따랐다.

**입력 구성 정정:** Intro.wav는 Auto이며 실제로 Stream을 선택한다. 따라서 Intro와
Phase22Stream을 섞은 이전 측정의 resident/stream 혼합 표시는 철회한다. 관찰된 CPU
수치는 해당 workload의 기록으로 남기며 최종 구성 수용으로 재사용하지 않는다.
새 `budget-0615919890424c51b916f9e7b98350cc`도 같은 원인으로 중단·제외했다.
명시적 Resident인 Phase22Spatial을 사용하는 실제 Editor canary에서 1 voice의
stream reads=0, 32 mixed voices의 stream reads>0을 확인한 뒤 새 프로세스로 재검증한다
(`residency-canary.json`). 기존 Player marker의 resident=50 표기도 실제 구성 증거로
재사용하지 않고 수정 후 MP3/FLAC 포함 package 결과만 채택한다.

기존 native generation의 Player 종료 runner는 26회 통과 후 종료했으며 최종
100회에 합산하지 않는다 (`player-exits-old-generation-stopped.json`).
최종 입력·스크립트·도구 및 package identity는 `final-validation-identities.json`에 기록했다.
최종 30분 및 Player 100회 결과가 확정되기 전 AU4/AU9는 완료로 표시하지 않는다.

### 수정 후 30분 제품·스트림·ETW 수용 — PASS

실행 시간 1800.827초, HTTP 상태 표본 1,742개. 최종 배포본 Editor의 actual Dynamic_CPP
명시적 Resident/Stream 혼합 0/1/32/128 voices 및 reverb on/off다.
`budget-b69f9f06dd2143cd8f20e84f7224925f/summary.json` 및 `samples.jsonl`:

| 조건 | 최종 관찰 | 판정 |
|---|---|---|
| 128 physical voices update p99 <= 1ms | 943,138프레임 중 >1ms 93회(0.00986%) | PASS. 전체 혼합 p99 상한 0.203ms는 128 전용 정확한 p99로 쓰지 않음 |
| 추가 private memory <= 256MiB | zero-voice 최소 1,202,987,008 bytes → 전체 peak 1,314,320,384 bytes, 증가 106.1758MiB | PASS. 첫 순간 baseline 증가 1.875MiB는 최종 memory 근거로 쓰지 않음 (`conservative-memory.json`) |
| 첫 재생 API 응답 <= 500ms | 127.9356ms | PASS. HTTP queue/poll 포함, DAC 첫 샘플 지연은 아님 |
| callback p99 < buffer 50% | 180,694회; p99 상한 3.80ms, max 7.3796ms, 반 주기 초과 19회 | PASS. 초과 횟수 및 max 보존 |
| 실제 mixer stream read 관찰 | PCM read 12,099,284; decoded-page starvation 0; I/O read failures 0 | PASS. 정상 EOF는 starvation이 아님 |
| 독립 OS trace | 선택 Audio event 26~49 glitch 관찰 0, event/buffer loss 0 | PASS. 아래 trace 범위와 positive canary를 함께 적용 |
| 출력·정리 | backend failures 0, 전체 표본 실제 outputMode=Device; Stop voice/instance 0, Editor exit 0/PID 종료 | PASS |

trace: `final-underrun-0ff2f487400f40758f1bfc361c194955/acceptance.json`.
Capture stop/read exit 0, eventsLost/logBuffersLost/realTimeBuffersLost 모두 0.
선택 Audio 이벤트 자체는 0개이며, 해당 provider/filter가 실제 audiodg event 33을
유실 없이 검출한 앞선 canary와 함께 계측을 수용했다. 관찰 창·필터 밖 오류가
존재하지 않는다는 보장으로 확대하지 않는다. callback 최대/초과 횟수를 driver
underrun 횟수로 바꾸지 않는다.

AU4는 완료. 최종 Player 프로세스 종료 100회만 AU9 잔여이며, 이전 generation의
26회나 playback 100회를 프로세스 종료 100회에 합산하지 않는다.

### 최종 Player 종료 100회 및 PHASE22 종료 — PASS

`player-exits-af8a3b956d3b4185955046accf67999f/summary.jsonl`의 최종 package
Player 프로세스 시작·실행·종료 **100/100 PASS**. 매 프로세스에서:

- C# playback/completion 각각 100; 명시적 resident/stream 각각 50, MP3/FLAC 각각 25.
- GC 이후 값 핸들, pause/resume/gain/stop, World/Session 및 attached/positioned 호출 통과.
- 최소 600 GT frames 및 presentation promotions 120, readiness true, submitted frames > 0, cooked text parser calls=0.
- exit code 0, crash/CLR 실패 표지 없음, 해당 PID 종료. 100회 뒤 남은 Player 없음.

ledger 100개의 연속 cycle 번호, exit/retirement 및 각 검증 수, 동일 engine build ID를
재대조했다. 최종 GameAssets.pak SHA-256도 manifest와 일치한다.
`final-phase22-acceptance.json`은 Editor의 `final-editor-acceptance.json`과 이 ledger를
합친 최종 판정이다. **AU0~AU9 done, PHASE22 종료.**

별도 테스트 프로젝트를 만들지 않았으며 사용자 원본 Test1/Test2·재질은 그대로다.
전체 콘텐츠 cook의 미해결 GUID 12개는 별도 정리로 남긴다. 로컬 clean engine 및
선택 package 수용은 원격 CI 실행 완료와 PHASE23 MSI 수용을 뜻하지 않는다.
B5의 오디오 선행 차단을 해제하되 실제 CI 게임 레그는 후속 작업이다.

검증용 managed engine checkout은 Git snapshot을 보존하고 archive했다. 실제
Dynamic_CPP 및 최종 배포본·package·검증 로그는 그대로 보존했다. archive 후에도
최종 배포본의 공식 verify-engine이 통과했다. 별도 Phase22Player 폴더와 실행 중인
검증 Editor/Player/ETW capture는 없다. root HEAD는 `3afe1daa`로 유지했으며
root commit/push는 수행하지 않았다. 대시보드 AU0~AU9 10/10 done, JavaScript
syntax 및 관련 문서/runner diff formatting 검사를 통과했다.
