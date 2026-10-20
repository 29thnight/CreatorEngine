# 스크립트 표면 재설계 (PHASE 9.5)

> 2026-10-20 정리 · master `27de3f5a02aadde7456aab680db3184c36ee735e` · 잔여 구현·수용 계획

판정 기준은 위 master의 커밋된 소스다. 작업 중인 로컬 코드 변경은 포함하지 않는다. 완료된 구현 단계·날짜별 실행 일지는 제거했다. 대시보드는 SRP 저작 전환 취소에 해당하는 항목만 제거하고, 나머지 상태·공수·링크를 보존한다.

[전체 문서 대조·충돌 판정](MasterPlanAudit20261020.html) · [정리 전 원문](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/docs/plans/ScriptSurfacePlan.md)

## 남은 작업과 완료 판정

- W3: InputAction의 이름뿐 아니라 타입/값을 managed 호출로 운반하는 통로와 오류/ABI/순서 검증을 닫는다.
- W4/5: Inspector/Undo·스크립트 setter의 dirty publication부터 실제 render proxy 반영까지 같은 프레임/세대로 관측한다. 값 왕복만으로 배선 수용을 대신하지 않는다.
- W6/8: native/managed lifecycle·field/message layout mirror와 훅 dispatch override 검증을 닫는다. W7의 옛 Transform/registry 문서 정리는 이번 계획 정리에서 완료 대상으로 제거한다.

## 적용할 구조와 경계

`managed component → native API table/handle → SceneRuntime → proxy publication`

위 경로는 소스에서 확인한 구현 기반이다. 아래 요구의 수용 결과를 뜻하지 않는다. 구현을 다시 만드는 항목과 제품 검증을 혼합하지 않는다.

## 추적할 잔여 항목

| 추적 ID | 소유 단위 (잔여 작업은 위 목록) | 대시보드 보존 상태 |
|---|---|---|
| `W3` · PHASE 9.5 | 입력 액션 값 통로 — 이름만 넘고 값이 끊긴다 | todo |
| `W4` · PHASE 9.5 | 저작 dirty 통로 — 인스펙터가 값만 대입하고 dirty를 모른다 | todo |
| `W5` · PHASE 9.5 | 렌더 델타 관측 하네스 — 헤드리스가 프록시 적용을 못 본다 | todo |
| `W6` · PHASE 9.5 | 미러 커버리지 — 검사 없는 경계 계약 3종 + SerializeField 왕복 | todo |
| `W8` · PHASE 9.5 | 디스패치 오버라이드 감지 | todo |

대시보드 상태는 인계용 식별 정보다. 위 구현 기반에서 이미 충족한 하위 요구는 재작성하지 않고, 남은 제품/실패/비용 수용을 수행한다.

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

<span id="0-왜-이-페이즈가-필요한가"></span>
<span id="1-완료분"></span>
<span id="2-잔여-트랙"></span>
<span id="3-착수-순서와-의존"></span>
<span id="4-이-페이즈가-지키는-원칙"></span>
<span id="sec-0-왜-이-페이즈가-필요한가"></span>
<span id="sec-1-완료분"></span>
<span id="sec-2-잔여-트랙"></span>
<span id="sec-3-착수-순서와-의존"></span>
<span id="sec-4-이-페이즈가-지키는-원칙"></span>
<span id="w1--골격-완료-79ac93ae--2026-09-04"></span>
<span id="w2--컴포넌트-노출-완료-84419886ebab7c7f--2026-09-04"></span>
<span id="w3--입력-액션-값-통로-대기--p1--4일"></span>
<span id="w4--저작-dirty-통로-대기--p1--3일"></span>
<span id="w5--렌더-델타-관측-하네스-대기--p2--3일"></span>
<span id="w6--미러-커버리지-대기--p2--2일"></span>
<span id="w7--문서-청산-대기--p3--1일"></span>
<span id="w8--디스패치-오버라이드-감지-대기--p1--1일"></span>

</details>
