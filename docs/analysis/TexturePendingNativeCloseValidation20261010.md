# PHASE 12 — 저장 대기 중 WM_CLOSE 종료 경로 (2026-10-10)

## 방법과 변경

제목 표시줄 닫기 버튼은 `EditorWindowChrome.cpp`에서 `PostMessageW(WM_CLOSE)`를 보내며
`App::RegisterHandler`가 이를 `App::Shutdown`으로 연결한다. 기존 CLI `RequestQuit` 종료와
달리 이번 관찰기는 실제 Inspector 숫자 입력으로 생성된 pending을 관찰한 뒤 같은 host
핸들에 WM_CLOSE를 보낸다. 실제 제목 표시줄 클릭을400ms 안에 수행한 검증은 아니다.

`assets.texture.pendingprobe`에 `nativeclose`와 `conflictnativeclose`를 추가했다.
격리 표시가 있는 fixture.png만 대상으로 삼고 입력은 생성하지 않는다. 정상 경로는 대기
편집2의 저장을, 충돌 경로는 외부 설정1의 정확한 bytes 보존과 대기 편집 거부를 검사한다.
실제 Windows UI로 fixture를 선택하고0을 선택한 화면을 확인한 뒤 숫자 키2를 입력했다.

종료 발원지는 기존 로그 시스템에 기록되지만 redirected stdout에 나타나지 않았다.
`App.cpp`에 WM_CLOSE와 CLI quit 수신 stdout 진단을 추가했다. 종료/저장 동작은 바꾸지
않았으며 검증 중 두 경로를 구분하고 수신→flush→Finalize 순서를 확인하기 위한 변경이다.

`verify-texture-pending-native-close.ps1`은 요청/flush age400ms 미만, 성공한 command receipt,
WM_CLOSE 수신과 CLI quit 부재, 충돌 여부, 설정/GUID/PNG/저장 씬 보존, exit0·Finalize·
DX12 pending0/0/0, 실행 전후 runtime hash를 판정한다.

## 결과와 기록

| 구성 | 정상 요청/flush(ms) | 외부 충돌 요청/flush(ms) | 결과 |
|---|---:|---:|---|
| Debug | 7 / 30 | 3 / 41 | 두 실행 exit0·보존·빈 종료 큐 통과 |
| Release | 9 / 33 | 10 / 38 | 두 실행 exit0·보존·빈 종료 큐 통과 |

최종4회에서 WM_CLOSE 수신→pending flush→Finalize 순서를 확인했고 CLI quit 로그는 없다.
정상2회는 설정2를 저장하고 충돌2회는 외부 설정1의 정확한 bytes를 보존했다. 전4회에서
GUID·PNG·저장 씬 보존과 DX12 pending task/batch/retirement0/0/0을 확인했다.
입력7파일 sourceDrift0과 실행 전후 runtime hash 일치를 확인했다. 판정기 hash는 최종
수정본을 별도로 기록한다. Debug/Release 라이브러리·host 빌드도 통과했으며
[최종 빌드 receipt](../../Build/TexturePendingNativeClose/build-result.json)를 남긴다.

Release2회는 잘리지 않은 Inspector를 조작하기 위해 실제 최대화 버튼을 눌렀다. 창 크기
변경 관찰이며 DPI 변경·다중 창·모든 control clipping의 수용 증거로 확대하지 않는다.

[최종 receipt·소스 hash](../../Build/TexturePendingNativeClose/result.json)에 각 실행 루트와
age, 종료 상태, 보존 판정을 남긴다. 각 루트의 launch.json, commands.jsonl, editor.out/err,
exit.json과 격리 프로젝트를 보존한다. 실제 UI 입력 화면은 이번 대화의 도구 캡처로 관찰했다.

첫 Debug 실행 `TAUI-Debug-nativeclose-793935c6`은 요청14/flush32ms·exit0·정상 저장을
관찰했지만 stdout에 WM_CLOSE 수신 로그가 없어 판정기에 실패했다. 계측 보강 후 새
runtime으로 다시 실행하며 이 첫 실행을 최종 수용 횟수에 포함하지 않는다.
판정기의 PowerShell `true` 상수 오기도 `$true`로 수정했다. 이 오류는 엔진 저장 실패가
아니며 고친 판정기로 보존된 실행 결과를 다시 확인한다.

## 수용 경계

실제 Inspector 입력과 제어된 WM_CLOSE의 저장 대기 경합을 한정 수용한다. 메시지 전송과
실제 클릭을 구분하며,400ms 안의 제목 표시줄 클릭·다중 창/배율·다른 실패 유형 반복·
RenderProfile UI/GPU 및 전체 소비자 검증은 남긴다. 한정된 fixture age는 성능 수용값이
아니다. 12-0-D 후속 보강으로 추가 가중치 없이66.25%(표시66%), T1b 대기·T3 중단을 유지한다.
