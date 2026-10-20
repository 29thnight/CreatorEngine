# 행동 트리 C# 우선 재설계 — 실행 설계

> 2026-10-20 정리 · master `27de3f5a02aadde7456aab680db3184c36ee735e` · 종료/승계 문서

판정 기준은 위 master의 커밋된 소스다. 작업 중인 로컬 코드 변경은 포함하지 않는다. 완료된 구현 단계·날짜별 실행 일지는 제거했다. 대시보드는 SRP 저작 전환 취소에 해당하는 항목만 제거하고, 나머지 상태·공수·링크를 보존한다.

[전체 문서 대조·충돌 판정](../MasterPlanAudit20261020.html) · [정리 전 원문](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/docs/plans/archive/BehaviorTreeManagedPlan.md)

## 남은 작업과 완료 판정

**단일 소유 계획:** [LatticeAdoptionPlan](../LatticeAdoptionPlan.md). 이 문서는 이전 진입 링크를 보존하는 인계 문서다.

- 이 문서의 완료·대체된 구현 단계는 활성 계획에서 제거했다. 남은 후속은 아래 단일 소유 계획을 따른다.
- 과거 계획에 남은 게임 BT 콘텐츠 43종의 이식은 엔진 기반 완료와 별개다. 현재 콘텐츠 인벤토리를 재확인하고 실제 콘텐츠/Player 수용을 닫는다.

## 적용할 구조와 경계

`managed component → native API table/handle → SceneRuntime → proxy publication`

위 경로는 소스에서 확인한 구현 기반이다. 아래 요구의 수용 결과를 뜻하지 않는다. 구현을 다시 만드는 항목과 제품 검증을 혼합하지 않는다.

## master 소스 근거

모든 행의 파일·행 번호·Git blob은 고정 master에서 대조했다. 소스 존재·배선 확인과 실행 성공을 구분한다.

- [ScriptCore/ScriptRegistry.cs:10](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/ScriptCore/ScriptRegistry.cs#L10) — `internal static class ScriptRegistry`
- [Engine/SceneRuntime/PlayerInput.cpp:33](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/PlayerInput.cpp#L33) — `m_actionMap->CheckAction(controllerIndex, script->m_scriptType,`
- [Engine/SceneRuntime/InputAction.h:8](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/InputAction.h#L8) — `enum class ActionType`
- [ScriptCore/Component.cs:22](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/ScriptCore/Component.cs#L22) — `public abstract class Component`

## 검증 경계

이번 정리는 master 소스·문서 전체의 소유권/충돌/링크/HTML 검증이다. 엔진 빌드·GPU 픽셀·SDK/장치·성능·실제 GUI 수용을 새로 실행한 결과가 아니다. 과거 통과는 해당 소스/바이너리 신원에만 유효하다. 남은 제품 수용은 위 요구를 실제 산출물로 닫아야 한다.

<details><summary>이전 절 링크 호환</summary>

이전 절의 완료 이력·구안은 제거했다. 이 링크로 들어온 경우 위의 남은 작업과 단일 소유 계획을 따른다.

<span id="1-왜-이-방향인가"></span>
<span id="2-경계-계약"></span>
<span id="3-옮길-것과-남길-것"></span>
<span id="4-슬라이스"></span>
<span id="5-위험"></span>
<span id="6-완료-기준"></span>
<span id="sec-1-왜-이-방향인가"></span>
<span id="sec-2-경계-계약"></span>
<span id="sec-3-옮길-것과-남길-것"></span>
<span id="sec-4-슬라이스"></span>
<span id="sec-5-위험"></span>
<span id="sec-6-완료-기준"></span>

</details>
