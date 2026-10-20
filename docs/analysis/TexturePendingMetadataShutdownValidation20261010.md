# PHASE 12 — metadata 경합과 종료 flush 검증 (2026-10-10)

<!-- master-doc-review:start -->
## 2026-10-20 master 구현 대조

기준: `27de3f5a02aadde7456aab680db3184c36ee735e`. 대시보드는 변경하지 않는다. 아래 현행 대조와 소스 선언이 이전 시점의 구조·미구현 표기보다 우선한다.

[전체 대조·링크 정리 원장](MasterDocumentationAudit20261020.html)

### 현행 계약과 이 문서의 적용 범위

- DataSystem의 typed 요청은 resolver 세대와 취소/게시 상태를 보존한다. descriptor·payload·이미지 owner와 frame pin은 분리된 수명이다.
- ownership_cpp 기반 자원 소유와 논리적 unmount를 GC 객체 수거·GPU 완료와 혼동하지 않는다. DirectXTex cook와 GPU-ready 소비의 제품 범위는 텍스처/AssetDepot 계획에서 수용한다.

### 해당 구현에서 확인한 선언·소유 경계

- [Tools/regression/verify-texture-auto-ui-shutdown.ps1:24](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-texture-auto-ui-shutdown.ps1#L24) — `$flush=[regex]::Match($log,'(?m)^\[AUTOSAVE_FLUSH\] ageMs=(\d+) path='+[regex]::Escape($sidecar)+'\r?$')`
- [Tools/regression/verify-texture-auto-ui-shutdown.ps1:25](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-texture-auto-ui-shutdown.ps1#L25) — `if(!$flush.Success -or !$log.Contains('[AUTOSAVE_CONFLICT] pending edit rejected path='+$sidecar)){throw 'Shutdown flush did not reject the conflicting pending save'}`
- [Tools/regression/verify-texture-auto-ui-shutdown.ps1:31](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-texture-auto-ui-shutdown.ps1#L31) — `'conflictshutdown' {$metadata -match '(?m)^\s*maxDimension:\s*1\s*$' -and $metadata.Contains('33333333-3333-4333-8333-333333333333')}`
- [Tools/build.ps1:2](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L2) — `[CmdletBinding()]`
- [Tools/build.ps1:4](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L4) — `[ValidateSet('Game')][string]$Target = 'Game',`
- [Tools/build.ps1:21](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L21) — `$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))`

### 이전 근거의 유효 범위

기존 세부 계약은 위 현행 구현과 모순되지 않는 범위에서 유지한다. 과거 수치·실행 통과·해시·장치 조건은 당시 기록이다. 로컬 산출물이 사라진 항목은 링크 대신 경로와 미보존 상태를 표시하며, 현재 master의 재실행 통과로 승격하지 않는다.

<!-- master-doc-review:end -->

<details data-master-history><summary>세부 계약·원래 분석과 당시 측정 기록</summary>

실제 Inspector에서 Maximum Dimension을0→2로 입력한 뒤, 저장 대기400ms 전에
격리 metadata를 삭제/손상시키고 같은 owner-thread 관찰에서 `RequestQuit`을 요청했다.
`deleteshutdown`/`corruptshutdown` 검증 모드와 도우미 선택지를 추가했으며 제품의
자동 저장/종료 구현은 변경하지 않았다. 미검증이던 Debug 유효 외부 설정 변경·종료도
기존 `conflictshutdown` 모드로 확인했다. 각 모드는 한 번의 제어된 실행이다.

| 구성 | 모드 | 요청 age | 종료 flush age | 종료 후 외부 상태 | 종료 코드 |
|---|---|---:|---:|---|---:|
| Debug | 삭제+종료 | 10ms | 41ms | metadata 없음, 재생성 금지 | 0 |
| Debug | 손상+종료 | 4ms | 28ms | 주입한 손상 bytes 그대로 | 0 |
| Debug | 유효 변경+종료 | 3ms | 36ms | 외부 maxDimension1/GUID 유지 | 0 |
| Release | 삭제+종료 | 7ms | 27ms | metadata 없음, 재생성 금지 | 0 |
| Release | 손상+종료 | 8ms | 29ms | 주입한 손상 bytes 그대로 | 0 |

다섯 실행 모두 해당 metadata에 대한 `AUTOSAVE_FLUSH` 다음 `AUTOSAVE_CONFLICT`를
기록해 대기 편집2를 쓰지 않았다. 정상 Finalize와 DX12 shutdown pending
task/batch/retirement0/0/0을 확인했다. 원본 PNG 및 저장 씬 bytes가 유지됐고
runtime SHA-256이 실행 전후 일치했다. 종료 전 실제 화면에 미저장 `UnsavedT0Texture`
객체가 있는 상태에서 입력했다. 미저장 객체의 종료 후 재실행 복구 수용은 아니다.
종료 뒤 UI 오류 표시·Reload는 이 실행에서 확인하지 않으며 일반 저장 대기의
[기존 복구 수용](TexturePendingMetadataValidation20261010.md)과 구분한다.

Debug/Release Editor 라이브러리·runtime·launcher 빌드를 통과했다.

- Debug runtime: `102999A2B5A93224D422035D3FD794807D103722B7D5E700A8BBDEAB9A8FCE9A`
- Release runtime: `579EC34DC986CC8E2F51A95A2BB7B057D4C84DC230D40AD42301437D0F7BA63E`
- Debug 결과 (`../../Build/TexturePendingMetadata/shutdown-debug-result.json` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조)
- Release 결과 (`../../Build/TexturePendingMetadata/shutdown-release-result.json` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조)
- Debug 삭제 로그 (`../../Build/TAUI-Debug-deleteshutdown-4457013b/editor.out` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조), 입력 직전 화면 (`../../Build/TAUI-Debug-deleteshutdown-4457013b/before-input.png` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조)
- Debug 손상 로그 (`../../Build/TAUI-Debug-corruptshutdown-2b7983a0/editor.out` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조), 입력 직전 화면 (`../../Build/TAUI-Debug-corruptshutdown-2b7983a0/before-input.png` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조)
- Debug 유효 변경 로그 (`../../Build/TAUI-Debug-conflictshutdown-27f8efb2/editor.out` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조), 입력 직전 화면 (`../../Build/TAUI-Debug-conflictshutdown-27f8efb2/before-input.png` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조)
- Release 삭제 로그 (`../../Build/TAUI-Release-deleteshutdown-89c02646/editor.out` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조), 입력 직전 화면 (`../../Build/TAUI-Release-deleteshutdown-89c02646/before-input.png` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조)
- Release 손상 로그 (`../../Build/TAUI-Release-corruptshutdown-38246217/editor.out` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조), 입력 직전 화면 (`../../Build/TAUI-Release-corruptshutdown-38246217/before-input.png` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조)

`verify-texture-auto-ui-shutdown.ps1`은 실행 root의 launch/command/exit receipt와 로그를
읽어 종료 코드, 요청 age400ms 미만, 해당 파일의 flush/충돌 및 최종 외부 상태,
PNG/저장 씬/runtime 보존과 종료 큐를 검증한다. 실제 입력을 생성하는 스크립트는 아니다.
`exit.json`은 실행 중 해당 프로세스 handle을 열어 WaitForExit 후 기록한 종료 결과다.
Build 증거는 로컬 산출물이며 청소 시 제거될 수 있다.

12-0-D 후속 보강으로 추가 가중치를 계상하지 않는다. 진행률13.25/20일 =
66.25% (표시66%) 및 T1b 대기·T3 중단을 유지한다. 최종 디스크 비교 이후 동시 쓰기,
반복/장시간·다중 창/배율 및 전체 소비자/GPU/품질은 잔여다. 이 제어된 실행은
실제 OS 창 닫기/강제 종료, 임의 타이밍의 스트레스나 성능 수용으로 확대하지 않는다.

</details>
