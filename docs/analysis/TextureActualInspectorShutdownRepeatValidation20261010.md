# PHASE 12 — 수정 runtime의 실제 Inspector 입력·종료 반복 (2026-10-10)

<!-- master-doc-review:start -->
## 2026-10-20 master 구현 대조

기준: `27de3f5a02aadde7456aab680db3184c36ee735e`. 대시보드는 변경하지 않는다. 아래 현행 대조와 소스 선언이 이전 시점의 구조·미구현 표기보다 우선한다.

[전체 대조·링크 정리 원장](MasterDocumentationAudit20261020.html)

### 현행 계약과 이 문서의 적용 범위

- DataSystem의 typed 요청은 resolver 세대와 취소/게시 상태를 보존한다. descriptor·payload·이미지 owner와 frame pin은 분리된 수명이다.
- ownership_cpp 기반 자원 소유와 논리적 unmount를 GC 객체 수거·GPU 완료와 혼동하지 않는다. DirectXTex cook와 GPU-ready 소비의 제품 범위는 텍스처/AssetDepot 계획에서 수용한다.

### 해당 구현에서 확인한 선언·소유 경계

- [Tools/regression/start-texture-auto-ui.ps1:9](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/start-texture-auto-ui.ps1#L9) — `$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))`
- [Tools/regression/start-texture-auto-ui.ps1:10](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/start-texture-auto-ui.ps1#L10) — `$fixture=[IO.Path]::GetFullPath($FixtureProject)`
- [Tools/regression/start-texture-auto-ui.ps1:13](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/start-texture-auto-ui.ps1#L13) — `$root=Join-Path $repo ('Build/TAUI-'+$Configuration+'-'+$Mode+'-'+[guid]::NewGuid().ToString('N').Substring(0,8))`
- [Tools/build.ps1:2](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L2) — `[CmdletBinding()]`
- [Tools/build.ps1:4](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L4) — `[ValidateSet('Game')][string]$Target = 'Game',`
- [Tools/build.ps1:21](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L21) — `$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))`

### 이전 근거의 유효 범위

기존 세부 계약은 위 현행 구현과 모순되지 않는 범위에서 유지한다. 과거 수치·실행 통과·해시·장치 조건은 당시 기록이다. 로컬 산출물이 사라진 항목은 링크 대신 경로와 미보존 상태를 표시하며, 현재 master의 재실행 통과로 승격하지 않는다.

<!-- master-doc-review:end -->

<details data-master-history><summary>세부 계약·원래 분석과 당시 측정 기록</summary>

게시 직전 원본 비교 보강 이후의 Debug/Release runtime에서 실제 숫자 입력과
저장 대기 중 외부 설정 변경·종료를 각각3회, 총6개의 독립 Editor 프로세스로 검증했다.
이번 실행에는 제품 소스 변경이나 추가 빌드가 없으며 직전 빌드 산출물을 사용했다.

## 방법과 범위

`start-texture-auto-ui.ps1`로 격리된 Tiny4/SavedFixture 프로젝트를 매번 새로 만들었다.
씬을 열고 UnsavedT0Texture를 생성한 뒤 실제 Windows UI로 fixture.png를 선택하고
Maximum Dimension의0을 선택한 화면을 확인했다. 매회 실제 숫자 키2를 입력했다.

`assets.texture.pendingprobe conflictshutdown`은 입력을 생성하지 않는다. 실제 입력으로
생긴 저장 대기를 관찰한 뒤 400ms 전에 외부 maxDimension1을 쓰고 `RequestQuit`을 호출한다.
Editor의 종료 flush·Finalize를 거쳐 프로세스가 종료될 때까지 기다렸다.
종료 요청은 이 관찰기의 API 호출이며 네이티브 닫기 버튼 반복 검증은 아니다.
이번 외부 변경은 최초 큐 비교 전에 주입되므로 게시 직전 비교 이후 경합의 증거가 아니다.

매회 종료 코드0, 외부 maxDimension1/GUID 보존, 원본 PNG·저장 씬 보존, flush 이후
Finalize와 DX12 pending task/batch/retirement0/0/0, 실행 전후 runtime hash 일치를 확인했다.
화면에는 종료 직전까지 미저장 객체가 있었으며 종료 후 그 객체를 저장했다고 주장하지 않는다.
입력4파일의 전후 hash를 대조해 sourceDrift0을 확인했다.

## 결과

| 구성 | 회차 | 종료 요청 age(ms) | flush age(ms) | 종료 코드 | 실행 루트 |
|---|---:|---:|---:|---:|---|
| Debug | 1 | 3 | 37 | 0 | TAUI-Debug-conflictshutdown-bf370847 |
| Debug | 2 | 9 | 38 | 0 | TAUI-Debug-conflictshutdown-4ac654a3 |
| Debug | 3 | 4 | 35 | 0 | TAUI-Debug-conflictshutdown-be41f9fe |
| Release | 1 | 8 | 30 | 0 | TAUI-Release-conflictshutdown-6dac6d47 |
| Release | 2 | 7 | 35 | 0 | TAUI-Release-conflictshutdown-247dcd1e |
| Release | 3 | 9 | 35 | 0 | TAUI-Release-conflictshutdown-2c343db8 |

전6회에서 위 보존/종료 판정을 통과했다. age는 관찰값이며 성능 수용값이 아니다.

- 최종 receipt·소스 hash (`../../Build/TextureActualUiRepeat/result.json` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조)
- Debug 1 입력 화면 (`../../Build/TAUI-Debug-conflictshutdown-bf370847/before-input.png` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조), Debug 2 (`../../Build/TAUI-Debug-conflictshutdown-4ac654a3/before-input.png` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조), Debug 3 (`../../Build/TAUI-Debug-conflictshutdown-be41f9fe/before-input.png` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조)
- Release 1 입력 화면 (`../../Build/TAUI-Release-conflictshutdown-6dac6d47/before-input.png` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조), Release 2 (`../../Build/TAUI-Release-conflictshutdown-247dcd1e/before-input.png` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조), Release 3 (`../../Build/TAUI-Release-conflictshutdown-2c343db8/before-input.png` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조)

각 루트에 launch.json, commands.jsonl, editor.out/err, exit.json과 격리 프로젝트를 보관했다.
receipt의 root를 따라 개별 로그와 파일을 확인할 수 있다. Build 산출물은 로컬 실행 증거다.

- Debug runtime SHA-256: `B0E1647D2A7A1FB055411B39331B0DED7D600D5ED6AD83C34051C066A177A993`
- Release runtime SHA-256: `4DEAECD2F4F21831243CBECA6C112B7B2520607315F8D88B71EF7876CF09A9B4`

## 수용과 잔여

최신 runtime의 실제 숫자 입력과 제어된 유효 외부 설정 충돌·종료를 구성당3회로 한정 수용한다.
다른 실패 유형·붙여넣기·네이티브 닫기 버튼의 반복, 다중 창/배율, 장시간·성능/누수와
전체 소비자/GPU는 추가 범위다. 마지막 디스크 비교 이후 동시 쓰기도 미해결이다.
12-0-D 후속 검증으로 추가 공수를 중복 계상하지 않으며66.25%(표시66%)를 유지한다.

</details>
