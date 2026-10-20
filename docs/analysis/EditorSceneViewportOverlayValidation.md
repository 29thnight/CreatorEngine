# Scene viewport toolbar / ImViewGuizmo 적용

<!-- master-doc-review:start -->
## 2026-10-20 master 구현 대조

기준: `27de3f5a02aadde7456aab680db3184c36ee735e`. 대시보드는 변경하지 않는다. 아래 현행 대조와 소스 선언이 이전 시점의 구조·미구현 표기보다 우선한다.

[전체 대조·링크 정리 원장](MasterDocumentationAudit20261020.html)

### 현행 계약과 이 문서의 적용 범위

- 창·메뉴/명령과 scene 편집은 값/세대 핸들을 경계로 전달한다. 실제 Component 변경·dirty publication·Undo 수용을 분리해 확인한다.
- Workspace W9 설정/프로필·Rendering 진단과 완료된 PBR-W9는 다른 작업이다. 미구현 설정을 동작하는 제품 스위치로 표시하지 않는다.

### 해당 구현에서 확인한 선언·소유 경계

- [Editor/EngineGUIWindow/SceneViewportOverlay.h:7](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineGUIWindow/SceneViewportOverlay.h#L7) — `class EditorCameraRig;`
- [Editor/EngineGUIWindow/SceneViewportOverlay.h:8](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineGUIWindow/SceneViewportOverlay.h#L8) — `class GizmoRenderer;`
- [Editor/EngineGUIWindow/SceneViewportOverlay.h:9](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineGUIWindow/SceneViewportOverlay.h#L9) — `struct EnhancedLiveDisplayTexture;`
- [Tools/regression/verify-editor-theme.ps1:16](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-editor-theme.ps1#L16) — `$Exe = [IO.Path]::GetFullPath($Exe)`
- [Tools/regression/verify-editor-theme.ps1:17](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-editor-theme.ps1#L17) — `$Work = [IO.Path]::GetFullPath($Work)`
- [Tools/regression/verify-editor-theme.ps1:36](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-editor-theme.ps1#L36) — `$settingsBytes = [IO.File]::ReadAllBytes($settingsPath)`
- [Editor/ImGuiHelper/ImGui.h:1](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/ImGuiHelper/ImGui.h#L1) — `#pragma once`
- [Player/ForbiddenIncludes/imgui.h:1](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Player/ForbiddenIncludes/imgui.h#L1) — `#pragma once`
- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.h:31](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.h#L31) — `enum class EnhancedLiveDisplayTarget : uint8_t`
- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.h:39](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.h#L39) — `enum class EnhancedLiveViewFlags : uint32_t`
- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.h:177](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.h#L177) — `own::shared_owner<const material_graph::SceneMaterialSource> materialPreview;`
- [Editor/EngineGUIWindow/SceneViewportOverlay.cpp:49](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineGUIWindow/SceneViewportOverlay.cpp#L49) — `draw->AddRectFilled(p, {p.x + width, p.y + height}, ImGui::GetColorU32(color), height * 0.5f, corners);`
- [Editor/EngineGUIWindow/SceneViewportOverlay.cpp:50](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineGUIWindow/SceneViewportOverlay.cpp#L50) — `draw->AddRect(p, {p.x + width, p.y + height}, ImGui::GetColorU32(ImGuiCol_Border), height * 0.5f, 1.f, corners);`
- [Editor/EngineGUIWindow/SceneViewportOverlay.cpp:51](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineGUIWindow/SceneViewportOverlay.cpp#L51) — `if (ImGui::IsItemFocused()) draw->AddRect(p, {p.x + width, p.y + height},`
- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp:216](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp#L216) — `const auto started = std::chrono::steady_clock::now();`
- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp:230](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp#L230) — `std::chrono::steady_clock::now() - started).count();`
- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp:307](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp#L307) — `const uint64_t generation = std::exchange(token.captureGeneration, 0);`

### 이전 근거의 유효 범위

기존 세부 계약은 위 현행 구현과 모순되지 않는 범위에서 유지한다. 과거 수치·실행 통과·해시·장치 조건은 당시 기록이다. 로컬 산출물이 사라진 항목은 링크 대신 경로와 미보존 상태를 표시하며, 현재 master의 재실행 통과로 승격하지 않는다.

<!-- master-doc-review:end -->

<details data-master-history><summary>세부 계약·원래 분석과 당시 측정 기록</summary>

- 날짜: 2026-09-12
- 범위: PHASE 21 W2-V의 씬뷰 툴바·방향 기즈모·통계 후속 구현
- 기준: 사용자가 제공한 Unreal 툴바, 기존 잘림 화면, Blender 방향 기즈모 스크린샷
- 백엔드: DX12. Vulkan은 기존 사용자 결정에 따라 보류한다.

## 적용 내용

| 영역 | 동작 |
|---|---|
| 왼쪽 툴바 | 원형 옵션 버튼, Perspective/Orthographic, Lit, Show 팝업 |
| 오른쪽 툴바 | Select/Move/Rotate/Scale 묶음, Local/World, 위치·각도·크기 스냅 토글/값, 카메라 설정 |
| 좁은 폭 | 전체 묶음 → 핵심 도구+현재 스냅 → 도구+더보기 → 옵션 메뉴. 같은 기능을 메뉴에서 계속 사용 |
| 렌더 메뉴 | Lit와 기존 Wireframe overlay, 기존 Render Pass 설정 연결. 미지원 Unlit 등을 작동하는 것처럼 표시하지 않음 |
| Show | 방향 기즈모 표시, 기존 Grid settings 창, Render Statistics |
| 방향 기즈모 | ImViewGuizmo의 축 클릭/회전 드래그. 반투명 원형 배경, 양의 축 색 원·문자, 음의 축 속 빈 색 테두리 |
| 카메라 | 축 전환과 궤도 회전 결과를 EditorCameraRig에 적용하고 후속 이동용 방향 상태도 동기화 |
| 통계 | FPS·Screen Size와 Runtime, CPU/GPU 시간 및 실제 GPU 패스 시간. 기존 상시 FPS 박스와 오래된 고정 패스 시간 목록 제거 |

버튼 아이콘은 Material Symbols를 사용한다. Unreal의 형태·배치를 참조하며 메뉴 내용은 엔진의 현재 기능에 맞춘다.
Grid settings는 기존 창 연결을 유지한 것이며 신규 그리드 렌더 설정을 구현한 것은 아니다.
기즈모의 축 이름은 엔진의 +X/+Y/+Z이고, Blender의 Z-up 좌표계로 엔진을 바꾸지 않는다.

## 코드와 경계

- `SceneViewportOverlay.h/.cpp`가 측정된 묶음 폭, 반응형 모드, 팝업과 포인터 소유권을 관리한다.
- Scene 이미지의 실제 min/max/extent를 툴바·기즈모·ImGuizmo·picking·모델 drop·terrain ray에 공유한다.
  `windowWidth - 270`의 창 원점 누락과 `imageMax`를 크기로 넘기던 경로를 제거했다.
- UI에서 시작한 누름은 버튼 밖에서 놓아도 씬 선택으로 전달하지 않는다.
  팝업/텍스트 편집/방향 애니메이션 중에는 씬 조작 단축키를 막는다.
- `editor.sceneview`는 UI 스레드가 게시한 마지막 씬뷰 값 스냅샷을 읽는다.
  명령 스레드에서 ImGui를 직접 접근하지 않는다. Scene 탭이 숨겨져 있으면 마지막으로 그린 값이다.
- Runtime은 `DrawRenderRuntime`을 Render Pass 창과 통계 팝업이 공유한다.
  통계 팝업은 `EnhancedSceneRenderer::GetLiveDebugSnapshot()`을 0.25초 간격으로 읽으며,
  `passTimings`의 실제 이름과 값을 표시한다. CPU/GPU 시간은 전체 라이브 렌더러 범위이며 씬뷰만의 비용으로 단정하지 않는다.
- ImViewGuizmo의 GLM 기본 경로를 Mathematics로 교체했다. 행 벡터 행렬·쿼터니언 합성 순서·+Z 전방을 반영한다.
  반대 방향 snap은 쿼터니언 slerp를 사용하고, press가 해당 축에서 시작했을 때만 release를 소비한다.
  출처·고정 SHA·수정 범위는 [PROVENANCE](../../ThirdParty/ImViewGuizmo/PROVENANCE.md)를 따른다.

## 검증 중 발견하고 수정한 문제

- 커서 복원만 하고 항목을 제출하지 않아 ImGui 1.92의 `SetCursorPos/SetCursorScreenPos` 검증이 실패했다.
  복원 뒤 `Dummy`로 레이아웃을 완료하도록 수정했고 재기동 및 실제 Scene 탭 표시를 확인했다.
- ImGui 1.92.8의 `AddRect`에서 두께/플래그 인자 순서가 바뀌어 테두리가 과도하게 커졌다.
  새 순서로 두께와 모서리 플래그를 명시했다.
- 외부 헤더의 `ImLengthSqr` 이름이 ImGui 내부 함수와 ADL로 충돌해 전용 이름으로 바꿨다.

## 검증 결과

- Debug 빌드 및 DX12 기동 후 `editor.selftest`: 640개 검사, 실패 0.
- 추가 검사: 음수 화면 원점, 폭 120~1440·높이 80/180/640 logical px·배율 1~3에서 겹침/경계 확인 315개.
- Mathematics: 회전 합성, 행렬/쿼터니언 일치, 여섯 축 +Z 전방, 반대 방향 보간의 유한/단위 회전 확인 14개.
- 실제 X축 클릭 시 카메라가 `(0,1,-10)`에서 `(8,1,-2)`로 이동하고 전방이 `(-1,0,0)`으로 바뀌었다.
- 최종 빌드에서 +X/+Y/+Z/-X/-Y/-Z를 차례로 클릭해 각각 반대 축 방향을 향하는 카메라 전방을 확인했다(오차 0.0001 이하).
- 실제 분할선 드래그로 이미지 폭 1890/1067/482/272 px에서 Full/Compact/Tools/Menu를 각각 확인했다.
  명령 관측에서도 좌우 묶음의 겹침과 이미지 밖 이탈이 없었고 기즈모가 툴바 아래에 유지됐다.
- 기즈모 드래그 후 카메라 전방 `(-0.865639,0.305059,-0.397)`을 관측했다.
  일반 씬 영역에서 RMB 입력을 받은 뒤에도 같은 전방을 유지했고 Main Camera 선택도 유지됐다.
- 최종 UI에서 Rotate 도구, World 좌표 전환, 회전 스냅 15→30도 및 활성 표시를 확인했다.
  `editor.sceneview`에서도 operation=2, local=false가 반영됐다.
- 실제 Render Statistics 팝업에서 DX12, runner on, pipeline ready, 타깃 크기, 드로우/배치, 프레임 수,
  CPU/GPU 및 `Shadow(x3)` 등 현재 패스 시간이 표시되는 것을 확인했다.
- 최종 Debug 빌드 성공: `Artifacts/phase21-scene-toolbar/build-debug-verified.log`.
  기존 `LNK4229 /DELAYLOAD:vulkan-1.dll` 경고는 남아 있으며 빌드 오류는 없다.
- 테두리·축 색상 보정 뒤 재기동/Scene 표시/축 조작에 assertion이 없었고 stderr는 비어 있다.
- 최종 화면의 `editor.theme`: clean/geometryMatches/themeMappingMatches=true, iconRoles=63, missingIconRoles=0.
- ImGui obsolete API 검사: 952개 소스, 27개 호출 위치, 위반 0. 프로젝트 XML과 대시보드 스크립트 구문 검사 통과.
- `verify-editor-theme.ps1 -Backends dx12`: 시작 배율 100→150→100%의 3회 재기동, 64개 검사 통과.
  각 실행의 selftest 640개도 통과했다. 실제 OS DPI는 150%였으며 모니터 간 이동 검사는 아니다.
  프로젝트 설정과 imgui.ini를 검증 전 원본 바이트로 복원했다.

최종 화면:
전체 툴바 (`../../Artifacts/phase21-scene-toolbar/scene-toolbar-wide.png` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조),
좁은 폭 (`../../Artifacts/phase21-scene-toolbar/scene-toolbar-narrow.png` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조),
Runtime 통계 (`../../Artifacts/phase21-scene-toolbar/scene-runtime-stats.png` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조),
GPU 패스 시간 (`../../Artifacts/phase21-scene-toolbar/scene-gpu-passes.png` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조).

원시 관측·빌드 로그는 `Artifacts/phase21-scene-toolbar/`에 둔다. GUI 축 검사는 `verified-pos-*.json`과
`verified-neg-*.json`, 전체/축소 모드 검사는 `interactive-checks.json`, 최종 자가 검사는 `selftest-verified.json`이다.
검증은 기본 SampleScene에서 진행했으며 에셋 drop·terrain brush 및 모든 입력 상태 조합을 전수 확인한 것은 아니다.

W2-V 전체 완료 판정은 하지 않는다. 전수 입력 상태·100회 이상 resize·모든 도킹 프리셋·성능 gate,
W4 렌더 타깃 소유권, W5 Play 입력 전이, Vulkan은 별도 범위다.

2026-09-12 후속 Scene 중앙 crop·기즈모/picking 좌표 통일 및 창 여백 축소 검증은
[EditorSceneCropValidation.md](EditorSceneCropValidation.md)에 기록한다.

</details>
