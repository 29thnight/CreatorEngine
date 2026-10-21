# Editor 명령 / Commandlet 경계

> 2026-10-20 정리 · master `27de3f5a02aadde7456aab680db3184c36ee735e` · 잔여 구현·수용 계획

판정 기준은 위 master의 커밋된 소스다. 작업 중인 로컬 코드 변경은 포함하지 않는다. 완료된 구현 단계·날짜별 실행 일지는 제거했다. 대시보드는 SRP 저작 전환 취소에 해당하는 항목만 제거하고, 나머지 상태·공수·링크를 보존한다.

[전체 문서 대조·충돌 판정](MasterPlanAudit20261020.html) · [정리 전 원문](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/docs/plans/EditorAutomationCLIPlan.md)

## 남은 작업과 완료 판정

- LC9의 GUI 수동 Undo/Redo 경로를 기존 Phase14_5ManualGuiChecklist의 미확인 항목까지 확인한다. HTTP/Commandlet·공통 편집 API와 Shipping 격리는 이미 구현된 기반이다.
- LC5 drain·LC8 Player 서비스 검증기의 과거 잘못된 가정을 현재 계약에 맞게 검증한다. 검증기 수정과 제품 오류를 구분하고 전체 회귀 통과로 확대하지 않는다.

## 적용할 구조와 경계

`Editor window/commands → common editing transaction → scene component → dirty publication`

위 경로는 소스에서 확인한 구현 기반이다. 아래 요구의 수용 결과를 뜻하지 않는다. 구현을 다시 만드는 항목과 제품 검증을 혼합하지 않는다.

## 추적할 잔여 항목

| 추적 ID | 소유 단위 (잔여 작업은 위 목록) | 대시보드 보존 상태 |
|---|---|---|
| `LC9` · PHASE 14.5 | 소비자 이관과 최종 계약 검증 | progress |

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

<span id="2026-09-06-후속-정리"></span>
<span id="2026-10-07-최근-병합-반영"></span>
<span id="commandlet-사용"></span>
<span id="sec-2026-09-06-후속-정리"></span>
<span id="sec-2026-10-07-최근-병합-반영"></span>
<span id="sec-commandlet-사용"></span>
<span id="sec-검증-기준"></span>
<span id="sec-구현-순서와-결과물"></span>
<span id="sec-마무리-반영"></span>
<span id="sec-목표"></span>
<span id="sec-소유권"></span>
<span id="sec-편집-http-계약"></span>
<span id="검증-기준"></span>
<span id="구현-순서와-결과물"></span>
<span id="마무리-반영"></span>
<span id="목표"></span>
<span id="소유권"></span>
<span id="종결-조건-2026-09-15-재정의"></span>
<span id="편집-http-계약"></span>

</details>
