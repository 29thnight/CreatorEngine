# PHASE 12 — 실제 Editor의 5분 자동 저장 경합 반복 (2026-10-10)

## 방법과 수용 경계

격리된 Tiny4/SavedFixture 프로젝트에서 실제 Inspector 숫자0→2 입력의 저장 대기를
관찰하고 정상 저장·재임포트가 끝난 뒤 연속 검증을 시작했다. 이후 입력은 실제 UI
반복이 아니라 같은 `QueueAutomaticTextureSettings` API의 자동 반복이다. 실제 Editor
프레임 루프·자산 서비스·watcher·cook/publication을 유지한 채 실행한다.

회차마다0→2를 연속 queue해 최신 설정으로 병합하고 다음 네 경우를 순환한다.

1. 정상 저장: 최종 maxDimension2 및 오류 없음.
2. 외부 유효 설정1: 대기 편집 거부·외부 bytes 유지 후 baseline 복원/새 자동 저장2.
3. metadata 삭제: 대기 편집 거부·재생성 금지 후 baseline 복원/새 자동 저장2.
4. metadata 손상: 대기 편집 거부·손상 bytes 유지 후 baseline 복원/새 자동 저장2.

네 경우 모두 일반400ms 대기와 명시적 `FlushAutomaticSaves` 경로를 번갈아 거친다.
명시적 flush 반복은 매회 실제 종료/창 닫기를 반복하는 검증이 아니다.
매 회차 원본 PNG·저장 씬을 대조하고 다음 회차와 최종 종료 전에 TextureImportReady를
기다린다. 각 단계30초·전체15분 timeout을 둔다. 연속 구간은300초 이상이며
중간 회차를 완료하고 정상 metadata 상태에서 끝낸다.

끝에서 `object.describe UnsavedT0Texture`가 성공하는지 확인하고, 실제 창 종료 뒤
exit0·Finalize·DX12 pending0/0/0·PNG/GUID/저장 씬/runtime 및 검증 입력 hash를 대조한다.
`verify-texture-auto-ui-soak.ps1`은 receipt와 종료 후 상태를 판정하며 UI 입력을 생성하지 않는다.
제품 저장 구현은 이번 검증에서 변경하지 않았다.

## Debug 결과

| 연속 시간 | 전체 회차 | 정상 | 유효 충돌 | 삭제 | 손상 | 강제 flush |
|---:|---:|---:|---:|---:|---:|---:|
| 300,347ms | 518 | 130 | 130 | 129 | 129 | 258 |

초기 실제 UI 저장 대기 age2ms를 관찰했다. 모든 회차에서 거부/외부 보존/복구를 통과했고
최종 maxDimension2·오류 해제와 미저장 객체 유지, 정상 종료를 확인했다.
강제 flush258회는 전체518회에 포함된 경로 집계로 별도 회차가 아니다.
Debug 라이브러리/runtime/launcher 빌드 및 sourceDrift0을 확인했다.

- Runtime SHA-256: `297C0E8DCC16C3554B60CFC37322502B8DCDF3445A6F9E52939B6CC2554B2905`
- [최종 판정](../../Build/TexturePendingMetadata/soak-debug-result.json)
- [명령 receipt](../../Build/TAUI-Debug-soak-585d7732/commands.jsonl), [로그](../../Build/TAUI-Debug-soak-585d7732/editor.out)
- [실제 초기 입력](../../Build/TAUI-Debug-soak-585d7732/before-input.png), [실행 중](../../Build/TAUI-Debug-soak-585d7732/during-soak.png), [완료 후](../../Build/TAUI-Debug-soak-585d7732/after-soak.png)

## Release 결과

| 연속 시간 | 전체 회차 | 정상 | 유효 충돌 | 삭제 | 손상 | 강제 flush |
|---:|---:|---:|---:|---:|---:|---:|
| 300,147ms | 532 | 133 | 133 | 133 | 133 | 264 |

초기 실제 UI 저장 대기 age15ms를 관찰했다. 같은 회차별 검사를 모두 통과하고
최종 maxDimension2·오류 해제·미저장 객체 유지와 실제 창 종료 후 exit0·빈 종료 큐를
확인했다. 원본 PNG/GUID·저장 씬/runtime 및 검증 입력 hash가 유지됐다.
Release 라이브러리/runtime/launcher 빌드와 sourceDrift0을 확인했다.

- Runtime SHA-256: `FCECFB6A4FF18CE0D0BE05AEDE74CB7B883DB39E1E79FE106FB4E4695D6029EE`
- [최종 판정](../../Build/TexturePendingMetadata/soak-release-result.json)
- [명령 receipt](../../Build/TAUI-Release-soak-6a76283d/commands.jsonl), [로그](../../Build/TAUI-Release-soak-6a76283d/editor.out)
- [실제 초기 입력](../../Build/TAUI-Release-soak-6a76283d/before-input.png), [실행 중](../../Build/TAUI-Release-soak-6a76283d/during-soak.png), [완료 후](../../Build/TAUI-Release-soak-6a76283d/after-soak.png)

두 구성 합계1,050회로 정상263·유효 충돌263·삭제262·손상262회다.
강제 flush 합계522회는1,050회에 포함된다. 회차 수 차이를 처리 성능 비교로 사용하지 않는다.
sourceDrift0은 각 root의 `inputs.json`에 기록한 자산 서비스·pending probe·soak probe·
실행 도우미 네 파일의 실행 전후 hash 대조이며 저장소 전체 변경이 없다는 뜻은 아니다.

## 남는 경계

5분은 이 fixture에 대한 한정된 연속 구간이다. GPU 픽셀·전체 소비자·메모리 누수/성능,
수 시간 실행·임의 외부 프로세스 타이밍·실제 UI 입력/종료 반복·다중 창/배율 및
최종 디스크 비교 이후 쓰기 경합의 수용으로 확대하지 않는다.
Build 증거는 로컬 산출물이며 청소 시 제거될 수 있다.
기존12-0-D 후속 보강으로 추가 공수를 중복 계상하지 않고66.25% (표시66%)를 유지한다.
