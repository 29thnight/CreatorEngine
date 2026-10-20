# PHASE 12 — 저장 대기 중 metadata 삭제·손상 UI 검증 (2026-10-10)

<!-- master-doc-review:start -->
## 2026-10-20 master 구현 대조

기준: `27de3f5a02aadde7456aab680db3184c36ee735e`. 대시보드는 변경하지 않는다. 아래 현행 대조와 소스 선언이 이전 시점의 구조·미구현 표기보다 우선한다.

[전체 대조·링크 정리 원장](MasterDocumentationAudit20261020.html)

### 현행 계약과 이 문서의 적용 범위

- DataSystem의 typed 요청은 resolver 세대와 취소/게시 상태를 보존한다. descriptor·payload·이미지 owner와 frame pin은 분리된 수명이다.
- ownership_cpp 기반 자원 소유와 논리적 unmount를 GC 객체 수거·GPU 완료와 혼동하지 않는다. DirectXTex cook와 GPU-ready 소비의 제품 범위는 텍스처/AssetDepot 계획에서 수용한다.

### 해당 구현에서 확인한 선언·소유 경계

- [Tools/build.ps1:2](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L2) — `[CmdletBinding()]`
- [Tools/build.ps1:4](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L4) — `[ValidateSet('Game')][string]$Target = 'Game',`
- [Tools/build.ps1:21](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L21) — `$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))`

### 이전 근거의 유효 범위

기존 세부 계약은 위 현행 구현과 모순되지 않는 범위에서 유지한다. 과거 수치·실행 통과·해시·장치 조건은 당시 기록이다. 로컬 산출물이 사라진 항목은 링크 대신 경로와 미보존 상태를 표시하며, 현재 master의 재실행 통과로 승격하지 않는다.

<!-- master-doc-review:end -->

<details data-master-history><summary>세부 계약·원래 분석과 당시 측정 기록</summary>

## 검증 방법과 범위

실제 Inspector의 Maximum Dimension 숫자 입력으로 자동 저장을 대기시킨 뒤,
`assets.texture.pendingprobe`가 owner thread에서 400ms 전에 외부 삭제 또는 손상을
주입했다. 숫자 편집과 Reload는 실제 Editor UI 조작이고, 파일 실패 주입은
`.texture-auto-ui-race` 표시가 있는 격리 프로젝트의 `fixture.png.meta`에 한정한다.
검증 모드 `delete`/`corrupt`와 실행 도우미의 해당 선택지를 추가했다.
제품 저장 경로의 구현은 이번 검증에서 변경하지 않았다.

대기 항목이 소진된 후 750ms 동안 외부 상태를 확인한다. 삭제는 파일이 없는 상태,
손상은 주입한 bytes 그대로여야 하며 자동 저장 오류가 있어야 성공이다.
각 모드는 구성별 한 번의 제어된 경합이다. 반복/장시간, 최종 디스크 비교 이후 쓰기,
삭제·손상과 동시에 종료하는 경합, 다중 창/배율, GPU pixel oracle은 포함하지 않는다.

## Debug 결과

| 모드 | 대기 중 주입 시점 | 결과 |
|---|---:|---|
| delete | 9ms | 삭제 상태 유지, 암묵적 재생성 없음, 자동 저장 거부 |
| corrupt | 3ms | 손상 bytes 유지, 자동 저장 거부 |

두 경우 Inspector가 `The source changed on disk; pending edits were not written.`를
표시하고 draft 값2와 미저장 `UnsavedT0Texture` 객체를 유지했다.
정상 baseline metadata를 복원한 뒤 실제 Reload로 디스크 값0을 읽고 오류를 해제했다.
새 숫자2 입력의 자동 저장을 확인했고 PNG/GUID 및 저장 씬은 유지됐다.
정상 Finalize와 DX12 shutdown pending task/batch/retirement `0/0/0`을 확인했다.
삭제·손상 거부 직후의 기존 렌더링 유지 여부는 별도 GPU 판정으로 계상하지 않는다.

