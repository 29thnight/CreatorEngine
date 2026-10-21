# MAT-9 노드 편집기·인스펙터 성능 측정

<!-- master-doc-review:start -->
## 2026-10-20 master 구현 대조

기준: `27de3f5a02aadde7456aab680db3184c36ee735e`. 대시보드는 변경하지 않는다. 아래 현행 대조와 소스 선언이 이전 시점의 구조·미구현 표기보다 우선한다.

[전체 대조·링크 정리 원장](MasterDocumentationAudit20261020.html)

### 현행 계약과 이 문서의 적용 범위

- live material은 compiled graph 프로그램과 밀봉된 scene/view/geometry 입력을 소비한다. 준비·실패 상태에서 옛 instance를 대체 경로로 선택하는 구안을 현행 계약으로 사용하지 않는다.
- GPU scene packet·resident table·visibility/REAC 최적화는 구현 기반이다. shader의 물리 의미, route별 출력과 전체 비용의 수용은 소스 존재와 별개다.

### 해당 구현에서 확인한 선언·소유 경계

- [Editor/EngineGUIWindow/MaterialGraphWindow.cpp:53](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineGUIWindow/MaterialGraphWindow.cpp#L53) — `own::shared_owner<const material_graph::Generation> previousGeneration;`
- [Editor/EngineGUIWindow/MaterialGraphWindow.cpp:54](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineGUIWindow/MaterialGraphWindow.cpp#L54) — `own::shared_owner<const material_graph::Generation> generation;`
- [Editor/EngineGUIWindow/MaterialGraphWindow.cpp:77](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineGUIWindow/MaterialGraphWindow.cpp#L77) — `std::ranges::any_of(name, [](unsigned char value) { return value < 32; }))`
- [Editor/EngineGUIWindow/ImGuiDrawHelperMeshRenderer.cpp:52](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineGUIWindow/ImGuiDrawHelperMeshRenderer.cpp#L52) — `ImGui::PushID(label);`
- [Editor/EngineGUIWindow/ImGuiDrawHelperMeshRenderer.cpp:53](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineGUIWindow/ImGuiDrawHelperMeshRenderer.cpp#L53) — `ImGui::SetNextItemWidth(sheet.line(label));`
- [Editor/EngineGUIWindow/ImGuiDrawHelperMeshRenderer.cpp:54](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineGUIWindow/ImGuiDrawHelperMeshRenderer.cpp#L54) — `ImGui::InputText("##Value", &value, ImGuiInputTextFlags_ReadOnly);`
- [ScriptCore/MeshRenderer.cs:13](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/ScriptCore/MeshRenderer.cs#L13) — `public sealed class MeshRenderer : NativeComponent`
- [ScriptCore/MeshRenderer.cs:15](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/ScriptCore/MeshRenderer.cs#L15) — `public string MaterialName => Native.MeshGetMaterialName(OwnerHandle);`
- [ScriptCore/MeshRenderer.cs:24](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/ScriptCore/MeshRenderer.cs#L24) — `public void InstantiateMaterial(string newName) => Native.MeshInstantiateMaterial(OwnerHandle, newName);`
- [Editor/ImGuiHelper/ImGui.h:1](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/ImGuiHelper/ImGui.h#L1) — `#pragma once`
- [Player/ForbiddenIncludes/imgui.h:1](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Player/ForbiddenIncludes/imgui.h#L1) — `#pragma once`
- [Engine/SceneRuntime/MeshRenderer.h:19](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/MeshRenderer.h#L19) — `class [[reflgen::reflect]] MeshRenderer : public meta::identity<MeshRenderer, Component>`
- [Engine/SceneRuntime/MeshRenderer.h:23](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/MeshRenderer.h#L23) — `void gc_trace(gc::tracer& tracer) const override`
- [Engine/SceneRuntime/MeshRenderer.h:25](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/MeshRenderer.h#L25) — `Component::gc_trace(tracer);`
- [Editor/EngineGUIWindow/MaterialGraphWindow.h:6](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineGUIWindow/MaterialGraphWindow.h#L6) — `class MeshRenderer;`
- [Editor/EngineGUIWindow/MaterialGraphWindow.h:7](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineGUIWindow/MaterialGraphWindow.h#L7) — `struct EnhancedLiveViewRequest;`
- [Editor/EngineGUIWindow/MaterialGraphWindow.h:11](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineGUIWindow/MaterialGraphWindow.h#L11) — `bool Open(MeshRenderer& renderer, std::string& error);`
- [Engine/SceneRuntime/MeshRenderer.cpp:28](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/MeshRenderer.cpp#L28) — `own::shared_owner<const material_graph::Instance> observed;`
- [Engine/SceneRuntime/MeshRenderer.cpp:39](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/MeshRenderer.cpp#L39) — `const auto authored = std::ranges::find(instance.description.parameters, id,`
- [Engine/SceneRuntime/MeshRenderer.cpp:46](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/MeshRenderer.cpp#L46) — `const auto parameter = std::ranges::find(parameters, id, &LX::LXMaterialParameter::id);`

### 이전 근거의 유효 범위

기존 세부 계약은 위 현행 구현과 모순되지 않는 범위에서 유지한다. 과거 수치·실행 통과·해시·장치 조건은 당시 기록이다. 로컬 산출물이 사라진 항목은 링크 대신 경로와 미보존 상태를 표시하며, 현재 master의 재실행 통과로 승격하지 않는다.

<!-- master-doc-review:end -->

<details data-master-history><summary>세부 계약·원래 분석과 당시 측정 기록</summary>

측정일: **2026-09-30**. 별도 세션 「프로파일링으로 프레임 급감 원인 찾기」의
실제 창 조작·HTTP·프로파일 결과를 PHASE 4.25 기록으로 통합한다.
수치는 reflgen 병합 **전** 측정이며, 병합 후 재측정 결과로 해석하지 않는다.

## 수정과 소스 확인

- LX 캔버스의 핀·열 너비 등 반복 계산을 캐시하고 그리기 과정의 중복 작업을 줄였다.
- 노드 편집기의 미리보기 패널을 선택 노드 설명·소켓 정보 패널로 바꿨다.
- MeshRenderer Inspector에 머테리얼 검색·선택·에셋 추가와 기본 수치 편집을 연결했다.
- 구체·격자 바닥 미리보기는 Inspector에서 기본 접힘으로 제공한다. 재질 변경·Refresh에
  갱신하고 완료 결과를 재사용한다. 노드 창을 닫은 상태에서도 Inspector 미리보기는 유지한다.
- `ImGuiDrawHelperMeshRenderer.cpp`의 `DrawInspectorPreview()` 호출과
  `MaterialGraphWindow.cpp`의 독립 Inspector preview 요청을 현재 소스에서 확인했다.
- 해당 세션이 기록한 최종 소스 8개의 SHA-256은 통합 전 작업 트리와 모두 일치했다.

## 실제 실행 측정

CreatorRobot, Scene **1302×614**, 워밍업 후 약 18~20초 구간이다.
노드 편집기와 Inspector를 표시하고 프로파일러 창은 닫았다.
FPS는 구간 전체에서 완료된 Scene frame 수를 시간으로 나눈 값이다.

| 조건 | 완료 Scene FPS | 캔버스 CPU 평균 |
|---|---:|---:|
| Debug 수정 전, 미리보기 접힘 | 28.99 | 미계측 |
| Debug 수정 전, 미리보기 펼침 | 29.12 | 미계측 |
| Debug 계측 추가, 수정 전 | 31.88 | 26.34 ms |
| Debug 수정 후, 미리보기 접힘 | 43.06 | 9.27 ms |
| Debug 수정 후, 미리보기 펼침 | 41.71 | 9.70 ms |
| Release 수정 후, 미리보기 접힘 | 310.99 | 0.697 ms |
| Release 수정 후, 미리보기 펼침 | 330.22 | 0.618 ms |
| Release 기록 중지, 미리보기 펼침 | 342.67 | 미계측 |

Presentation의 긴 CPU 구간을 DXGI Present 자체의 비용으로 분류하면 안 된다.
실제 DXGI Present는 평균 0.35~0.37 ms였고, 캔버스 계산과 Scene 잠금 대기가
프레임 경로를 지연시켰다. Debug 렌더 스레드 CPU 약 23 ms는 이 창 배치에서 남는다.
Release 보관 프레임 제한으로 저장 표본에는 공백이 있다. CPU/GPU 평균은 수집된
유효 구간, FPS는 전체 측정 구간의 완료 프레임 수를 사용한다.

이 조건은 [MAT9MaterialScenePerformance](MAT9MaterialScenePerformance.md)의
작은/큰 SceneView CPU 측정 조건과 다르므로 FPS 숫자를 직접 이어 붙이지 않는다.
GPU/GBuffer/tier별 최종 성능 상한과 이동 카메라 수용을 대신하지 않는다.

## 검증과 한계

- Lattice Debug 자체 검사 통과.
- Material Node Editor 제품 회귀: Debug **1,341개**, Release **1,339개** 검사,
  각 실제 capture 5회, source drift 0, 정상 종료. GPU 검증 문제 0건.
- 실제 UI의 에셋 추가·재선택·Undo·노드 창 종료 후 미리보기 유지 확인.
- 임시 검증 에셋 제거·Release workspace 복원·검증 Editor 정상 종료 확인.
- 별도 `editor.windows` 등록 감사는 MaterialGraph 창을 bodyless 1건으로 보고했다.
  실제 그리기와 제품 회귀가 통과했어도 이 감사 항목을 해결된 것으로 세지 않는다.
- MAT-8은 완료 상태를 유지하고 MAT-9는 진행 상태를 유지한다. ImGui scale 최초 0.8
  원인 분석은 사용자가 미뤄 둔 별도 후속 항목이다.

보존한 집계·소스 identity·회귀 종료값은
[MAT9NodeEditorPerformance.json](MAT9NodeEditorPerformance.json)에 있다.
원시 `.ceprof` 및 임시 workspace·검증 에셋은 제품 소스 커밋에 넣지 않는다.

</details>
