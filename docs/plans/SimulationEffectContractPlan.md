# PrSM 기반 시뮬레이션 효과 계약 및 도입 계획 (PHASE 24)

> 2026-10-20 정리 · master `27de3f5a02aadde7456aab680db3184c36ee735e` · 잔여 구현·수용 계획

판정 기준은 위 master의 커밋된 소스다. 작업 중인 로컬 코드 변경은 포함하지 않는다. 완료된 구현 단계·날짜별 실행 일지는 제거했다. 대시보드는 SRP 저작 전환 취소에 해당하는 항목만 제거하고, 나머지 상태·공수·링크를 보존한다.

[전체 문서 대조·충돌 판정](MasterPlanAudit20261020.html) · [정리 전 원문](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/docs/plans/SimulationEffectContractPlan.md)

## 남은 작업과 완료 판정

- PrSM0~5와 효과 런타임: 폐쇄 효과 카탈로그·즉시 효과/중단 분리·Var<T>, routine cleanup/transaction 의미를 엔진 타깃 변환·등록·직렬화·build/reload/진단까지 연결한다.
- 현재 ContactStream·OnBeginSimulation/Scope와 생명주기 안정화는 기존 구현을 재사용한다. 별도 Job/PhysicsBehaviour 저작 방안을 되살리지 않고 PrSM 제품 도입 수용과 구분한다.

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

<span id="0-결정한-목표와-착수-조건"></span>
<span id="1-결정--두-절반을-합치되-즉시-효과와-중단을-가른다"></span>
<span id="2-과거-참조-구현-검증-기록--엔진-도입-증거와-구분"></span>
<span id="2026-10-08-물리-접촉-작업과의-경계"></span>
<span id="3-목표-실행-계약과-c-생성-표면"></span>
<span id="4-엔진-계약-트랙--전체-미래-보류"></span>
<span id="5-미래-착수-순서와-다른-계획의-경계"></span>
<span id="6-이-페이즈가-지키는-원칙"></span>
<span id="7-prsm-수정creatorengine-도입-트랙"></span>
<span id="71-조사-기준과-재사용-경계"></span>
<span id="8-최종-완료-판정"></span>
<span id="c-백엔드-기준-예시"></span>
<span id="e0--대기-원시와-awaiter-계약-p0--0-재개-조건3-실행-의미-선행"></span>
<span id="e1--폐쇄-카탈로그와-중단-원시-p0--e0-선행"></span>
<span id="e2--부수효과-분류와-공통-카탈로그-p0--0-재개-조건-선행"></span>
<span id="e3--효과-기록재생-p1--e1e2-선행"></span>
<span id="e4--실패-결과와-루틴-진단-통합-p1--e0현행-안정화-lc1lc2-선행"></span>
<span id="e5--진단-표면-p2--e1prsm4-연계"></span>
<span id="e6--할당실행-시간-예산-p2--e1-선행-최종-판정은-prsm5"></span>
<span id="e7--정적-계약-검사기-p1--e1e2-선행"></span>
<span id="e8--트랜잭션-셀-p2--e2e7-선행"></span>
<span id="prsm0--대상-프로필-분리-미래-착수-첫-단계"></span>
<span id="prsm1--기존-컴포넌트직렬화등록-경로-연결-prsm0-선행"></span>
<span id="prsm2--효과중단수명-의미-분석-e2prsm0-선행-e7과-공동-검증"></span>
<span id="prsm3--루틴정리트랜잭션-lowering-e0e1prsm12-선행-트랜잭션은-e8-선행"></span>
<span id="prsm4--빌드에디터진단리로드-연결-prsm1-이후-점진-연결-e5-연계"></span>
<span id="prsm5--실제-콘텐츠-도입과-비교-검증-필수-엔진prsm-트랙-통합"></span>
<span id="sec-0-결정한-목표와-착수-조건"></span>
<span id="sec-1-결정--두-절반을-합치되-즉시-효과와-중단을-가른다"></span>
<span id="sec-2-과거-참조-구현-검증-기록--엔진-도입-증거와-구분"></span>
<span id="sec-3-목표-실행-계약과-c-생성-표면"></span>
<span id="sec-4-엔진-계약-트랙--전체-미래-보류"></span>
<span id="sec-5-미래-착수-순서와-다른-계획의-경계"></span>
<span id="sec-6-이-페이즈가-지키는-원칙"></span>
<span id="sec-7-prsm-수정creatorengine-도입-트랙"></span>
<span id="sec-8-최종-완료-판정"></span>
<span id="결정-이력--제네릭-판을-왜-버렸나"></span>
<span id="다른-계획과의-경계"></span>
<span id="어휘-창구-simulation"></span>
<span id="왜-금지가-아니라-라우팅인가"></span>
<span id="이-형태가-치르는-값"></span>
<span id="전환-전에-확정할-실행-의미"></span>
<span id="중단은-실행기-원시다"></span>
<span id="참조-구현-creatorengineverselike-2026-09-05-오후"></span>
<span id="초안-프로토타입-제네릭-판-2026-09-05-오전-net-10--c-14-release"></span>
<span id="취소-모델--finally를-믿지-않는다"></span>
<span id="폐쇄-카탈로그--어휘-전부를-다뤘는지-컴파일이-강제한다"></span>
<span id="현재-관측은-미래-이관-규모의-확정값이-아니다"></span>

</details>
