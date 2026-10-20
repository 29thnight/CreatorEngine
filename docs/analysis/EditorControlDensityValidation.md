# Editor 입력 컨트롤·메뉴 간격 보정 (2026-09-12)

<!-- master-doc-review:start -->
## 2026-10-20 master 구현 대조

기준: `27de3f5a02aadde7456aab680db3184c36ee735e`. 대시보드는 변경하지 않는다. 아래 현행 대조와 소스 선언이 이전 시점의 구조·미구현 표기보다 우선한다.

[전체 대조·링크 정리 원장](MasterDocumentationAudit20261020.html)

### 현행 계약과 이 문서의 적용 범위

- 창·메뉴/명령과 scene 편집은 값/세대 핸들을 경계로 전달한다. 실제 Component 변경·dirty publication·Undo 수용을 분리해 확인한다.
- Workspace W9 설정/프로필·Rendering 진단과 완료된 PBR-W9는 다른 작업이다. 미구현 설정을 동작하는 제품 스위치로 표시하지 않는다.

### 해당 구현에서 확인한 선언·소유 경계

- [Tools/regression/verify-editor-theme.ps1:16](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-editor-theme.ps1#L16) — `$Exe = [IO.Path]::GetFullPath($Exe)`
- [Tools/regression/verify-editor-theme.ps1:17](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-editor-theme.ps1#L17) — `$Work = [IO.Path]::GetFullPath($Work)`
- [Tools/regression/verify-editor-theme.ps1:36](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-editor-theme.ps1#L36) — `$settingsBytes = [IO.File]::ReadAllBytes($settingsPath)`
- [Editor/ImGuiHelper/ImGui.h:1](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/ImGuiHelper/ImGui.h#L1) — `#pragma once`
- [ScriptCore/LightComponent.cs:4](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/ScriptCore/LightComponent.cs#L4) — `public enum LightType`
- [ScriptCore/LightComponent.cs:17](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/ScriptCore/LightComponent.cs#L17) — `public enum LightStatus`
- [ScriptCore/LightComponent.cs:40](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/ScriptCore/LightComponent.cs#L40) — `public sealed class LightComponent : NativeComponent`
- [Player/ForbiddenIncludes/imgui.h:1](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Player/ForbiddenIncludes/imgui.h#L1) — `#pragma once`
- [Engine/SceneRuntime/LightComponent.h:19](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/LightComponent.h#L19) — `class [[reflgen::reflect]] LightComponent : public meta::identity<LightComponent, Component>`
- [Engine/SceneRuntime/LightComponent.h:23](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/LightComponent.h#L23) — `void gc_trace(gc::tracer& tracer) const override`
- [Engine/SceneRuntime/LightComponent.h:25](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/LightComponent.h#L25) — `Component::gc_trace(tracer);`
- [Tools/build.ps1:2](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L2) — `[CmdletBinding()]`
- [Tools/build.ps1:4](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L4) — `[ValidateSet('Game')][string]$Target = 'Game',`
- [Tools/build.ps1:21](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L21) — `$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))`
- [Engine/SceneRuntime/LightComponent.cpp:5](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/LightComponent.cpp#L5) — `void LightComponent::OnInitialized()`
- [Engine/SceneRuntime/LightComponent.cpp:36](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/LightComponent.cpp#L36) — `void LightComponent::OnAddedToScene()`
- [Engine/SceneRuntime/LightComponent.cpp:50](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/LightComponent.cpp#L50) — `void LightComponent::OnRemovingFromScene()`

### 이전 근거의 유효 범위

기존 세부 계약은 위 현행 구현과 모순되지 않는 범위에서 유지한다. 과거 수치·실행 통과·해시·장치 조건은 당시 기록이다. 로컬 산출물이 사라진 항목은 링크 대신 경로와 미보존 상태를 표시하며, 현재 master의 재실행 통과로 승격하지 않는다.

<!-- master-doc-review:end -->

<details data-master-history><summary>세부 계약·원래 분석과 당시 측정 기록</summary>

## 원인과 수정

| 증상 | 원인 | 수정 |
|---|---|---|
| 체크박스·드롭다운·float 입력이 큼 | 글꼴 16px에 상하 padding 4px가 더해져 컨트롤 높이 24px. 사용자/모니터 배율이 각각 150%이면 약 54 physical px | 상하 padding 2px, 컨트롤 높이 20 logical px. 일반 행 간격 8→4px, 본문 글꼴 16px 유지 |
| 탭 핸들 크기 유지 | ImGui 탭과 입력칸이 FramePadding을 공유해 함께 축소됨 | TabStyleScope로 NewFrame의 floating dock, DockSpace, 창 Begin, Animator의 명시적 탭에 기존 4px padding 적용. 본문은 2px로 복원하며 탭 높이는 기존 24px 유지 |
| 컴포넌트 활성 체크박스가 제목보다 큼 | ImGui Checkbox가 GetFrameHeight() 전체를 사각형 크기로 사용 | 해당 체크박스에만 padding.y=0을 적용해 16px. 예약 칸도 16px, 제목과 간격 6px로 조정 |
| Add Component 문구가 잘림 | 배율과 문구 길이에 관계없는 고정 물리 폭 180px | CalcTextSize+실제 padding으로 최소 폭 산정. 내용 영역 안에서 가운데 배치. 부족하면 Add/Component 두 줄 표시 |
| 메뉴 항목이 붙음 | 제목 줄에서 사용하는 ItemSpacing.y=0이 열린 팝업까지 상속됨 | 메뉴 간격 x=10/y=6px, 팝업 WindowPadding=8×6px. 제목 행 높이 20px 유지 |

컴포넌트 헤더의 접기 버튼은 체크박스 사각형 위에서 반응하지 않도록 분리했다.
ImGui Checkbox와 기존 Component::SetEnabled 경로를 계속 사용한다.
메뉴는 표준 BeginMenu/MenuItem, 입력값 편집은 기존 ImGui 및 Inspector 숫자 위젯을 사용한다.

## 검증

증거 디렉터리: `Artifacts/phase21-control-density/`.

- Debug x64 `CreatorEditor.vcxproj /t:Build` 통과. 기존 `/DELAYLOAD:vulkan-1.dll` LNK4229 경고는 남는다.
  도중 커서 모듈의 변경된 호출부와 기존 라이브러리가 달라 LNK2019가 발생했고,
  해당 모듈을 포함한 재빌드로 통과했다. 이 작업에서 커서 소스는 수정하지 않았다.
- 최종 실행 파일의 `editor.selftest`: **916 checks, 0 failures**. `editor.theme`의
  geometry/theme mapping, DPI, Inter/Material Symbols 검사 모두 통과.
- 실제 DX12 화면(사용자 150%, OS DPI 150%)에서 Scene/Game, Hierarchy, Inspector,
  Content Browser 탭이 기존 높이를 유지하고 본문 입력칸은 축소된 것을 확인했다.
  기본 16px 글꼴은 그대로이며 배율을 적용한 탭은 54 physical px,
  입력칸은 ImGui padding 절삭을 포함해 44 physical px이다.
- Main Camera의 Transform float3, Camera float4/float1, Tag/Layer 콤보와 체크박스 표시 확인.
  float2 전용 화면 및 모든 전용 드로어를 전수 조작한 결과는 아니다.
- LightComponent 체크박스 끄기/켜기와 헤더 접기를 각각 조작했다.
  체크박스 클릭으로 본문이 함께 접히지 않았고, 마지막에 활성 상태를 복원했다.
- Add Component 전체 문구 표시와 기존 컴포넌트 선택 팝업 열기 확인.
  창을 2880×1665 → 850×1245 → 720×1245 → 2880×1665로 변경하면서,
  마지막 좁은 폭에서 Add/Component 두 줄 표시를 확인했다. 컴포넌트 추가/씬 저장은 하지 않았다.
- Edit 드롭다운의 행 간격과 팝업 가장자리 여백을 실제 화면에서 확인했다.
- `verify-editor-theme.ps1 -Backends dx12`: **3회 기동, 64 checks 통과**.
  사용자 배율 100→150→100% 왕복이며 실제 OS DPI는 150%였다.
  물리 모니터 간 이동 검증은 포함하지 않는다.
- 검증용 Editor 종료 후 EngineSettings.asset와 imgui.ini를 시작 전 바이트로 복원했다.
  변경 파일 `git diff --check` 및 대시보드 JavaScript 구문 검사 통과.

최종 탭/숫자 입력 화면: tabs-restored-camera.png (`../../Artifacts/phase21-control-density/tabs-restored-camera.png` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조).
좁은 패널: add-component-wrapped.png (`../../Artifacts/phase21-control-density/add-component-wrapped.png` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조).
메뉴 간격: edit-menu.png (`../../Artifacts/phase21-control-density/edit-menu.png` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조).
메뉴/LightComponent 동작 캡처는 탭 높이 복원 전 중간 빌드의 증거이며,
최종 빌드의 탭과 입력칸 크기는 `tabs-restored-camera.png`를 기준으로 한다.

Vulkan은 사용자 결정에 따라 보류한다. 이 작업은 W2-I 전체나 비동기 썸네일 W7의 완료를 의미하지 않는다.

</details>
