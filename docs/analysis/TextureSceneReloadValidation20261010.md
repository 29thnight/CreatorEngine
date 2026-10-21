# PHASE 12 — 실제 Editor 씬 재로드 검증

<!-- master-doc-review:start -->
## 2026-10-20 master 구현 대조

기준: `27de3f5a02aadde7456aab680db3184c36ee735e`. 대시보드는 변경하지 않는다. 아래 현행 대조와 소스 선언이 이전 시점의 구조·미구현 표기보다 우선한다.

[전체 대조·링크 정리 원장](MasterDocumentationAudit20261020.html)

### 현행 계약과 이 문서의 적용 범위

- DataSystem의 typed 요청은 resolver 세대와 취소/게시 상태를 보존한다. descriptor·payload·이미지 owner와 frame pin은 분리된 수명이다.
- ownership_cpp 기반 자원 소유와 논리적 unmount를 GC 객체 수거·GPU 완료와 혼동하지 않는다. DirectXTex cook와 GPU-ready 소비의 제품 범위는 텍스처/AssetDepot 계획에서 수용한다.

### 해당 구현에서 확인한 선언·소유 경계

- [Tools/regression/verify-texture-scene-reload.ps1:5](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-texture-scene-reload.ps1#L5) — `$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))`
- [Tools/regression/verify-texture-scene-reload.ps1:14](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-texture-scene-reload.ps1#L14) — `$root = Join-Path $repo ('Build/TSR-' + $config + '-' + [guid]::NewGuid().ToString('N').Substring(0,8))`
- [Tools/regression/verify-texture-scene-reload.ps1:23](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-texture-scene-reload.ps1#L23) — `[IO.File]::WriteAllText($texture + '.meta', "guid: 33333333-3333-4333-8333-333333333333n")`
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

기준일: 2026-10-10 KST. 정본은 [TexturePipelinePlan](../plans/TexturePipelinePlan.md),
직전 자산 서비스 검사는 [임포트 수명 기록](TextureImportLifecycleValidation20261010.md)에 있다.

## 실행 경계

`verify-texture-scene-reload.ps1`은 현재 Debug/Release의 실제 `CreatorEditor.exe`와
`CreatorEditor.runtime.dll`을 사용한다. `--development-project`로 ignored Build 안의
새 프로젝트를 지정하고 실제 명령·프레임 경로에서 실행한다. 기존 프로젝트의 자산,
설정, 작업 공간 파일에 검증 fixture를 작성하지 않는다.

Inspector의 Reload Saved Scene이 사용하는 `scene.open_async`로 씬을 준비하고,
일반 프레임 진행 뒤 `scene.load.status`로 terminal state를 확인한다. 최초 fixture는
`scene.new` → `object.create` → `scene.save`로 만든다. 검사기는 저장된 fixture의
필수 bundle에 PNG 참조를 추가하고 문서 손상과 의존성 누락을 주입한다.
이는 제품 저작 UI와 같은 조작을 증명하는 검사가 아니다.

## 수용 계약

1. 필수 4×4 PNG가 포함된 저장 씬의 재로드가 Ready가 되고 SavedOnly 엔티티가 복원된다.
   이전 씬의 InitialUnsaved 엔티티는 새 씬에 남지 않는다. 제품 texture 조회로 4×4를 확인한다.
2. 교체 뒤 RetainedUnsaved 엔티티를 새로 만든다. 없는 씬 파일, 손상된 씬 문서,
   없는 필수 texture의 세 요청이 각각 Failed가 된다.
3. 각 실패 뒤 활성 씬을 별도 파일로 저장해 SavedOnly와 RetainedUnsaved 보존을 확인한다.
   `object.describe`의 entity handle과 sceneId도 최초 RetainedUnsaved 생성 결과와 같아야 한다.
4. 유효한 저장 씬으로 다시 요청하면 Ready가 되고 새로운 sceneId로 교체된다.
   저장되지 않은 RetainedUnsaved는 최종 씬에 남지 않는다.

정상 두 요청과 실패 세 요청을 구성마다 실행한다. 실패는 정확히 세 건의
`scene.load.status`에서 `scene.load_failed`로 발생해야 하며 나머지 명령은 성공해야 한다.
제품 명령 정책은 Failed에 종료 코드 4를 부여하므로 음성 검사 프로세스의 4는
예상 결과다. 검사기는 임의 오류/종료 코드를 성공으로 바꾸지 않는다.

## 결과와 재현

최종 결과는 `Build/TextureSceneReload/result.json`에 기록한다. 각 프로젝트의
`author/reload.jsonl`, stdout/stderr, 다섯 개 저장 관찰 파일과 시나리오를 보존한다.
Debug/Release 모두 정상 Ready 2건, Failed 3건, 실패 후 미저장 엔티티 및
entity handle/sceneId 보존 3건, 저장 관찰 파일 5개 비교를 통과했다.
총 재로드 요청 10건을 실행했다. 검증 대상 소스 5개의 `sourceDrift=0`을 확인했다.
두 실행의 종료 코드는 예상한 4이며 검사 스크립트는 성공으로 종료했다.
실행 프로젝트는 Debug `Build/TSR-Debug-9383a063`, Release `Build/TSR-Release-8e5d9c58`이다.
대시보드 전체 JavaScript 파싱·렌더·유한 진행률과 변경 파일 whitespace 검사는 통과했다.
구조 검사기의 기존 PHASE 4.85 quoted-key RTP 문제(문자열 140/항목 모양 14)는
이전 결과와 동일하게 남아 있다. 전체 구조 검사 통과로 표시하지 않는다.

```powershell
Tools/regression/verify-texture-scene-reload.ps1 -Configuration All
```

이 후속 작업은 제품 동작 변경이나 추가 빌드를 요구하지 않았다. 검사 시나리오를
추가했고 기존 unrelated 작업 트리 변경을 보존했다. commit/push는 수행하지 않았다.

## 잔여 범위

이번 texture는 작은 원본 PNG 의존성이다. 이후 cooked generation 게시와
ImageComponent 씬 재로드를 한 프로세스에서 연결한 결과는
[종단 검증 기록](TextureReimportSceneValidation20261010.md)에 별도로 유지한다.
Inspector Save and Reimport/확인창/취소/파일 선택의 실제 조작, GPU texture sampling,
Shipping GPU package, 신규 BC5/BC7·모델 전체 소비 및 품질/시간/메모리 측정은 남아 있다.
각 실패 유형을 모든 runtime publication 실패나 모든 GPU/장시간 조건으로 확대하지 않는다.

T0/T1a/T2는 진행 중이며 `earnedDays: 0`, T1b 대기, T3 중단을 유지한다.

</details>
