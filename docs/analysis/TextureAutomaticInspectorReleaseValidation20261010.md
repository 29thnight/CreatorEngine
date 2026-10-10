# Release 자동 Inspector·붙여넣기·저장 대기 경합 검증

2026-10-10, HEAD `b2f918c196d02c648eef5ef064e11ae4f8d7846b`의 로컬 수정 소스로
Release 실제 Editor를 빌드하고 격리 프로젝트에서 입력했다. 실제 복사·붙여넣기,
잘못된 설정 거부, cook 실패·Retry·원본 복구, 저장 대기 중 Inspector 닫기와 정상
종료를 아래 범위에서 수용한다. PHASE 12 전체 완료나 장시간 경합 수용은 아니다.

## 빌드와 입력 수정

실행 파일은 `Bin/x64-Release/Editor/CreatorEditor.exe`, 검증 runtime DLL SHA-256은
`0225A5ABC1A685BC5AA2EDF3D1DA36E4B5EE93410E0E2A62B19C46167387540E`다.
최종 Editor와 host 빌드는 각각
[Editor 빌드 로그](../../Build/phase12-release-ui-clipboard-editor.log),
[host 빌드 로그](../../Build/phase12-release-ui-clipboard-host.log)에서 성공했다.
링커의 Utility_Framework.pdb LNK4020 경고는 남았다. 실행 검증은 성공했지만
해당 라이브러리의 디버그 형식 정보가 완전하다고 주장하지 않는다.

수정 전 숫자 키 입력은 됐지만 실제 Ctrl+C/Ctrl+V는 동작하지 않았다.
WinProcProxy가 Win32 메시지를 PresentationThread에 넘기는 동안 메시지 발생 시점의
키 상태를 보존하지 않아 ImGui Win32 backend의 Ctrl 조회가 달라졌다.
[GetKeyState 문서](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getkeystate)는
호출 스레드의 메시지 큐에 따라 상태가 갱신된다고 설명한다.
메시지와 함께 GetKeyboardState snapshot을 전달하고 consumer에서 SetKeyboardState로
복원하도록 [WinProcProxy.cpp](../../Editor/EngineEntry/WinProcProxy.cpp)를 수정했다.
[SetKeyboardState 문서](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setkeyboardstate)의
범위처럼 호출 스레드의 상태 테이블을 변경한다. 수정 후 실제 Ctrl+A/C와 Ctrl+A/V를
통과했다. 기존 메시지 tuple API와 자동 저장 대기 400ms는 유지한다.

## 실제 UI 결과

Windows UI 입력으로 other.png의 값 2를 복사하고 fixture.png의 초기 값 0에 붙여넣었다.
Enter·저장 버튼·재임포트 버튼·씬 재로드 없이 metadata 2와 Inspector 2 및 씬 표시
변경을 확인했다. 테스트용 미저장 엔티티는 열린 씬에 유지됐다.

| 검사 | 결과 | 근거 |
|---|---|---|
| Release 실제 복사·붙여넣기 | 자동 저장·cook·씬 표시 변경 | [화면](../../Build/TAUI-Release-manual-075e7c23/clipboard-auto-saved.png) |
| Normal Map 없는 BC5 | 오류 표시, 저장 및 실제 Retry 거부; 당시 정상 meta hash 유지 | [화면](../../Build/TAUI-Release-manual-075e7c23/invalid-bc5-rejected.png) |
| PNG 손상과 실제 Retry | 진단과 재시도 오류 표시, 직전 정상 씬 및 미저장 엔티티 유지 | [화면](../../Build/TAUI-Release-manual-075e7c23/failure-retry-retained.png) |
| PNG 원본 복구 | 추가 버튼이나 씬 재로드 없이 Ready 복귀 | [화면](../../Build/TAUI-Release-manual-075e7c23/automatic-recovery.png) |
| 저장 대기 중 Inspector 닫기 | 요청 시 대기 age 0ms, 디스크는 이전 값; 닫힌 패널과 저장된 2 및 씬 변경 관찰 | [화면](../../Build/TAUI-Release-close-84c0265b/pending-close-saved.png), [명령 receipt](../../Build/TAUI-Release-close-84c0265b/commands.jsonl) |
| 저장 대기 중 정상 종료 | 요청 age 7ms·이전 디스크, Finalize에서 age 28ms 대기 저장 처리, 최종 값 2 | [종료 로그](../../Build/TAUI-Release-shutdown-d9d7470b/editor.out), [명령 receipt](../../Build/TAUI-Release-shutdown-d9d7470b/commands.jsonl) |
| 종료 후 같은 프로젝트 재실행 | Inspector 2와 변경된 씬 표시 복원 | [화면](../../Build/TAUI-Release-shutdown-d9d7470b/restart-persisted.png) |

