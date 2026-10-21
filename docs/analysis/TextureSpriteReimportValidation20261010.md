# PHASE 12 — SpriteRenderer 재임포트·씬 재로드 검증

<!-- master-doc-review:start -->
## 2026-10-20 master 구현 대조

기준: `27de3f5a02aadde7456aab680db3184c36ee735e`. 대시보드는 변경하지 않는다. 아래 현행 대조와 소스 선언이 이전 시점의 구조·미구현 표기보다 우선한다.

[전체 대조·링크 정리 원장](MasterDocumentationAudit20261020.html)

### 현행 계약과 이 문서의 적용 범위

- DataSystem의 typed 요청은 resolver 세대와 취소/게시 상태를 보존한다. descriptor·payload·이미지 owner와 frame pin은 분리된 수명이다.
- ownership_cpp 기반 자원 소유와 논리적 unmount를 GC 객체 수거·GPU 완료와 혼동하지 않는다. DirectXTex cook와 GPU-ready 소비의 제품 범위는 텍스처/AssetDepot 계획에서 수용한다.

### 해당 구현에서 확인한 선언·소유 경계

- [Tools/regression/verify-texture-reimport-scene.ps1:5](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-texture-reimport-scene.ps1#L5) — `$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))`
- [Tools/regression/verify-texture-reimport-scene.ps1:15](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-texture-reimport-scene.ps1#L15) — `$root = Join-Path $repo ('Build/TRS-' + $config + '-' + [guid]::NewGuid().ToString('N').Substring(0,8))`
- [Tools/regression/verify-texture-reimport-scene.ps1:24](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-texture-reimport-scene.ps1#L24) — `[IO.File]::WriteAllText($texture + '.meta', "guid: 33333333-3333-4333-8333-333333333333n")`
- [Engine/SceneRuntime/SpriteRenderer.h:11](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/SpriteRenderer.h#L11) — `class [[reflgen::reflect]] SpriteRenderer : public meta::identity<SpriteRenderer, Component>`
- [Engine/SceneRuntime/SpriteRenderer.h:16](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/SpriteRenderer.h#L16) — `void gc_trace(gc::tracer& tracer) const override`
- [Engine/SceneRuntime/SpriteRenderer.h:18](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/SpriteRenderer.h#L18) — `Component::gc_trace(tracer);`
- [Tools/build.ps1:2](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L2) — `[CmdletBinding()]`
- [Tools/build.ps1:4](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L4) — `[ValidateSet('Game')][string]$Target = 'Game',`
- [Tools/build.ps1:21](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L21) — `$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))`
- [Engine/SceneRuntime/SpriteRenderer.cpp:9](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/SpriteRenderer.cpp#L9) — `void SpriteRenderer::OnInitialized()`
- [Engine/SceneRuntime/SpriteRenderer.cpp:20](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/SpriteRenderer.cpp#L20) — `void SpriteRenderer::OnAddedToScene()`
- [Engine/SceneRuntime/SpriteRenderer.cpp:53](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/SpriteRenderer.cpp#L53) — `void SpriteRenderer::SetSprite(const own::shared_owner<const Texture>& ptr)`

### 이전 근거의 유효 범위

기존 세부 계약은 위 현행 구현과 모순되지 않는 범위에서 유지한다. 과거 수치·실행 통과·해시·장치 조건은 당시 기록이다. 로컬 산출물이 사라진 항목은 링크 대신 경로와 미보존 상태를 표시하며, 현재 master의 재실행 통과로 승격하지 않는다.

<!-- master-doc-review:end -->

<details data-master-history><summary>세부 계약·원래 분석과 당시 측정 기록</summary>

기준일: 2026-10-10 KST. 정본: [TexturePipelinePlan](../plans/TexturePipelinePlan.md).

## 재현과 수정

수정 전 실제 Debug Editor 실행에서 저장 씬의 `m_SpritePath: ""`를 확인했다.
재로드 후 SpriteRenderer가 새 generation을 복원하지 못해
`Sprite component generation was not retained/restored`로 실패했다.
격리 프로젝트는 `Build/TRS-Debug-360796b3`, 실행 기록은
`Build/phase12-sprite-baseline-run.log` 및 해당 프로젝트 `results.jsonl`이다.

`SpriteRenderer::SetSprite`도 ImageComponent와 같이 기존 경로/이름이 없는 cooked
자산은 TextureAssetOrigin의 GUID로 등록 경로를 찾아 저장하도록 수정했다.
등록 경로를 찾지 못하면 기존 owner를 교체하기 전에 거부한다.
loose 경로, origin 없는 임시 생성 텍스처 및 null로 참조를 해제하는 처리를 유지한다.
immutable description/artifact를 수정하지 않는다.

## 실제 실행 수용

기존 격리 `assets.texture.reimportprobe`에 SpriteRenderer를 추가했다.
UITexture와 일반 Texture role을 각각 비동기로 준비하고 실제 두 컴포넌트를 같은 씬에 저장한다.
각 구성의 단일 Editor 프로세스에서 다음을 확인했다.

1. 초기 4px texture 연결과 씬 저장
2. 최대 크기 2 재임포트·게시 후 새 2px owner 생성, 열린 두 컴포넌트는 기존 owner 유지
3. 저장 씬 재로드와 프레임 교체 후 두 컴포넌트가 각 role의 새 2px owner 참조
4. 이전 씬 퇴역 후 보유한 이전 Sprite texture/image view가 4px로 유효
5. 손상 PNG cook 실패와 없는 씬 reload 실패 시 현재 씬/두 컴포넌트 owner 보존
6. 원본 복구 후 4px 재게시, 열린 두 컴포넌트는 2px owner 유지

Debug/Release 실제 host와 참조 프로젝트 빌드 및 실행 종료 코드 0을 확인했다.
검사 수는 deadline/state 폴링 단정을 포함하므로 고정 기능 사례 수가 아니다.

| 구성 | 격리 프로젝트 | 단정 수 | runtime DLL SHA-256 |
|---|---|---:|---|
| Debug | `TRS-Debug-edc50552` | 50 | `818EDADF973D695FF62EBF0CF32D82584876CFF6389514996AD15D49CE592776` |
| Release | `TRS-Release-0a85bc1c` | 51 | `2D94FA2B59280572FBBBE308A63DF1A01170D2097B4FCEAEE16C5AFDE9E3090D` |

소스 9개 drift 0, fixture PNG 원본 복구 및 runtime DLL 실행 전후 hash 일치를 확인했다.
유지된 검사기는 `Tools/regression/verify-texture-reimport-scene.ps1 -Configuration All`이다.
receipt: `Build/TextureReimportScene/result.json`.
빌드 로그: `Build/phase12-sprite-{debug,release}-build.log`.
실행 로그: `Build/phase12-sprite-run.log`.
기존 texture import 정적 검사 5/5 및 변경된 tracked 파일 whitespace 검사는 통과했다.
Release의 기존 Utility_Framework PDB LNK4020 경고는 남아 있다.
대시보드 전체 JavaScript 파싱·렌더·유한 진행률은 통과했다. 기존 PHASE 4.85
quoted-key 구조 문제는 문자열 140건/항목 모양 14건 그대로 남아 전체 구조 검사는
실패한다(`Build/phase12-sprite-dashboard.log`).

## 잔여 수용

이는 작은 fixture의 실제 저장/역직렬화·CPU image view·owner identity 검증이다.
Inspector 버튼/경고/취소/파일 선택 직접 입력, GPU 화면 픽셀, 전체 모델/포맷 소비,
Shipping GPU 패키지, 품질/시간/메모리 및 대량·장시간 검증은 남긴다.
자동 live component rebind와 역할별 다중 산출물 선택은 별도 확장 범위다.
다음 활성 검증은 Inspector 실제 조작이다.
T0/T1a/T2 진행 중 및 earnedDays 0, T1b 대기, T3 중단은 유지한다.

</details>
