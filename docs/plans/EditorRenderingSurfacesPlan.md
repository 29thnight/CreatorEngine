# Editor 렌더 설정·진단 표면 정리

> 2026-10-20 정리 · master `27de3f5a02aadde7456aab680db3184c36ee735e` · 잔여 구현·수용 계획

판정 기준은 위 master의 커밋된 소스다. 작업 중인 로컬 코드 변경은 포함하지 않는다. 완료된 구현 단계·날짜별 실행 일지는 제거했다. 대시보드는 SRP 저작 전환 취소에 해당하는 항목만 제거하고, 나머지 상태·공수·링크를 보존한다.

[전체 문서 대조·충돌 판정](MasterPlanAudit20261020.html) · [정리 전 원문](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/docs/plans/EditorRenderingSurfacesPlan.md)

## 남은 작업과 완료 판정

- Workspace W9: Preferences와 Project Settings의 저장 범위·default profile/환경 resolver·quality tier·Player cook 소비를 일치시킨다. temporal renderFeatures 설정의 구현된 부분은 재작성하지 않는다.
- 14-LIVE: 녹화에 독립적인 Rendering 진단의 데이터 신선도·손실·비용·실제 UI를 수용한다. compiled graph viewer와 RenderCapture의 수명을 합치지 않는다.

## 적용할 구조와 경계

`Editor window/commands → common editing transaction → scene component → dirty publication`

위 경로는 소스에서 확인한 구현 기반이다. 아래 요구의 수용 결과를 뜻하지 않는다. 구현을 다시 만드는 항목과 제품 검증을 혼합하지 않는다.

## 추적할 잔여 항목

| 추적 ID | 소유 단위 (잔여 작업은 위 목록) | 대시보드 보존 상태 |
|---|---|---|
| `W9` · PHASE 21 | Preferences·Project Settings 설정 소유권·저장·Quality | progress |
| `14-LIVE` · PHASE 14 | Rendering - Live·Scene 통계의 녹화 독립 view 귀속 | progress |

대시보드 상태는 인계용 식별 정보다. 위 구현 기반에서 이미 충족한 하위 요구는 재작성하지 않고, 남은 제품/실패/비용 수용을 수행한다.

## master 소스 근거

모든 행의 파일·행 번호·Git blob은 고정 master에서 대조했다. 소스 존재·배선 확인과 실행 성공을 구분한다.

- [Editor/EngineGUIWindow/MenuBarWindow.cpp:459](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineGUIWindow/MenuBarWindow.cpp#L459) — `if (ImGui::BeginMenu("Settings"))`
- [Engine/RuntimeHost/CommandCore/CommandRegistry.h:15](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RuntimeHost/CommandCore/CommandRegistry.h#L15) — `class CommandRegistry`
- [Engine/RuntimeHost/CommandCore/CommandDescriptor.h:134](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RuntimeHost/CommandCore/CommandDescriptor.h#L134) — `struct CommandDescriptor`
- [Engine/SceneRuntime/Scene.h:428](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/Scene.h#L428) — `bool PublishRenderProxyDirty(Component* component, ProxyDirty dirty);`

## 검증 경계

이번 정리는 master 소스·문서 전체의 소유권/충돌/링크/HTML 검증이다. 엔진 빌드·GPU 픽셀·SDK/장치·성능·실제 GUI 수용을 새로 실행한 결과가 아니다. 과거 통과는 해당 소스/바이너리 신원에만 유효하다. 남은 제품 수용은 위 요구를 실제 산출물로 닫아야 한다.

<details><summary>이전 절 링크 호환</summary>

이전 절의 완료 이력·구안은 제거했다. 이 링크로 들어온 경우 위의 남은 작업과 단일 소유 계획을 따른다.

<span id="1-배치와-현재-적용-범위"></span>
<span id="2-w9--preferences--project-settings-소유권저장-계약-phase-21-미산정"></span>
<span id="3-14-live--rendering-live-완료-계약-phase-14-미산정"></span>
<span id="4-추적과-누락-방지"></span>
<span id="sec-1-배치와-현재-적용-범위"></span>
<span id="sec-2-w9--preferences--project-settings-소유권저장-계약-phase-21-미산정"></span>
<span id="sec-3-14-live--rendering-live-완료-계약-phase-14-미산정"></span>
<span id="sec-4-추적과-누락-방지"></span>
<span id="w9-완료-게이트"></span>

</details>
