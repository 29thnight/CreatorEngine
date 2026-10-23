# PrSM 기반 시뮬레이션 효과 계약 및 도입 계획 (PHASE 24)

> 2026-10-23 최신 결정: 게임 저작과 공통 관리 실행기는 F#이다. 컴포넌트 생명주기는 **override 없는 일반 멤버 → 생성기의 정적 콜백 등록**, 고정 틱은 **OnTickSimulate(dt), Script API45**다. ScriptCore의 C# 코드는 네이티브 ABI·값 형식·바인딩 진입점으로 유지한다. 수명·세대 핸들·실패 reload 보존은 유지한다. [현재 계약](../design/FSharpScriptingContract.md)과 [API45 수용 기록](../analysis/FSharpManagedRuntimeAcceptance20261023.md)을 따른다. 아래 API43/44·C# 기본 저작 기록은 이전 단계이며 OnSimulate·Scope.Delay·simulation CE는 복원하지 않는다.


> 2026-10-20 정리 · master `27de3f5a02aadde7456aab680db3184c36ee735e` · 잔여 구현·수용 계획

> 2026-10-22 사용자 결정 반영: C# 계약 도입 중단, PrSM 기존 LLVM 언어 작업으로 이관. 아래 master 근거는 엔진 기반의 과거 대조이며 PrSM 최신 구현 증거는 별도로 명시한다.

판정 기준은 위 master의 커밋된 소스다. 작업 중인 로컬 코드 변경은 포함하지 않는다. 완료된 구현 단계·날짜별 실행 일지는 제거했다. 대시보드는 SRP 저작 전환 취소에 해당하는 항목만 제거하고, 나머지 상태·공수·링크를 보존한다.

[전체 문서 대조·충돌 판정](MasterPlanAudit20261020.html) · [정리 전 원문](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/docs/plans/SimulationEffectContractPlan.md)

## 남은 작업과 완료 판정

- 2026-10-22 결정: C# Verse식 효과·중단·실패·트랜잭션 저작 계약 도입은 중단한다. 언어 구문·타입/효과 검사·실행 의미의 소유자는 기존 PrSM LLVM 저장소다. C#은 일반 저작으로 복귀하고 Scope.Delay·공개 Scope·OnSimulate 전용 코루틴 저작 표면도 제거한다. C#은 C++ 계약 OnUpdate(dt)로 대체하고 PrSM 계약 도입 전까지 고정 틱으로 실행한다.
- PrSM의 기존 compiler/ABI/중단 실행을 재사용하고, 미지원 언어 계약과 CreatorEngine 제품 adapter를 구분해 구현·수용한다. 기존 C# OnSimulate/Scope 전용 저작 표면의 소비자 이주·제거는 ScriptSurfacePlan이 소유한다. 이를 PrSM용 C# 계약 구조로 재작성하지 않는다.

## 적용할 구조와 경계

`PrSM source → syntax/semantics/effect checks → LLVM → existing PrSM ABI → CreatorEngine host adapter`

C#은 기본 managed component/native API 경로로 복귀하고 공개 전용 coroutine/Scope 저작을 제거한다. 공통 tick·스레드·권한·수명 경계는 엔진이 소유하고 언어의 효과 검사와 구분한다. 고정 틱/서버 수용은 [NetworkFrameworkPlan](NetworkFrameworkPlan.md), C# 정책은 [ScriptSurfacePlan](ScriptSurfacePlan.md)이 소유한다.

## C# 이관 구현 — 2026-10-22

C# 전용 OnSimulate/Scope.Delay 저작은 제거했고 C++ 소유 OnUpdate 고정 틱으로 이주했다. 내부 접촉 구독 수명·일반 C# await 마셜링을 보존했으며 [Windows 수용 기록](../analysis/CSharpFixedUpdateAcceptance20261022.md)을 남겼다. 이는 PrSM 저장소의 언어 구문·LLVM·효과/실패 계약 구현 또는 CreatorEngine PrSM adapter 완료가 아니다. PrSM 저장소는 이번 작업에서 변경하지 않았다.

## PrSM 기존 작업 참조 — 2026-10-22

