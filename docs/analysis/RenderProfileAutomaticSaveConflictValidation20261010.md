# PHASE 12 — RenderProfile 자동 저장 경합 실행 (2026-10-10)

## 실제 경로와 검사 범위

`QueueAutomaticRenderProfile`은 GUID를 실제 `.renderprofile` 경로로 해석하고 typed profile을
직렬화한다. 최초 편집 때 저장한 원본을 자동 저장 callback과 공통 publisher까지 전달한다.
이번 검사는 이 공개 queue API, 명시적 flush, 실제 watcher와 DataSystem을 실행했다.
제품 소스 변경은 없으며 검사 host와 프로젝트 include 경로·판정 기준만 보강했다.

일반 저장 대기와 `.tmp` 파일을 관찰한 게시 구간에서 다음 세 외부 상태를 각각 주입한다.

1. 유효 profile 변경: 외부 bytes를 유지하고 대기 편집을 거부한다.
2. 본문 삭제: 파일을 재생성하지 않고 거부한다. watcher의 자산 삭제 정책에 따라
   `.meta`도 제거되는지 기다린 뒤 카탈로그 변경을 반영한다.
3. 손상 YAML: `settings: [`의 주입 bytes를 그대로 유지하고 거부한다.

실패 후 pending 항목 소진·충돌 진단·임시 파일 제거를 확인한다. 본문이 남아 있는 경우
sidecar GUID를 확인한다. 삭제는 GUID 보존 대상 파일 자체가 사라진 상태이므로 기존
자산 쌍을 같은 GUID로 명시적으로 복원한 뒤 새 자동 저장의 정확한 직렬화 bytes와 오류
해제를 검사한다. 삭제했던 자산을 자동으로 복원했다고 주장하지 않는다.

게시 구간의 draft는 `skyboxTextureName`에16MiB 문자열을 사용해 임시 파일 관찰 시간을
확보한다. 이는 실제 skybox 로딩이나 성능 측정이 아니다. `.tmp` 관찰은 마지막 CPU
비교의 정확한 전후를 지정하지 않으며 최종 비교 이후 파일 교체 경합은 여전히 미해결이다.

## 검사 host 보정

처음에는 필요한 Interfaces include 경로와 RenderEngine 타입 등록이 빠져 검사기 빌드/
직렬화가 실패했다. 엔진과 동일한 `register_RenderEngine(Meta::Types())`를 호출하도록 했다.
metadata 전체 bytes 동일성은 정상 watcher timestamp 갱신과 충돌해 GUID 검사로 바꿨다.
본문 삭제 시 sidecar 제거 정책도 반영하고 삭제 이벤트 처리를 끝낸 뒤 자산 쌍을 복원한다.
초기 Release 실행에서는 Windows 삭제 진행 중 sidecar 상태 조회가 Access denied 예외를
던졌다. 오류 코드를 받는 조회로 바꾸고 삭제가 완료된 부재 상태까지 기다리도록 했다.
[검사기 삭제 대기 실패 로그](../../Build/RenderProfileAutomaticConflict/delete-pending-fixture-failure.log).
이 실패들은 최종 수용 검사 횟수에 포함하지 않는다.

## 최종 판정

**전체 회귀 실패 — RenderProfile 외부 내용 덮어쓰기 재현.**

| 구성/경로 | 최종 실행 | 결과 |
|---|---:|---|
| Debug 짧은/긴 경로 | 각2회 | 각149 checks 통과 |
| Release 짧은 경로 | 1회 | 149 checks 통과 |
| Release 긴 경로 | 1회 | 게시 구간 손상 주입에서 실패 |

Release 긴 경로의 마지막 `staged=1, fault=2`에서 `.tmp` 관찰과 외부 쓰기는 완료됐지만
최종 파일이 외부 `settings: [` bytes가 아니라16,779,168 bytes의 대기 profile로 교체됐다.
충돌 거부 로그도 이 마지막 회차에는 없다. 앞선5개 RenderProfile 회차는 복구까지 통과했다.
Release 두 번째 반복은 검사기의 실패 즉시 중단 정책으로 실행하지 않았다.

소스의 원본 재읽기와 `MoveFileExW(REPLACE_EXISTING)`은 분리된 연산이다. 이 사이 외부
쓰기를 배제하는 장치가 없다. 이번 덮어쓰기 관찰은 남겨 둔 조건부 게시 경합을 실제
실패로 확인한 것이며 단순 검사 타이밍 실패로 제외하거나 재시도 성공으로 덮지 않는다.
정확한 CPU 명령 순서는 이 검사로 계측하지 않았다.

- [실패 판정·각 실행 로그·실패 시 소스 hash](../../Build/RenderProfileAutomaticConflict/result.json)
- [실패 로그](../../Build/RenderProfileAutomaticConflict/run-Release-long-1.log)
- [보존된 실제 덮어쓰기 결과](../../Build/RenderProfileAutomaticConflict/overwritten.renderprofile)
- [기대했던 외부 손상 bytes](../../Build/RenderProfileAutomaticConflict/expected-external.renderprofile)

성공한 전체 실행은5회×149=745 checks다. 전체 검증은 실패했으며 최종 sourceDrift 검사까지
진행하지 않았다. 이 부분 성공을 RenderProfile 경합 전체 수용으로 계상하지 않는다.
두 구성의 검사기 빌드는 통과했다. 제품 소스와 기존 Editor runtime은 변경하지 않았다.

## 한계와 잔여

실행 범위는 Editor 자산 서비스와 watcher/cook 경로다. 실제 RenderProfile Inspector UI,
렌더링 설정의 화면/GPU 적용이나 정상 종료와의 동시 경합은 이번 검사에 포함하지 않는다.
기존 Texture 회귀를 함께 실행하지만 GPU 픽셀 수용을 대신하지 않는다.
최종 비교 이후 쓰기는 이번에 실제 재현된 수정 필요 항목이다. 임시 파일 완성/게시 규약과
외부 쓰기 보존을 함께 만족하는 방식을 정한 뒤 같은 실패 회귀를 통과시켜야 한다.
RenderProfile 경합 전체 수용, 다중 창/배율 및 다른 실제 UI 반복 확장은 잔여다.
12-0-D 후속 검증으로 완료 공수를 중복 계상하지 않으며66.25%(표시66%)를 유지한다.

## 후속 수정

이 문서의 실패는 당시 실행의 판정으로 보존한다. 이후 공통 자동 저장의 조건부 게시와
시작 시 원본 복구를 구현하고 Debug/Release 총8회×166 checks 및 제어된 프로세스 종료
복구12 checks를 통과했다. 현재 수용 범위와 경로 부재·UI·전원 차단 검증 한계는
[조건부 게시 수정 기록](ConditionalAssetPublishValidation20261010.md), 정본 계획 §30을 따른다.
