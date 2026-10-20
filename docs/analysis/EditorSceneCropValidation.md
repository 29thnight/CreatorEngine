# Scene View 중앙 crop과 창 본문 여백

<!-- master-doc-review:start -->
## 2026-10-20 master 구현 대조

기준: `27de3f5a02aadde7456aab680db3184c36ee735e`. 대시보드는 변경하지 않는다. 아래 현행 대조와 소스 선언이 이전 시점의 구조·미구현 표기보다 우선한다.

[전체 대조·링크 정리 원장](MasterDocumentationAudit20261020.html)

### 현행 계약과 이 문서의 적용 범위

- 창·메뉴/명령과 scene 편집은 값/세대 핸들을 경계로 전달한다. 실제 Component 변경·dirty publication·Undo 수용을 분리해 확인한다.
- Workspace W9 설정/프로필·Rendering 진단과 완료된 PBR-W9는 다른 작업이다. 미구현 설정을 동작하는 제품 스위치로 표시하지 않는다.

### 해당 구현에서 확인한 선언·소유 경계

- [Editor/ImGuiHelper/ImGui.h:1](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/ImGuiHelper/ImGui.h#L1) — `#pragma once`
- [Player/ForbiddenIncludes/imgui.h:1](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Player/ForbiddenIncludes/imgui.h#L1) — `#pragma once`
- [Editor/EngineGUIWindow/SceneViewWindow.h:38](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineGUIWindow/SceneViewWindow.h#L38) — `const std::vector<gc::trace_ref<Entity>>& slots);`
- [Editor/EngineGUIWindow/SceneViewWindow.h:63](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineGUIWindow/SceneViewWindow.h#L63) — `class SceneViewWindow`
- [Editor/EngineGUIWindow/SceneViewWindow.h:66](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineGUIWindow/SceneViewWindow.h#L66) — `void RenderSceneViewWindow();`
- [Editor/EngineGUIWindow/SceneViewWindow.cpp:77](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineGUIWindow/SceneViewWindow.cpp#L77) — `void SceneViewWindow::RenderSceneViewWindow()`
- [Editor/EngineGUIWindow/SceneViewWindow.cpp:133](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineGUIWindow/SceneViewWindow.cpp#L133) — `void SceneViewWindow::RenderSceneView(float* matrix, Entity* obj, Camera* cam)`
- [Editor/EngineGUIWindow/SceneViewWindow.cpp:687](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineGUIWindow/SceneViewWindow.cpp#L687) — `math::vector3 SceneViewWindow::ConvertMouseToWorldPosition(`

### 이전 근거의 유효 범위

기존 세부 계약은 위 현행 구현과 모순되지 않는 범위에서 유지한다. 과거 수치·실행 통과·해시·장치 조건은 당시 기록이다. 로컬 산출물이 사라진 항목은 링크 대신 경로와 미보존 상태를 표시하며, 현재 master의 재실행 통과로 승격하지 않는다.

<!-- master-doc-review:end -->

<details data-master-history><summary>세부 계약·원래 분석과 당시 측정 기록</summary>

2026-09-12 사용자 보고: Scene 창을 조절하면 화면이 가로/세로로 왜곡되고, 각 창 본문의 네 면 여백이 과하다.

## 원인과 수정

- `SceneViewWindow`는 전체 렌더 타깃을 `ImGui::Image(texture, contentSize)`로 표시했다.
  카메라 투영은 전체 렌더 타깃 비율인데 표시 사각형은 도킹 패널 크기여서 축별 배율이 달라졌다.
- GPU 완료 이미지의 실제 width/height와 표시 ID를 동일한 수명 락 아래 읽는다.
  최신 제출 프레임 크기를 이전 완료 이미지에 적용하지 않는다.
- `SceneViewportImage`는 원본 픽셀당 framebuffer 픽셀 하나를 유지하고, 패널 중앙을 광학 중심으로 사용한다.
  창 크기 변경은 UV crop만 바꾸며 카메라 FOV를 쓰지 않는다. 패널이 원본보다 크면 남는 영역은 배경으로 표시한다.
- ImGuizmo는 잘리기 전 가상 이미지 사각형과 원본 종횡비를 사용한다. 선택·모델 배치·지형 브러시의
  ray 변환도 같은 사각형을 사용한다. 툴바/방향 기즈모는 보이는 패널 영역에 배치한다.
- 공통 WindowPadding은 8×6→3×2 logical px, Browser 트리/목록의 child padding은 6→2px로 줄인다.
  컨트롤 내부 간격과 셀 간격은 별도 값으로 분리한다. 이전에 요청한 분할선 뒤 10px 간격은 유지한다.

기존 렌더 타깃 크기 소유권은 유지한다. Scene 패널만 조절할 때 고정된 원본을 잘라 표시하는 수정이며,
씬뷰별 렌더 타깃 생성 및 W4/W5 전체 전환을 완료한 것으로 계산하지 않는다.

## 검증

산출물: `Artifacts/phase21-scene-crop/`.

- 제품 `editor.selftest`에 가로/세로 크기 변화, 음수 원점, framebuffer scale 1/1.5/2,
  원근/직교 투영의 크기 보존 및 picking 역변환, 중앙 crop 경계, 원본보다 큰 패널, 0 크기 검사를 추가한다.
- `editor.sceneview`에 원본 사각형·표시 사각형·UV crop·원본 비율 관측을 연결한다.
- DX12 실행 화면과 배율별 theme gate를 확인한다. Vulkan은 이번 실행 검증에서 제외한다.

| 검증 | 결과 |
|---|---|
| VS18/v145 x64 Debug CreatorEditor | 통과, `build-final.log`. 첫 전체 빌드에는 기존 Vulkan DELAYLOAD LNK4229 경고 1개 |
| `editor.selftest` | 815 checks / 0 failures. 이번 crop 검사 175개 포함 |
| DX12 화면 비교 | 넓은 배치 2030×1054, 좁은 배치 1100×1054, 낮은 배치 2030×596 (ImGui 좌표) |
| 원본·카메라 보존 | 세 배치 모두 원본 2880×1665, 카메라 위치/방향 동일. UV 범위×원본 크기=표시 크기, 축별 확대/축소 없음 |
| 실제 선택 | 넓은/낮은 배치에서 카메라/라이트 표시 위치 클릭→Main Camera 선택, 이동 기즈모 원점 일치 |
| 창 여백 | Content Browser, Hierarchy, Inspector에서 작은 본문 여백 확인 |
| DX12 theme gate | 100→150→100% 사용자 배율, 3 starts / 64 checks PASS. 각 실행의 815개 selftest도 통과 |

자동화 도구의 실제 분할선 drag 요청에서는 폭 변화가 관측되지 않았다. 따라서 세 가지 크기는
저장된 dock 비율을 바꿔 재기동한 **배치 fixture**이며, 연속 마우스 resize 통과로 보고하지 않는다.
연속 크기 변화의 확대/축소 불변식은 제품 selftest의 crop 계산 검사로 확인했다.
지형 브러시·모델 drop 소비자와 이동 기즈모 drag는 이번 실행에서 별도로 조작하지 않았다.
원근/직교 투영 검사는 수학 검사이며, 실제 GUI 비교는 기본 원근 카메라다.

`scene-wide.png`, `scene-tall.png`, `scene-short.png`, `picking-wide.png`에 화면을 보관했다.
원시 관측은 `initial.json`, `tall-ready.json`, `short-ready.json`, 요약은 `crop-layout-checks.csv`다.
검증용 Editor는 종료했고, 설정과 도킹 ini는 검증 직전 원본 바이트로 복원했다.

</details>
