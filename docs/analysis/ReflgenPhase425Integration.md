# reflgen와 PHASE 4.25 통합 검증

<!-- master-doc-review:start -->
## 2026-10-20 master 구현 대조

기준: `27de3f5a02aadde7456aab680db3184c36ee735e`. 대시보드는 변경하지 않는다. 아래 현행 대조와 소스 선언이 이전 시점의 구조·미구현 표기보다 우선한다.

[전체 대조·링크 정리 원장](MasterDocumentationAudit20261020.html)

### 현행 계약과 이 문서의 적용 범위

- 현행 서술 정본은 reflgen::schema_of/type_descriptor다. 저작 문서와 cooked archive는 공유 서술의 서로 다른 소비 정책이다.
- 모델/AssetSet 신원은 구현된 UUIDv8/CEMF 계약을 따른다. 다른 저작 객체의 신원까지 일괄 치환하거나 과거 UUIDv4 이주 단계를 재개하지 않는다.

### 해당 구현에서 확인한 선언·소유 경계

- [Engine/RenderEngine/ReflgenRegistration.h:1](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/ReflgenRegistration.h#L1) — `#pragma once`
- [Engine/Utility_Framework/AuthoringRymlErrorPolicy.h:29](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/Utility_Framework/AuthoringRymlErrorPolicy.h#L29) — `void EnsureRymlErrorPolicy();`
- [Engine/Utility_Framework/AuthoringRymlErrorPolicy.h:38](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/Utility_Framework/AuthoringRymlErrorPolicy.h#L38) — `struct RymlErrorPolicyProbe`
- [Engine/Utility_Framework/AuthoringRymlErrorPolicy.h:77](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/Utility_Framework/AuthoringRymlErrorPolicy.h#L77) — `[[nodiscard]] RymlErrorPolicyProbe ProbeRymlErrorPolicy();`
- [ScriptCore/MeshRenderer.cs:13](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/ScriptCore/MeshRenderer.cs#L13) — `public sealed class MeshRenderer : NativeComponent`
- [ScriptCore/MeshRenderer.cs:15](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/ScriptCore/MeshRenderer.cs#L15) — `public string MaterialName => Native.MeshGetMaterialName(OwnerHandle);`
- [ScriptCore/MeshRenderer.cs:24](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/ScriptCore/MeshRenderer.cs#L24) — `public void InstantiateMaterial(string newName) => Native.MeshInstantiateMaterial(OwnerHandle, newName);`
- [Editor/ImGuiHelper/ImGui.h:1](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/ImGuiHelper/ImGui.h#L1) — `#pragma once`
- [Player/ForbiddenIncludes/imgui.h:1](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Player/ForbiddenIncludes/imgui.h#L1) — `#pragma once`
- [Engine/SceneRuntime/MeshRenderer.h:19](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/MeshRenderer.h#L19) — `class [[reflgen::reflect]] MeshRenderer : public meta::identity<MeshRenderer, Component>`
- [Engine/SceneRuntime/MeshRenderer.h:23](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/MeshRenderer.h#L23) — `void gc_trace(gc::tracer& tracer) const override`
- [Engine/SceneRuntime/MeshRenderer.h:25](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/MeshRenderer.h#L25) — `Component::gc_trace(tracer);`
- [Engine/RenderEngine/SceneRenderProfile.h:5](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/SceneRenderProfile.h#L5) — `struct [[reflgen::reflect]] SceneRenderProfile`
- [Engine/RenderEngine/SceneRenderProfile.h:7](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/SceneRenderProfile.h#L7) — `public:`
- [Engine/RenderEngine/SceneRenderProfile.h:8](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/SceneRenderProfile.h#L8) — `SceneRenderProfile() = default;`
- [Engine/RenderEngine/Interfaces/FoliageType.h:53](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Interfaces/FoliageType.h#L53) — `own::shared_owner<const assets::ModelAnimationDescriptor> m_modelDescriptor{};`
- [Engine/RenderEngine/Interfaces/FoliageType.h:55](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Interfaces/FoliageType.h#L55) — `own::shared_owner<const assets::ModelMeshDescriptor> m_meshDescriptor{};`
- [Engine/RenderEngine/Interfaces/FoliageType.h:57](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Interfaces/FoliageType.h#L57) — `own::shared_owner<const Material> m_material{};`

### 이전 근거의 유효 범위

기존 세부 계약은 위 현행 구현과 모순되지 않는 범위에서 유지한다. 과거 수치·실행 통과·해시·장치 조건은 당시 기록이다. 로컬 산출물이 사라진 항목은 링크 대신 경로와 미보존 상태를 표시하며, 현재 master의 재실행 통과로 승격하지 않는다.

<!-- master-doc-review:end -->

<details data-master-history><summary>세부 계약·원래 분석과 당시 측정 기록</summary>

2026-09-30. [PR #114](https://github.com/29thnight/CreatorEngine/pull/114)의 reflgen 도입을
master에 병합하고, 기존 PHASE 4.25 변경과 다른 세션의 편집기 성능 개선을 함께 통합한다.
MAT-9 렌더 결과 대조의 잔여 작업은 이 통합 검증과 별도로 유지한다.

## 설치와 병합

- VS **18 Community** 인스턴스 `55b1962e`에 공식 **Reflgen.VisualStudio 1.0.1** VSIX를 설치했다.
  설치 종료 코드 0과 설치된 `extension.vsixmanifest`의 identity
  `Reflgen.VisualStudio.29thnight`·버전 1.0.1을 확인했다.
- [확장 릴리스](https://github.com/29thnight/reflgen_cpp/releases/tag/v1.0.1)의 VSIX SHA-256:
  `44E7F76741BA890CFEC3CBC051823FEEEAFCF0646A8DA479E5176E7EFFF8994D`.
  확장의 IDE 자동완성 기능을 직접 조작한 검증은 하지 않았다.
- 엔진 manifest/overlay port의 생성기·런타임은 **reflgen 1.0.0**이다.
  확장 1.0.1은 VSIX 패키징 변경이며 생성기 버전 변경과 구분한다.
- PR head `1ff657a6ea5b9aa4cbae63128221ebd4be2f2ced`를 확인해 병합했다.
  GitHub master 병합 커밋은 `090aba0afe16e1495fec06738d928fb581617595`다.
- 로컬 4.25 변경은 먼저 `0268e4b5cc6bfeed5258b426a3ce5f3bbed7f886`에 보존했다.
  이 커밋과 PR 병합 커밋을 합쳐 충돌을 해결했다.

## 4.25와 맞춘 내용

- MeshRenderer Inspector의 그래프 저작·미리보기 기능을 유지하고 legacy 재질의
  Rendering Mode 편집만 reflgen `field_info`·enum 서술자를 사용하도록 바꿨다.
- `SceneRenderProfile`·`SceneRenderProfileComponent` 이름을 유지하면서
  `[[reflgen::reflect]]` 서술로 옮겼다. 구 `VolumeProfile`·`VolumeComponent` 이름은 복원하지 않는다.
- Material의 그래프 인스턴스, Foliage의 그래프 소스 및 프로파일 컴포넌트의
  런타임 설정·캐시는 `[[reflgen::ignore]]`로 저장에서 제외했다.
- RenderEngine 프로젝트는 MaterialGraph 헤더와 `ReflgenRegistration.h`를 함께 포함한다.
- 생성된 서술자의 저장 필드 순서를 확인했다: Material 14개, FoliageType 3개,
  SceneRenderProfile 1개, SceneRenderProfileComponent 자체 필드 2개.
  병합 전 저장 계약과 일치하고 런타임 포인터·캐시는 포함되지 않는다.
- 다른 세션 「프로파일링으로 프레임 급감 원인 찾기」의 캔버스 계산 캐시,
  노드 선택 정보, Inspector 재질 선택·구체/격자 미리보기 변경을 포함했다.
  해당 측정 기록은 [MAT9NodeEditorPerformance](MAT9NodeEditorPerformance.md)에 정리했다.
  병합 전 성능 수치를 병합 후 실측으로 표시하지 않는다.
- README·문서 색인·LX 계획서의 현재 상태를 Material Editor 제품 연결에 맞춰 정리했다.
  MAT-0~8 완료 **32/34일**, MAT-9 및 LX-3/3H 진행 상태를 유지한다.

## 검증

VS 18 x64 MSBuild·MSVC v145, `UseDynamicDebugging=false`, `LinkIncremental=false`로
`CreatorEngine.sln` 전체를 빌드했다. Editor·Player·AssetCooker 및 솔루션의
공용 모듈·도구 빌드를 포함한다.

| 검증 | 결과 |
|---|---|
| 전체 솔루션 Debug | 성공, 종료 0, 빌드 오류 0, 12분 48초 |
| 전체 솔루션 Release | 성공, 종료 0, 빌드 오류 0, 16분 31초 |
| reflgen 속성 배치 | 자기 검사 및 헤더 52개·속성 블록 295개 통과 |
| 컨테이너 분류 | Debug/Release 모두 통과 |
| 컨테이너 값·저장 형상 왕복 | Debug 28축, 실패 0 |
| 생성된 저장 계약 | Debug/Release 각각 4타입·20필드 일치, 런타임 필드 제외 |
| Material Node Editor 제품 회귀 Debug | 1,344검사·최종 capture 5회·소스 1,139개 drift 0·정상 종료 |
| Material Node Editor 제품 회귀 Release | 1,344검사·최종 capture 5회·소스 1,139개 drift 0·정상 종료 |
| 대시보드 | 전체 JS 파싱, 435항목·산수 검사·유한한 진행률 통과 |

제품 회귀는 격리된 검증 프로젝트를 실제 Editor에서 열고 HTTP 명령으로
동일한 LXDocument를 편집했다. 문서 ID/revision 충돌 거부, 값·연결·접힘,
Undo/Redo·Save/Reload, Apply 실패 시 마지막 정상 generation 유지,
Scene 저장·새 프로세스 재개방 및 Scene/구체 미리보기 픽셀 변화를 확인한다.
Debug/Release GPU validation 문제·누락 메시지는 각각 0이다. 기동 시 엔진의
`VerifyReflectRegistration()`도 통과했다. 성능 재측정을 목적으로 한 실행은 아니다.

빌드 로그에는 MSVC 변환·링커(`LNK4075`, `LNK4229`)와 .NET trimming 경고가 있다.
생성기는 반영 선언 밖의 clang 진단 2건을 `RG0101` 안내로 보고했다
(`AuthoringRymlErrorPolicy.h` 주석 위치). 생성된 저장 계약 검사와 MSVC 컴파일,
실제 Editor 기동·직렬화 회귀 결과를 각각 확인한다.

설치·빌드·회귀 집계와 소스 identity는
[ReflgenPhase425Integration.json](ReflgenPhase425Integration.json)에 보존한다.
원시 빌드/회귀 로그는 로컬 `Build/Obj/ReflgenIntegration/`에 있다.

첫 manifest 빌드는 reflgen 설치를 완료한 뒤 MSBuild targets를 다시 읽도록
재빌드를 안내했다. 새 의존성을 설치한 첫 실행의 결과이며, 실제 컴파일 검증은
그 다음 빌드 결과를 사용한다.

## 남는 범위

이번 작업은 reflgen 통합과 빌드·편집/저장 회귀 확인이다. MAT-9의 박막 이미지 차이,
Special·텍스처/조명 입력 및 경로별 대조·최종 성능 수용은 남아 있다.
최초 ImGui scale 0.8 원인 분석도 사용자가 미룬 후속 항목으로 유지한다.
원시 `Dynamic_CPP/2026-09-30-1135.ceprof`는 로컬에 보존하고 소스 커밋에 넣지 않는다.

</details>
