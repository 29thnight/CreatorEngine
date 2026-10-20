# PHASE 12 — Debug Inspector 파일 실패·복구 검증

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
- [Tools/regression/verify-texture-reimport-scene.ps1:5](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-texture-reimport-scene.ps1#L5) — `$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))`
- [Tools/regression/verify-texture-reimport-scene.ps1:15](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-texture-reimport-scene.ps1#L15) — `$root = Join-Path $repo ('Build/TRS-' + $config + '-' + [guid]::NewGuid().ToString('N').Substring(0,8))`
- [Tools/regression/verify-texture-reimport-scene.ps1:24](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-texture-reimport-scene.ps1#L24) — `[IO.File]::WriteAllText($texture + '.meta', "guid: 33333333-3333-4333-8333-333333333333n")`
- [Engine/SceneRuntime/ImageComponent.h:14](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/ImageComponent.h#L14) — `class [[reflgen::reflect]] ImageComponent : public meta::identity<ImageComponent, UIComponent>`
- [Engine/SceneRuntime/ImageComponent.h:19](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/ImageComponent.h#L19) — `void gc_trace(gc::tracer& tracer) const override`
- [Engine/SceneRuntime/ImageComponent.h:21](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/ImageComponent.h#L21) — `UIComponent::gc_trace(tracer);`
- [Tools/build.ps1:2](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L2) — `[CmdletBinding()]`
- [Tools/build.ps1:4](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L4) — `[ValidateSet('Game')][string]$Target = 'Game',`
- [Tools/build.ps1:21](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L21) — `$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))`
- [Engine/SceneRuntime/ImageComponent.cpp:19](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/ImageComponent.cpp#L19) — `ImageComponent::ImageComponent()`
- [Engine/SceneRuntime/ImageComponent.cpp:24](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/ImageComponent.cpp#L24) — `bool ImageComponent::ReplaceTextureOwner(const Texture* previous,`
- [Engine/SceneRuntime/ImageComponent.cpp:25](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/ImageComponent.cpp#L25) — `const own::shared_owner<const Texture>& replacement)`

### 이전 근거의 유효 범위

기존 세부 계약은 위 현행 구현과 모순되지 않는 범위에서 유지한다. 과거 수치·실행 통과·해시·장치 조건은 당시 기록이다. 로컬 산출물이 사라진 항목은 링크 대신 경로와 미보존 상태를 표시하며, 현재 master의 재실행 통과로 승격하지 않는다.

<!-- master-doc-review:end -->

<details data-master-history><summary>세부 계약·원래 분석과 당시 측정 기록</summary>

실행: 2026-10-10 KST. 소스 HEAD `d3c85faf182e93842217dd46eee3d3aabf4afc13`.
현재 Debug Editor에서 실제 Ctrl+C/Ctrl+V, Reload Disk Settings, Retry Import를 조작했다.
기존 Debug runtime을 실행했으며 이 기록은 새 빌드 완료를 주장하지 않는다.
runtime SHA-256은 `B286C2E2BCBB5626DFC7DEF2120CFB18940FA2D5D7E5D9D52723317702559FD0`이며
두 UI 실행 전후 동일했다. 제품 코드는 이 검증에서 수정하지 않았다.

## 실행 결과

| 경계 | 실제 조작 및 관찰 | 판정 |
|---|---|---|
| Debug 수정 후 붙여넣기 | other.png의 최대 크기 2를 Ctrl+C로 복사하고 fixture.png의 0에 Ctrl+V로 붙여넣음 | 통과 |
| 저장 대기 외부 충돌 | 실제 UI 입력 후 대기 age 3ms에 probe가 외부 설정 1 주입. 자동 저장 거부와 Inspector 오류 표시, 외부 bytes 보존 receipt | 통과, 일반 저장 1건 |
| 잠긴 metadata 재읽기 | FileShare.None 잠금 중 실제 Reload 클릭. failed to open authoring document 표시, 잠금 해제 후 외부 bytes hash 동일 | 통과 |
| 손상 metadata 재읽기 | 격리 sidecar를 닫히지 않은 YAML 배열로 교체 후 실제 Reload 클릭. missing terminating ] 표시, 편집 값 2 유지 | 통과 |
| Retry 오류 해제 | 정상 외부 bytes 복원 후 실제 Retry 클릭. 자동 저장 오류와 로컬 파싱 오류 해제, Cooked generation ready 및 metadata 2 | 통과 |
| 잠금 중 자동 저장 | 선택해 읽은 초안에 파일 잠금을 걸고 실제 Ctrl+V. 자동 저장 실패 표시, 잠금 해제 후 baseline bytes 동일 | 보존·복구 통과, 진단 문구 개선 잔여 |
| Reload와 새 편집 복구 | 잠금 해제 후 Reload로 디스크 0을 읽고 오류 해제. 새 Ctrl+V 입력 뒤 자동 저장으로 metadata 2 | 통과 |
| 미저장 씬 보존·정상 종료 | 두 실행 모두 UnsavedT0Texture가 UI에 유지됨. 저장 씬·원본 PNG bytes와 GUID 보존, Finalize 완료 및 DX12 pending 0/0/0 | 통과 |

현재 Debug 소비자 회귀도 별도로 통과했다: `verify-texture-reimport-scene.ps1 -Configuration Debug`,
248 checks, sourceDrift 0. ImageComponent/SpriteRenderer의 자동 4→2 연결, 미저장 씬·Undo,
cook/reload 실패 보존 및 4 복구가 포함된다. UI 화면만으로 GPU 픽셀 정확도나 품질을 판정하지 않는다.

## 재현 및 로컬 증거

격리 fixture 준비와 Editor 실행을 저장소의
[`start-texture-auto-ui.ps1`](../../Tools/regression/start-texture-auto-ui.ps1)에 보존했다.
먼저 소비자 회귀로 만든 `Build/TextureReimportScene/result.json`의 project를 FixtureProject로
전달하고 conflict 또는 manual 모드로 실행한다. 준비 명령은 씬·브라우저만 열며 값 편집과
Reload/Retry는 실제 UI 입력이다. 외부 충돌 probe와 파일 잠금·손상은 제어된 실패 주입이다.
manual 실행의 첫 잠금은 선택 전에 걸려 Inspector를 읽지 못했다. 잠금을 해제해 선택한 뒤
다시 잠그고 편집한 실행만 자동 저장 실패 수용에 사용했다.

- 집계 receipt (`../../Build/TextureAutomaticInspectorDebugFailure-result.json` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조)
- Debug 소비자 회귀 (`../../Build/TextureSidecarDiagnostics/previous-scene-result.json` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조)
- 외부 충돌 receipt (`../../Build/TAUI-Debug-conflict-6d2c7780/commands.jsonl` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조)
- 잠긴 파일 Reload (`../../Build/TAUI-Debug-conflict-6d2c7780/locked-reload.png` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조)
- 손상 후 편집 값 2 (`../../Build/TAUI-Debug-conflict-6d2c7780/corrupt-draft.png` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조)
- 손상 파일 Reload (`../../Build/TAUI-Debug-conflict-6d2c7780/corrupt-reload.png` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조)
- Retry 정상 상태 (`../../Build/TAUI-Debug-conflict-6d2c7780/retry-recovered.png` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조)
- Retry 오류 해제 (`../../Build/TAUI-Debug-conflict-6d2c7780/retry-errors-cleared.png` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조)
- 자동 저장 잠금 실패 (`../../Build/TAUI-Debug-manual-6c1e823c/automatic-save-locked.png` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조)
- Reload 복구 (`../../Build/TAUI-Debug-manual-6c1e823c/reload-recovered.png` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조)
- 새 편집 복구 (`../../Build/TAUI-Debug-manual-6c1e823c/fresh-edit-recovered.png` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조)
- 정상 종료 로그: `Build/TAUI-Debug-{conflict-6d2c7780,manual-6c1e823c}/editor.out`

Build 증거는 로컬 산출물이며 Git에 포함하지 않는다. 과거 실행의 Build fixture가 정리되어
현재 실행은 새 fixture로 재검증했다. 이전 기록을 이번 실행 결과로 소급 대체하지 않는다.

## 잔여와 진행률

파일 잠금 자동 저장 오류가 `Texture sidecar must have a valid canonical UUIDv4 identity`로
표시된다. 실제 파일 신원은 정상이고 읽기가 잠금으로 실패했으므로 원인 구분 진단은 잔여다.
삭제, 저장 대기 중 손상 외부 bytes 충돌, 최종 비교 이후 동시 쓰기, 반복/장시간,
다중 창/배율은 미실행이다. Debug 종료 충돌 재검증과 다른 소비자/GPU/품질도 확대 수용하지 않는다.

이번 한정 Debug 입력·실패 재읽기·오류 해제 수용을 `12-0-D` 0.25일로 분리한다.
T0 잔여는 0.25일, 활성 총20일은 유지한다. 완료13.25/20 = 66.25%, 대시보드 반올림 66%.
계획 가중치이며 실제 투입 시간이나 전체 T0 완료를 뜻하지 않는다.

</details>
