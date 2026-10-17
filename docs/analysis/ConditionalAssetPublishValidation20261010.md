# PHASE 12 — 조건부 자동 저장 게시와 중간 종료 복구 (2026-10-10)

## 수정 범위

[이전 RenderProfile 실패](RenderProfileAutomaticSaveConflictValidation20261010.md)는 원본 비교와
덮어쓰기 사이의 외부 쓰기를 실제로 재현했다. 기존 실패 로그와 덮어쓴 산출물은 그대로
보존한다. 이번 수정은 자동 Texture 설정, 일반 import metadata, RenderProfile 저장이
공유하는 `WriteBinaryFileLocked`의 expectedDisk 경로에 적용한다. 직접/수동 저장 경로의
덮어쓰기 정책은 변경하지 않았다.

`ConditionalAssetPublish.h`는 원본을 읽기·삭제 권한과 읽기 공유만으로 열고, 같은 열린
핸들에서 기대 bytes를 비교한다. 원본을 `.tmp.autosave-original` 복구 경로로 옮긴 뒤
완성된 staging 파일을 덮어쓰기 없이 게시한다. 잠금 이전 외부 변경은 비교에서 거부하고,
잠금 중 외부 쓰기는 공유 위반으로 실패한다. 원본을 옮긴 뒤 외부 파일이 새로 생기면
게시가 실패하며 그 외부 파일을 보존한다. 게시 실패 시 원본도 덮어쓰기 없이 복원한다.

Editor 자산 서비스 시작 시 metadata 정리보다 먼저 복구 파일을 검색한다. 목적지가
없으면 원본을 복원하고, 목적지가 있으면 그 내용을 우선하며 과거 복구 파일을 정리한다.
복구할 수 없으면 오류와 복구 경로를 남기고 초기화를 실패시킨다.

Windows의 공유 모드와 이름 변경 조건은 [CreateFileW 문서](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew)와
[FILE_RENAME_INFO 문서](https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-file_rename_info)를 따른다.
완성 파일만 게시하지만 원본을 옮기고 새 파일을 게시하는 사이 정본 경로가 잠깐 없을 수
있다. 연속적인 경로 존재나 단일 원자적 compare-and-swap을 보장하는 구현은 아니다.

## 실행 및 판정

최종 실행 결과와 소스/바이너리 hash는 [검증 receipt](../../Build/ConditionalAssetPublish/result.json),
[전체 실행 로그](../../Build/ConditionalAssetPublish/verify-All.log)에 보존한다.

| 구성 | 짧은/긴 경로 | 서비스·게시 검사 | 중간 종료/시작 복구 |
|---|---:|---:|---:|
| Debug | 각2회 | 4×166=664 통과 | exit73 → exit0, 6 통과 |
| Release | 각2회 | 4×166=664 통과 | exit73 → exit0, 6 통과 |
| 합계 | 8회 | 1328 통과 | 12 통과 |

입력11파일의 실행 전후 sourceDrift0을 확인했다. RenderProfile 6유형×8회=48회와
Texture/일반 metadata staging 경합2유형×8회=16회가 위 서비스 검사에 포함된다.
정확한 최종 게시 경계 검사는 각 실행에 포함되며 별도 완료 가중치를 추가하지 않는다.
이번 staging 관찰 반복의 RenderProfile 외부 작업48회는 모두 성공했고 정확한 외부 상태를
보존했다(`externalBlocked=0`). 잠금 거부 분기는 별도 최종 경계의 복구 파일 쓰기 검사로
확인했다. 비결정적 실행에서 잠금 거부가 실제 발생했다고 주장하지 않는다.

Debug/Release Editor 정적 라이브러리와 실행 host 빌드를 통과했다.
[빌드 receipt와 runtime hash](../../Build/ConditionalAssetPublish/build-result.json)를 별도로 남긴다.
현재 수정 후 실제 Inspector UI 실행은 추가하지 않았다.

검사기는 실제 Editor 자산 서비스·watcher·DataSystem을 사용한다. 외부 쓰기가 성공한
경우에는 정확한 외부 bytes 보존을 검사한다. 잠금에 의해 외부 쓰기가 거부된 경우에는
완성된 Editor draft 게시와 오류 부재를 검사한다. 성공한 외부 쓰기의 덮어쓰기는 여전히
검사 실패다. 이전 실패를 잠금 거부로 재분류하지 않는다.

- Texture 설정/일반 metadata: staging 관찰 중 외부 변경, 원본 PNG와 기존 owner 보존,
  pending 소진·임시 파일 정리, 새 편집 복구.
- RenderProfile: 일반 대기/staging 관찰 × 유효 변경/삭제/손상, GUID 정책과 정확한 bytes,
  충돌 진단·새 편집 복구.
- 정확한 원본 퇴역 이후 경계: 복구 파일 쓰기 잠금, 외부 목적지 생성 우선, 정상 게시,
  staging 부재 시 원본 복원, 예외 rollback, 복구 파일 단독/목적지 공존 복구.
- 별도 프로세스: 원본 퇴역 직후 `ExitProcess(73)`로 중단한 뒤 실제 자산 서비스 초기화로
  원본 bytes·GUID·카탈로그 신원을 복구한다. 미게시 staging draft를 채택하지 않는다.

## 수용 한계

이 실행은 자산 서비스와 제어된 프로세스 중단/재시작 검증이다. 실제 RenderProfile
Inspector 입력, 네이티브 닫기 버튼, GPU 화면 적용, 다중 Editor 동시 게시, 전원 차단의
내구성 검증을 대신하지 않는다. 중단 시 남은 미게시 `.tmp` 파일은 watcher에서 제외되며
이번 복구는 해당 draft를 자동 저장하지 않는다. 활성 읽기 핸들이 삭제 공유를 허용하지
않는 경우 자동 저장이 busy 오류로 거부될 수 있다.

12-0의 조건부 게시 실패 회귀를 한정 수용하되 입력/종료 반복 확장·다중 창/배율은
잔여다. 추가 계획 가중치 없이66.25%(표시66%), T1b 대기·T3 중단을 유지한다.
