# 효과 계약 도입 전 스크립트 생명주기·Task 계약 안정화 계획

> 2026-10-20 정리 · master `27de3f5a02aadde7456aab680db3184c36ee735e` · 종료/승계 문서

판정 기준은 위 master의 커밋된 소스다. 작업 중인 로컬 코드 변경은 포함하지 않는다. 완료된 구현 단계·날짜별 실행 일지는 제거했다. 대시보드는 SRP 저작 전환 취소에 해당하는 항목만 제거하고, 나머지 상태·공수·링크를 보존한다.

[전체 문서 대조·충돌 판정](../MasterPlanAudit20261020.html) · [정리 전 원문](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/docs/plans/archive/ScriptLifecycleContractHardeningPlan.md)

## 남은 작업과 완료 판정

**단일 소유 계획:** [ScriptSurfacePlan](../ScriptSurfacePlan.md). 이 문서는 이전 진입 링크를 보존하는 인계 문서다.

- 이 문서의 완료·대체된 구현 단계는 활성 계획에서 제거했다. 남은 후속은 아래 단일 소유 계획을 따른다.
- LC7의 pause 중 대기·세대 오류 주입 수용은 별도 확인이 필요하다. LC6의 보류된 최적화를 구현 완료로 바꾸지 않는다. PrSM 도입은 PHASE24가 소유한다.

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

<span id="0-판단과-범위"></span>
<span id="1-현재-생명주기와-실행-순서"></span>
<span id="11-소유자와-호출-경로"></span>
<span id="12-보존할-정상-동작"></span>
<span id="2-확인한-문제와-위험-수준"></span>
<span id="21-조사-중-수행한-격리-시험"></span>
<span id="22-실엔진-실패-픽스처-실측-2026-09-05"></span>
<span id="3-독립-안정화-트랙"></span>
<span id="4-적용-순서와-미래-계획에-넘길-결정"></span>
<span id="5-계약을-당장-도입하지-않을-때의-남는-한계"></span>
<span id="6-완료-및-보고-기준"></span>
<span id="lc0--관리-생명주기-기준선과-실패-픽스처-p1--선행"></span>
<span id="lc1--단계-성공과-실행-허용-상태-정리-p1--lc0-선행"></span>
<span id="lc2--취소정리-예외-격리와-종료-보장-p1--lc0-선행"></span>
<span id="lc3--완료된-대기의-취소-등록-해제-p1--lc0-선행"></span>
<span id="lc4--대기-완료취소파괴-재진입-p1--lc0-선행-lc2lc3와-통합-검증"></span>
<span id="lc5--외부-await엔진-스레드task-실패-정책-p1--lc0-선행"></span>
<span id="lc6--기존-틱스코프-비용의-선택적-개선-p2--lc0-기준선-w8과-조정"></span>
<span id="lc7--실제-엔진-통합-회귀와-종료-판정-p1--필수-트랙-통합"></span>
<span id="sec-0-판단과-범위"></span>
<span id="sec-1-현재-생명주기와-실행-순서"></span>
<span id="sec-2-확인한-문제와-위험-수준"></span>
<span id="sec-3-독립-안정화-트랙"></span>
<span id="sec-4-적용-순서와-미래-계획에-넘길-결정"></span>
<span id="sec-5-계약을-당장-도입하지-않을-때의-남는-한계"></span>
<span id="sec-6-완료-및-보고-기준"></span>
<span id="①-범위를-못-박았다--그리고-w8의-분모가-비용과-무관하다는-것이-드러났다"></span>
<span id="②-lc5-c-진입-검사의-비용을-쟀다--분해능-아래다"></span>
<span id="③-남긴-것과-다시-재야-할-때"></span>
<span id="착지--측정-후-변경-보류-2026-09-06"></span>
<span id="착지-2026-09-05"></span>

</details>
