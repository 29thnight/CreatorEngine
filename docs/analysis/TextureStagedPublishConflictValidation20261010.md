# PHASE 12 — 게시 직전 원본 비교 보강 (2026-10-10)

<!-- master-doc-review:start -->
## 2026-10-20 master 구현 대조

기준: `27de3f5a02aadde7456aab680db3184c36ee735e`. 대시보드는 변경하지 않는다. 아래 현행 대조와 소스 선언이 이전 시점의 구조·미구현 표기보다 우선한다.

[전체 대조·링크 정리 원장](MasterDocumentationAudit20261020.html)

### 현행 계약과 이 문서의 적용 범위

- DataSystem의 typed 요청은 resolver 세대와 취소/게시 상태를 보존한다. descriptor·payload·이미지 owner와 frame pin은 분리된 수명이다.
- ownership_cpp 기반 자원 소유와 논리적 unmount를 GC 객체 수거·GPU 완료와 혼동하지 않는다. DirectXTex cook와 GPU-ready 소비의 제품 범위는 텍스처/AssetDepot 계획에서 수용한다.

### 해당 구현에서 확인한 선언·소유 경계

- [Engine/RenderEngine/DataSystem.h:30](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/DataSystem.h#L30) — `own::shared_owner<const Texture> descriptor;`
- [Engine/RenderEngine/DataSystem.h:44](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/DataSystem.h#L44) — `std::vector<own::shared_owner<const Material>> materials;`
- [Engine/RenderEngine/DataSystem.h:45](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/DataSystem.h#L45) — `std::vector<own::shared_owner<const Texture>> textures;`
- [Tools/build.ps1:2](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L2) — `[CmdletBinding()]`
- [Tools/build.ps1:4](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L4) — `[ValidateSet('Game')][string]$Target = 'Game',`
- [Tools/build.ps1:21](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L21) — `$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))`
- [Engine/RenderEngine/DataSystem.cpp:55](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/DataSystem.cpp#L55) — `for (const auto& entry : file::directory_iterator(directory))`
- [Engine/RenderEngine/DataSystem.cpp:129](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/DataSystem.cpp#L129) — `throw std::runtime_error("Asset image decoder apartment initialization failed.");`
- [Engine/RenderEngine/DataSystem.cpp:4342](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/DataSystem.cpp#L4342) — `if (!MoveFileExW(staging.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))`

### 이전 근거의 유효 범위

기존 세부 계약은 위 현행 구현과 모순되지 않는 범위에서 유지한다. 과거 수치·실행 통과·해시·장치 조건은 당시 기록이다. 로컬 산출물이 사라진 항목은 링크 대신 경로와 미보존 상태를 표시하며, 현재 master의 재실행 통과로 승격하지 않는다.

<!-- master-doc-review:end -->

<details data-master-history><summary>세부 계약·원래 분석과 당시 측정 기록</summary>

## 변경과 수용 경계

기존 자동 저장은 큐를 소진할 때 원본 bytes를 비교한 뒤 callback에서 문서를 파싱하고
임시 파일을 작성했다. 이 작업 중 발생한 외부 변경을 게시 경계에서 다시 확인하지 않았다.
큐가 최초 편집 때 보관한 원본을 callback과 공통 writer까지 전달하고, 임시 파일의
write/flush/close 이후 `MoveFileExW` 직전에 다시 비교하도록 보강했다.
변경이 있으면 임시 파일을 제거하고 기존 자동 저장 충돌 진단을 유지하며 게시를 거부한다.

자동 Texture 설정·일반 import metadata·RenderProfile 저장에 적용했다. 공개 API와
일반 직접 저장의 동작은 유지한다. RenderProfile 경합은 공통 경로의 소스/빌드 확인이며
이번 실행 검사의 직접 대상은 Texture 설정과 일반 import metadata다.

**비교와 파일 교체는 하나의 조건부 연산이 아니다.** 마지막 비교 이후 교체 직전의
동시 쓰기는 여전히 미해결이다. 단순 추가 비교를 원자적인 충돌 해결로 수용하지 않는다.
기존 임시 파일 게시 방식을 유지했으며, 외부 쓰기를 막으려고 파일을 직접 덮어쓰거나
원본을 먼저 다른 이름으로 옮기는 방식으로 바꾸지 않았다.
[MoveFileEx 문서](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-movefileexw)의
`MOVEFILE_REPLACE_EXISTING`은 대상 교체를 지정하며 예상 원본 bytes를 받지 않는다.

## 제어된 실행 검사

`TextureImportLifecycleProbe`의 기존 자산 서비스 회귀에 다음 검사를 추가했다.
실제 Editor UI 없이 Editor archive와 DataSystem/cook/publication을 실행한다.

1. 유효 GUID와 16MiB의 미지 metadata 필드를 가진 원본을 저장하고 자동 편집을 queue한다.
2. 별도 스레드가 `.tmp` 파일을 관찰하면 authoring mutex를 거치지 않고 외부 metadata를 쓴다.
3. 명시적 flush 뒤 외부 bytes·GUID·원본 PNG와 이전 Texture owner 유지, 충돌 진단,
   임시 파일 제거를 확인한다.
4. 정상 metadata 복원 후 새 자동 저장과 오류 해제·재임포트 완료를 확인한다.
5. Texture 설정과 일반 import metadata 두 경로에서 반복한다.

`.tmp` 관찰은 초기 큐 비교 이후의 게시 구간에 도달했다는 증거다. 마지막 CPU 비교의
정확한 전후를 지정하는 동기화 장벽은 아니다. 큰 metadata는 관찰 시간을 확보하기 위한
fixture이며 일반 자산 크기, 저장 성능 또는 임의 외부 프로세스 타이밍의 대표값이 아니다.

수정 전 Debug 실행은 외부 bytes를 보존했으나 교체가 GetLastError=5로 실패하고 일반
게시 실패를 보고했다. 충돌 진단 검사가 실패했다. **이 실행에서 외부 내용 덮어쓰기를
재현했다고 주장하지 않는다.** 수정 전 로그 (`../../Build/TextureStagedPublish/race-before.log` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조).

최초 Release 반복의 마지막 긴 경로는 충돌 거부·외부 보존을 통과했으나 검사 스레드가
이미 작성 완료된 임시 파일을 관찰해 엄격한 크기 조건을 실패했다.
검사 기준을 게시 전 `.tmp` 관찰로 수정하고 전체 검사를 다시 실행했다.
타이밍 조건 실패 로그 (`../../Build/TextureStagedPublish/race-timing-rejected.log` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조).

## 최종 결과

| 구성 | 짧은 경로 | 긴 경로 | 각 실행 검사 | 게시 구간 충돌 주입 |
|---|---:|---:|---:|---:|
| Debug | 2회 | 2회 | 79개 | 8회 |
| Release | 2회 | 2회 | 79개 | 8회 |

총8회×79=632개 검사를 통과했다. 충돌 주입16회는 각 실행의 Texture/metadata 두 경로이며
632개 검사에 포함된다. 기존 잘못된 enum·잠금·손상/비정상 GUID·삭제·cook 실패·이전 세대
복구·supersede·eviction/rehydration·rename 회귀도 함께 통과했다.
입력9파일의 실행 전후 hash를 대조해 sourceDrift0을 확인했다.
최종 판정과 입력/실행 파일 hash (`../../Build/TextureStagedPublish/lifecycle-all-result.json` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조),
전체 실행 요약 (`../../Build/TextureStagedPublish/verify-all.log` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조).

수정된 Debug/Release Editor/runtime/launcher 빌드를 통과했다.
빌드 판정과 runtime hash (`../../Build/TextureStagedPublish/build-result.json` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조),
Debug 빌드 (`../../Build/TextureStagedPublish/build-runtime-Debug.log` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조),
Release 빌드 (`../../Build/TextureStagedPublish/build-runtime-Release.log` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조).
대시보드 전체 파싱/진행률 검사는467 entries, stringViolations0, shapeErrors0,
metaMismatch0 및 render 성공을 확인했다. 변경 범위의 whitespace 검사도 통과했다.

## 남은 작업과 진행률

게시 직전 비교 이후 동시 쓰기와 수정된 runtime의 실제 UI 입력/종료 반복·다중 창/배율은
남긴다. 새 소스에서 이전 UI/소비자/GPU 검사를 다시 실행했다고 주장하지 않는다.
기존 5분 Editor 반복 결과는 수정 이전 runtime의 이력이다.
12-0-D의 후속 보강으로 별도 완료 공수를 중복 계상하지 않는다.
PHASE 12는 13.25/20일, 66.25%(표시66%)를 유지하며 T1b 대기·T3 중단도 유지한다.

</details>