manual·close fixture와 재실행 host는 native 창 닫기로 정상 종료했다.
pending shutdown은 관찰 명령의 RequestQuit으로 정상 Finalize 경로에 들어갔다.
세 fixture 모두 원본 PNG와 GUID가 유지됐고 저장 씬 파일은 초기 내용과 동일했다.
각 종료 및 재실행 종료의 Finalize 완료와 DX12 pending task/batch/retirement 0/0/0을 확인했다.
재실행은 저장 씬을 열었으므로 저장하지 않은 테스트 엔티티는 복원되지 않는다.

## 경합 관찰 방법과 한계

[texture_auto_ui_pending_probe.inl](../../Tools/regression/texture_auto_ui_pending_probe.inl)은
격리 marker와 fixture.png만 허용하는 commandlet 전용 관찰 명령이다.
설정을 만들거나 저장하지 않고 실제 Inspector 입력이 저장 대기열에 들어오기를 기다린다.
대기 age가 400ms 미만이고 디스크가 초기 bytes인 순간 기존 패널 닫기 큐 또는 정상
종료를 요청한다. EditorAssetDatabase의 읽기 전용 age 조회와 종료 flush 로그를 사용한다.
0ms는 millisecond 단위로 버림한 age이며 성능/지연 0을 의미하지 않는다.
패널 닫기 요청 시각을 측정했으며 UI 스레드에서 닫기를 적용한 시각을 별도로 측정하지 않았다.
따라서 native 마우스 종료를 400ms 안에 직접 수행했다는 수용으로 확대하지 않는다.

실제 입력은 Windows UI 조작이고 닫기/종료 경합의 요청 시점만 제어했다.
수용은 작은 4×4→2×2 fixture의 manual 1건, 제어된 close 1건, shutdown 1건이다.
씬 화면 관찰은 독립 GPU pixel oracle이나 전체 소비자 owner 계측을 대신하지 않는다.
대기 후 36000 프레임 명령은 close 화면 관찰을 위한 유지 요청이며 장시간 완료 증거가 아니다.
Debug에는 이번 keyboard-state 수정의 실제 UI 재검증을 수행하지 않았다.

최종 [통합 receipt와 소스 manifest](../../Build/TextureAutomaticReleaseUi/result.json)는
runtime 일치, 세 fixture의 최종 meta/PNG/GUID/저장 씬, pending 명령 결과, 종료 로그와
관련 소스 11개의 hash를 기록한다. [수집 스크립트](../../Build/collect-texture-release-ui.ps1)는
디스크/로그 단정을 다시 검사한다. UI 관찰은 화면과 실행 기록에 의존한다.
Build 아래 실행 산출물은 로컬 증거이며 저장소 배포 산출물이 아니다.

동일 최종 Release runtime으로 `verify-texture-reimport-scene.ps1 -Configuration Release`도
다시 통과했다. ImageComponent/SpriteRenderer 4→2 자동 연결, 미저장 씬 유지,
저장 씬 재로드, cook/reload 실패 보존, 4 복구, Undo 및 프리팹 자동 Apply/Undo/Redo를
확인했다. [명령 receipt](../../Build/TRS-Release-6bbf7e7a/results.jsonl)와
[통합 회귀 결과](../../Build/TextureReimportScene/result.json)에 sourceDrift 0을 기록했다.
UI source manifest 11개도 최종 확인에서 drift 0이다. 문서 갱신 후 대시보드 전체 파싱,
457항목의 문자열/구조 검사 및 phase-meta 검사를 통과했다. 관련 tracked 변경의
`git diff --check` 오류는 0이다.

수정 전 clipboard 재현은 `TAUI-Release-manual-a3871b1b`에 남긴다.
초기 commandlet bootstrap 자동 종료와 일반 script에서 commandlet 전용 명령을 호출한
pilot은 실행 모드 문제로 최종 수용에서 제외했다. 위 표는 최종 runtime으로 수행한 결과만 사용한다.

## 계획 반영

T0/T1a/T2는 진행 중 및 earnedDays 0, T1b는 품질·성능 측정 대기, T3는 제품 타깃
부재 중단을 유지한다. 이번 Release UI·실제 붙여넣기와 제어된 pending close/정상 종료는
남은 작업에서 제거한다. 디스크 충돌, 다중 창·배율, 장시간/반복 경합, Debug 수정 재검증,
모든 소비자·포맷·모델과 GPU pixel/Shipping GPU·품질·성능 수용은 남는다.