정본 저장소는 [29thnight/PrSM](https://github.com/29thnight/PrSM)이다. 공개 웹 조회는 404였으며, origin이 같은 로컬 checkout의 HEAD `53ad8d7683d6b7f60ad86c15f0dd95691eb0d296`와 README·docs/implementation-status.md·docs/spec/v1-policy.md를 읽어 대조했다. 원격 최신 commit 확인 또는 이번 실행 검증을 뜻하지 않는다.

- 기존 LLVM/GCCE PrSM은 contract fn, Component metadata, suspend coroutine, gather/first 일부, defer·협조적 취소, ORC/AOT·reload 모의 호스트 경로가 구현된 bounded profile로 기록돼 있다. 이를 새로 만드는 계획으로 세지 않는다.
- 전체 effect inference·launch·일반 언어 기능과 CreatorEngine production 도입은 잔여 또는 미검증이다. contract fn의 엔진 훅 연결이 Verse식 실패/효과/트랜잭션 전체 구현을 뜻하지 않는다. 52개 정책 결정도 전체 언어 적합 구현과 구분한다.
- 이 저장소는 PrSM for Unity와 별개다. 기존 v1 정책·진단·ABI·테스트 구조에서 확장하고 언어 명세를 CreatorEngine 문서에 복제하지 않는다. LLVM IR을 직접 저작하라는 요구가 아니라 PrSM 소스의 LLVM 변환 경로를 사용한다.

## 중단과 이관 범위

| 기존 요구 | 변경된 소유와 상태 |
|---|---|
| C# 전용 awaiter/효과 카탈로그/정적 계약 검사/트랜잭션 저작 강제 | 중단. C# 전용 Scope/OnSimulate 저작 API도 제거 대상으로 두고 내부 생명주기 안전성은 유지 |
| E0/E1 중단 원시와 재개 규칙 | PrSM 기존 suspend 구현 대조 후 v1 명세에서 잔여만 확정 |
| E2/E7 효과·실패 문맥과 정적 검사 | PrSM 언어 명세·의미 분석으로 이관. C# 분석기를 선행시키지 않음 |
| E3/E4/E5 기록·재생·오류·진단 | 공통 엔진 관측과 PrSM 언어 진단을 구분해 연계 |
| E6 실행·할당 예산 | PrSM 실제 콘텐츠와 engine adapter에서 측정 |
| E8 트랜잭션/rollback | PrSM의 실패 범위·허용 효과·외부 부작용 정책을 먼저 명세. C# Var<T> 구조 도입은 중단 |
| PrSM0~5 | 기존 compiler/ABI 성과를 대조해 잔여만 진행. 엔진 등록·build/reload·제품 수용은 별도 |

공통 N3 clock의 실제 FPS·CCT·GPU 보간 수용은 [2026-10-23 기록](../analysis/N3PacingAcceptance20261023.md)을 따른다. C#에 Verse식 저작 구조를 되넣지 않으며 PrSM adapter와 언어 계약은 후속 소유다.

## 착수와 완료 기준

1. PrSM 저장소의 각 계약에 대해 명세/구현/테스트를 대응시킨다. 중단 가능한 호출, sync/suspend 효과 전파, 실패와 panic 구분, 취소/cleanup 순서를 명세하고 미지원 구문은 명확히 진단한다.
2. Verse식 계약의 실제 구문은 PrSM의 기존 contract fn·suspend·expected/handler 등과 충돌 없이 언어 저장소에서 결정한다. 본 문서는 새 키워드/구문을 임의로 확정하지 않는다.
3. 트랜잭션은 되돌릴 수 있는 상태와 되돌릴 수 없는 엔진 부작용을 구분한다. 임의 C++ 호출·네트워크·오디오 같은 부작용이 자동 rollback된다고 보장하지 않는다.
4. LLVM lowering·ORC/AOT, 중단 frame·GC root·취소·코드 세대/reload의 정상/실패 검사를 기존 하네스에 확장한다. 모의 호스트 통과와 실제 CreatorEngine 수용을 분리한다.
5. production adapter·등록·직렬화·진단·빌드/리로드를 실제 Editor/cooked Player 콘텐츠로 검증한다. 공통 clock과 tick 명령 경계는 N3 계약을 따르고 C# Verse 런타임 완성을 기다리지 않는다.

## master 소스 근거

모든 행의 파일·행 번호·Git blob은 고정 master에서 대조했다. 소스 존재·배선 확인과 실행 성공을 구분한다.

- [ScriptCore/ScriptRegistry.cs:10](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/ScriptCore/ScriptRegistry.cs#L10) — `internal static class ScriptRegistry`
- [Engine/SceneRuntime/PlayerInput.cpp:33](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/PlayerInput.cpp#L33) — `m_actionMap->CheckAction(controllerIndex, script->m_scriptType,`
- [Engine/SceneRuntime/InputAction.h:8](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/InputAction.h#L8) — `enum class ActionType`
- [ScriptCore/Component.cs:22](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/ScriptCore/Component.cs#L22) — `public abstract class Component`

## 검증 경계

2026-10-20 고정 master의 문서 감사와 2026-10-22 현재 작업 트리의 구현·제품 수용은 별도다. 새로 실행한 빌드·Editor/신규 cooked Player와 코드 신원은 [수용 기록](../analysis/CSharpFixedUpdateAcceptance20261022.md)에 한정한다. 서버/PrSM 전체, 대표 성능·GPU 픽셀·Debug/Shipping·다른 OS 수용으로 확대하지 않는다.

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
