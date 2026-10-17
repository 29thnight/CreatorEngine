# PHASE 12 — 조건부 게시 수정 후 실제 붙여넣기·종료 검증 (2026-10-10)

## 조작과 판정 경계

[조건부 게시 수정](ConditionalAssetPublishValidation20261010.md)의 Debug/Release runtime을
사용해 격리된 Tiny4/SavedFixture 프로젝트에서 실제 Windows Inspector를 조작했다.
other.png의 Maximum Dimension 2를 Ctrl+C로 복사한 뒤 fixture.png의 0을 선택하고
Ctrl+V로 붙여넣었다. 이후 실행에서도 같은 복사 값을 사용했다. 입력을 생성하는 API
자동화가 아니라 실제 키 조작이다. 매번 선택 필드와 미저장 객체가 있는 화면을 관찰했다.

저장 대기 경합 4회는 `assets.texture.pendingprobe`가 실제 UI 입력으로 생긴 대기를
관찰해 외부 상태를 변경하고 `RequestQuit`을 호출한다. 창 닫기 2회는 실제 제목 표시줄의
X를 클릭했다. 후자는 400ms 저장 대기 안의 클릭이라는 계측이 없어 일반 닫기로만 수용한다.
두 결과를 저장 대기 중 네이티브 닫기 수용으로 합치지 않는다.

## 결과

| 구성/조작 | 경합 요청/flush age(ms) | 종료 코드 | 격리 실행 루트 |
|---|---:|---:|---|
| Debug 붙여넣기·metadata 삭제·RequestQuit | 3 / 20 | 0 | TAUI-Debug-deleteshutdown-840481bd |
| Debug 붙여넣기·유효 외부 변경·RequestQuit | 3 / 31 | 0 | TAUI-Debug-conflictshutdown-6d915afe |
| Debug 붙여넣기·실제 X 닫기 | 미계측 | 0 | TAUI-Debug-manual-8f132f41 |
| Release 붙여넣기·손상 metadata·RequestQuit | 7 / 29 | 0 | TAUI-Release-corruptshutdown-5e416ac4 |
| Release 붙여넣기·유효 외부 변경·RequestQuit | 0 / 32 | 0 | TAUI-Release-conflictshutdown-7e6436f6 |
| Release 붙여넣기·실제 X 닫기 | 미계측 | 0 | TAUI-Release-manual-cfea6d41 |

경합 4회는 외부 상태와 원본 PNG·저장 씬을 보존하고 flush 충돌 거부, Finalize,
DX12 pending task/batch/retirement0/0/0을 통과했다. 일반 닫기 2회는 maxDimension2 저장과
GUID·PNG·저장 씬 보존, Finalize, 동일한 빈 종료 큐를 통과했다. 저장 씬에는 미저장 객체를
추가하지 않았다. age는 이 fixture의 관찰값이며 성능 수용값이 아니다.

- [전체 receipt·파일 판정·runtime/source hash](../../Build/ConditionalPublishActualUi/result.json)
- [저장 대기 종료 경합 검사 receipt](../../Build/ConditionalPublishActualUi/shutdown-result.json)

각 루트의 launch.json, commands.jsonl, editor.out/err, exit.json과 프로젝트를 보관했다.
runtime hash는 실행 전후와 직전 빌드 receipt가 일치한다. 이전 검증에서 저장한 제품
입력11파일과 UI 관찰기/실행기/판정기3파일의 hash를 실행 후 대조해14파일 일치를 확인했다.
이번 실행에 제품 소스 변경이나 추가 빌드는 없다.

최초 Debug 유효 충돌 실행 `TAUI-Debug-conflictshutdown-5d02eeb6`도 실제 Ctrl+V와
요청8/flush48ms·정상 Finalize 로그를 관찰했으나 프로세스가 종료된 뒤 확인해 종료 코드를
기록하지 못했다. 이를 위6회에서 제외하고 종료 코드까지 기록한 별도 Debug 실행으로
반복했다. 로그와 격리 프로젝트는 삭제하지 않았다.

## 수용과 잔여

최신 runtime의 실제 Ctrl+V6회, 제어된 저장 대기 종료 경합4회, 네이티브 일반 닫기2회를
한정 수용한다. 저장 대기 400ms 안의 네이티브 닫기, 실패 유형별 반복 확장, 다중 창/배율,
RenderProfile 실제 UI·GPU 적용과 전체 소비자는 잔여다. 실패를 주입한4회는 최초 원본
비교 전에 변경하므로 마지막 게시 경계 검증을 대신하지 않는다.
12-0-D 후속 보강으로 추가 가중치 없이66.25%(표시66%), T1b 대기·T3 중단을 유지한다.
