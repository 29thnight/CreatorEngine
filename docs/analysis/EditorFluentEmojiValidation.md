# Microsoft Fluent Emoji 이미지 적용 검증

<!-- master-doc-review:start -->
## 2026-10-20 master 구현 대조

기준: `27de3f5a02aadde7456aab680db3184c36ee735e`. 대시보드는 변경하지 않는다. 아래 현행 대조와 소스 선언이 이전 시점의 구조·미구현 표기보다 우선한다.

[전체 대조·링크 정리 원장](MasterDocumentationAudit20261020.html)

### 현행 계약과 이 문서의 적용 범위

- 창·메뉴/명령과 scene 편집은 값/세대 핸들을 경계로 전달한다. 실제 Component 변경·dirty publication·Undo 수용을 분리해 확인한다.
- Workspace W9 설정/프로필·Rendering 진단과 완료된 PBR-W9는 다른 작업이다. 미구현 설정을 동작하는 제품 스위치로 표시하지 않는다.

### 해당 구현에서 확인한 선언·소유 경계

- [Tools/icons/Fetch-FluentEmoji.ps1:2](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/icons/Fetch-FluentEmoji.ps1#L2) — `[CmdletBinding()]`
- [Tools/icons/Fetch-FluentEmoji.ps1:5](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/icons/Fetch-FluentEmoji.ps1#L5) — `$iconRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../Resources/Editor/Icons'))`
- [Tools/icons/Fetch-FluentEmoji.ps1:11](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/icons/Fetch-FluentEmoji.ps1#L11) — `$temp = [IO.Path]::GetTempFileName()`
- [ScriptCore/Audio.cs:8](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/ScriptCore/Audio.cs#L8) — `public readonly struct AudioAssetId : IEquatable<AudioAssetId>`
- [ScriptCore/Audio.cs:46](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/ScriptCore/Audio.cs#L46) — `public readonly struct AudioParameter`
- [ScriptCore/Audio.cs:83](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/ScriptCore/Audio.cs#L83) — `public struct AudioPlaySettings`
- [ScriptCore/Prefab.cs:10](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/ScriptCore/Prefab.cs#L10) — `public readonly struct Prefab`
- [ScriptCore/Prefab.cs:12](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/ScriptCore/Prefab.cs#L12) — `private readonly string? _name;`
- [ScriptCore/Prefab.cs:14](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/ScriptCore/Prefab.cs#L14) — `private Prefab(string name) => _name = name;`
- [Editor/ImGuiHelper/ImGui.h:1](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/ImGuiHelper/ImGui.h#L1) — `#pragma once`
- [Engine/SceneRuntime/Prefab.h:10](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/Prefab.h#L10) — `class [[reflgen::reflect]] Prefab : public Object`
- [Engine/SceneRuntime/Prefab.h:12](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/Prefab.h#L12) — `friend struct reflgen::access;`
- [Engine/SceneRuntime/Prefab.h:13](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/Prefab.h#L13) — `public:`
- [Engine/SceneRuntime/Terrain.h:16](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/Terrain.h#L16) — `class [[reflgen::reflect]] TerrainComponent : public meta::identity<TerrainComponent, Component>`
- [Engine/SceneRuntime/Terrain.h:20](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/Terrain.h#L20) — `void gc_trace(gc::tracer& tracer) const override`
- [Engine/SceneRuntime/Terrain.h:22](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/Terrain.h#L22) — `Component::gc_trace(tracer);`
- [Player/ForbiddenIncludes/imgui.h:1](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Player/ForbiddenIncludes/imgui.h#L1) — `#pragma once`
- [Engine/RenderEngine/Experiment/Model.h:9](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Experiment/Model.h#L9) — `class ModelLoader;`
- [Engine/RenderEngine/Experiment/Model.h:13](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Experiment/Model.h#L13) — `class Model final`
- [Engine/RenderEngine/Experiment/Model.h:15](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Experiment/Model.h#L15) — `public:`

### 이전 근거의 유효 범위

기존 세부 계약은 위 현행 구현과 모순되지 않는 범위에서 유지한다. 과거 수치·실행 통과·해시·장치 조건은 당시 기록이다. 로컬 산출물이 사라진 항목은 링크 대신 경로와 미보존 상태를 표시하며, 현재 master의 재실행 통과로 승격하지 않는다.

<!-- master-doc-review:end -->

<details data-master-history><summary>세부 계약·원래 분석과 당시 측정 기록</summary>

2026-09-13. 사용자 요청에 따른 엔티티 및 Content Browser 이미지 교체.

## 적용

- 기본 엔티티는 **Package(상자)**. Inspector 헤더, 선택 팝업, Hierarchy가 같은 이미지를 쓴다.
- 기존 9개 프리셋 ID를 유지하고 이미지만 교체했다. 저장된 선택·잠금·Undo 데이터는 그대로 사용한다.
- Content Browser의 프로젝트 머리행, 폴더 트리/도구 버튼/타일/목록, 파일 유형 타일/목록에
  Fluent Emoji PNG를 연결했다. 검색·탐색·보기 전환 등 기능 글리프는 Material Symbols다.
- EditorAssetPresentation이 이미지 수명을 소유하며 그리기 중 디스크 로드를 하지 않는다.
  파일 분류를 공유해 필터와 아이콘의 확장자 판정을 일치시킨다.
  미분류 파일은 Page facing up 이미지와 Unknown 이름으로 표시한다.
- 이미지 바인딩 실패 시 기존 유형 글리프로 대체한다. ImGui 항목의 클릭·선택·드롭 영역은 유지한다.
- 현재 자산 표시 경로는 유형 이미지다. 비동기 자산 썸네일 요청·생성·완료 게시·타일 교체는
  기존 W7 후속 범위이며 이번 교체의 완료 실적으로 포함하지 않는다.

## 원본 및 재현

- 공식 원본: [Microsoft Fluent Emoji](https://github.com/microsoft/fluentui-emoji), **3D** PNG.
- 고정 revision: `1ffb34c752ecf5d402f04cfb4b392c77f57c54bc`.
- 21개 원본을 24개 리소스 파일명에 대응한다. 원본 PNG 바이트를 수정하지 않고 UI에서 축소한다.
- `Resources/Editor/Icons/FluentEmoji.provenance.json`: 원본 경로·revision URL·SHA-256.
- `Resources/Editor/Icons/LICENSE-FluentEmoji-MIT.txt`: Microsoft MIT 고지문 원문.
- `Tools/icons/Fetch-FluentEmoji.ps1`: 고정 원본/고지문 복원 및 해시 검증.
- 실행 배포 위치: `Bin/x64-Debug/Resources/Icons`. 이전 @icons 변환 도구와 원본은 은퇴한다.

| 대상 | Emoji |
|---|---|
| Entity / Model | Package |
| Game Manager / Camera / Light | Gear / Movie camera / Light bulb |
| Audio / Player / Trigger | Speaker high volume / Bust in silhouette / High voltage |
| Script / Prefab | Scroll / Puzzle piece |
| 프로젝트 / 폴더 닫힘·열림 | Video game / File folder · Open file folder |
| Texture / Material / Terrain | Framed picture / Artist palette / National park |
| Shader / Code / Sound | Sparkles / Scroll / Musical notes |
| HDR / Volume Profile / Font | Sunrise over mountains / Control knobs / Input latin letters |
| Unknown | Page facing up |

## 검증

- VS18/v145 Debug Editor 빌드 통과(오류 0). 기존 `LNK4229 /DELAYLOAD:vulkan-1.dll` 경고 1개.
  로그: `Artifacts/phase21-fluent-emoji/build-debug.log`.
- 원본 24개와 배포 24개의 SHA-256 일치, 원본/배포 MIT 고지문 해시 일치.
- DX12 theme 게이트: 시작 배율 100→150→100%, **3회 기동 / 64 checks 통과**.
  각 `editor.selftest`는 **937 checks / 0 failures**, 창·메뉴·theme 통과.
  현재 OS DPI 150% 관측이며 실제 모니터 간 DPI 이동 검사는 이번에 하지 않았다.
  설정과 imgui.ini는 게이트 전후 바이트 단위 복원.
- 실제 UI: 기본 상자의 Inspector/Hierarchy 표시, 프리셋 9개와 현재 선택 체크,
  폴더 트리와 타일/목록 이미지, 폴더 이동/경로 바 이동, 미분류 파일 타일/목록 전환 확인.
  대표 표시를 확인했으며 모든 파일 확장자의 개별 화면을 전수 검사한 것은 아니다.
  검증용 에디터를 닫은 뒤 설정과 imgui.ini를 시작 전 바이트로 복원했다.
- 아이콘/잠금 저장·복제·Undo 기능의 이전 검증은
  [EditorEntityNavigationValidation.md](EditorEntityNavigationValidation.md)에 유지한다.
  이번에는 해당 상태 경로를 바꾸지 않았고 163개 전체 행렬을 재실행하지 않았다.

화면: 기본 상자와 폴더 (`../../Artifacts/phase21-fluent-emoji/entity-package-and-folders.png` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조),
프리셋 선택 (`../../Artifacts/phase21-fluent-emoji/entity-presets.png` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조),
파일 목록 (`../../Artifacts/phase21-fluent-emoji/file-list.png` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조).

W2-I/W2-B 전체 상태와 공수는 유지한다. Vulkan 검증은 사용자 결정에 따라 보류한다.

</details>