Debug Editor 라이브러리/runtime/launcher 빌드는 통과했다. 두 UI 실행의 runtime SHA-256은
`D84ED3C42CDF5762F6D9CD77D18FDF0398D9F4C1AE69FA2CA3F234DD88BB1C56`이며 실행 전후 동일했다.

- Debug receipt (`../../Build/TexturePendingMetadata/ui-debug-result.json` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조)
- 삭제 경합 명령 receipt (`../../Build/TAUI-Debug-delete-d84dfe96/commands.jsonl` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조)
- 삭제 오류 (`../../Build/TAUI-Debug-delete-d84dfe96/pending-rejected.png` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조), draft 유지 (`../../Build/TAUI-Debug-delete-d84dfe96/draft-retained.png` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조)
- 삭제 복구 Reload (`../../Build/TAUI-Debug-delete-d84dfe96/reload-recovered.png` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조), 새 편집 (`../../Build/TAUI-Debug-delete-d84dfe96/fresh-edit.png` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조)
- 손상 경합 명령 receipt (`../../Build/TAUI-Debug-corrupt-84d593b1/commands.jsonl` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조)
- 손상 오류 (`../../Build/TAUI-Debug-corrupt-84d593b1/pending-rejected.png` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조), draft 유지 (`../../Build/TAUI-Debug-corrupt-84d593b1/draft-retained.png` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조)
- 손상 복구 Reload (`../../Build/TAUI-Debug-corrupt-84d593b1/reload-recovered.png` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조), 새 편집 (`../../Build/TAUI-Debug-corrupt-84d593b1/fresh-edit.png` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조)

## Release 결과

| 모드 | 대기 중 주입 시점 | 결과 |
|---|---:|---|
| delete | 7ms | 삭제 상태 유지, 암묵적 재생성 없음, 자동 저장 거부 |
| corrupt | 8ms | 손상 bytes 유지, 자동 저장 거부 |

Debug와 같은 실제 UI 절차로 오류 표시·draft2·미저장 객체 보존, baseline 복원 후
Reload0/오류 해제 및 새 편집 자동 저장2를 확인했다. PNG/GUID·저장 씬을 유지하고
정상 Finalize 및 DX12 pending0/0/0으로 종료했다. Release Editor 라이브러리/runtime/launcher
빌드는 통과했으며 최종 연결은 다른 LTCG 연결과 겹치지 않게 진행했다.
두 실행 runtime SHA-256은
`C7A889DA079940476467389CBCCA8E3DA4D2109205E9C21DA63ED5F00C4C11EC`이며 실행 전후 동일했다.

- Release receipt (`../../Build/TexturePendingMetadata/ui-release-result.json` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조)
- 삭제 경합 명령 receipt (`../../Build/TAUI-Release-delete-babb41a7/commands.jsonl` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조)
- 삭제 오류 (`../../Build/TAUI-Release-delete-babb41a7/pending-rejected.png` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조), draft 유지 (`../../Build/TAUI-Release-delete-babb41a7/draft-retained.png` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조)
- 삭제 복구 Reload (`../../Build/TAUI-Release-delete-babb41a7/reload-recovered.png` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조), 새 편집 (`../../Build/TAUI-Release-delete-babb41a7/fresh-edit.png` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조)
- 손상 경합 명령 receipt (`../../Build/TAUI-Release-corrupt-e78f21c0/commands.jsonl` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조)
- 손상 오류 (`../../Build/TAUI-Release-corrupt-e78f21c0/pending-rejected.png` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조), draft 유지 (`../../Build/TAUI-Release-corrupt-e78f21c0/draft-retained.png` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조)
- 손상 복구 Reload (`../../Build/TAUI-Release-corrupt-e78f21c0/reload-recovered.png` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조), 새 편집 (`../../Build/TAUI-Release-corrupt-e78f21c0/fresh-edit.png` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조)

Build 아래 receipt와 화면은 로컬 검증 산출물이며 청소 시 제거될 수 있다.
이번 증거는 12-0-D 파일 실패·복구 UI 수용의 후속 보강이다. 별도 가중치를 중복
계상하지 않고 13.25/20일 = 66.25% (표시66%)를 유지한다.

</details>
