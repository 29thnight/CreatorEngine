# UI 시스템 재설계 — Scene 소유 UI Runtime과 값 타입 렌더 제출 (PHASE 16)

> 2026-10-20 정리 · master `27de3f5a02aadde7456aab680db3184c36ee735e` · 잔여 구현·수용 계획

판정 기준은 위 master의 커밋된 소스다. 작업 중인 로컬 코드 변경은 포함하지 않는다. 완료된 구현 단계·날짜별 실행 일지는 제거했다. 대시보드는 SRP 저작 전환 취소에 해당하는 항목만 제거하고, 나머지 상태·공수·링크를 보존한다.

[전체 문서 대조·충돌 판정](MasterPlanAudit20261020.html) · [정리 전 원문](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/docs/plans/UISystemRedesignPlan.md)

## 남은 작업과 완료 판정

- U0/T/U1~7: UI/Transform 진실의 저장소, scene-local 입력 routing·layout dirty·값 UIDrawSnapshot·그리기 기능과 Editor/직렬화의 남은 수직 슬라이스를 구현·수용한다.
- 현재 Canvas/UIManager 소비자·이벤트·GC trace/수명을 조사해 전역 UIManager 퇴역을 단계화한다. 스냅샷은 render thread가 live Component를 역참조하지 않도록 한다.

## 적용할 구조와 경계

`UI components → UIManager/Canvas → prepared render data → UI rendering`

위 경로는 소스에서 확인한 구현 기반이다. 아래 요구의 수용 결과를 뜻하지 않는다. 구현을 다시 만드는 항목과 제품 검증을 혼합하지 않는다.

## 미래 공통 UI 기반과의 경계

2026-10-28 등록한 [엔진 공통 UI 기반과 자체 편집기 UI 전환 계획](NativeUIFrameworkPlan.md)은 현재 리팩토링 이후의 FUI 미래 항목이다. 본 계획은 Scene·Canvas·Transform·게임 UI 저작/직렬화와 U/T 수용을 계속 소유한다. FUI는 Scene에 독립적인 공통 기반과 어댑터 적합성을 소유하며, 중첩되는 텍스트/폰트 구현의 소유자는 착수 시 한 곳으로 정한다. FUI 등록으로 현재 작업을 중단하거나 U/T 완료를 중복 산정하지 않는다.

## 추적할 잔여 항목

| 추적 ID | 소유 단위 (잔여 작업은 위 목록) | 대시보드 보존 상태 |
|---|---|---|
| `U0-a` · PHASE 16 | 저작·측정 표면 신설 — 자를 먼저 세운다 | todo |
| `U0-b` · PHASE 16 | 지혈 — C1·C3·dead path | todo |
| `U0-c` · PHASE 16 | 게이트 보강 — 눈먼 초록 제거 | todo |
| `T0` · PHASE 16 | 트랙 T — 폰트 자산과 SDF 아틀라스 정본 | todo |
| `T1` · PHASE 16 | 트랙 T — 텍스트 draw item | todo |
| `T2` · PHASE 16 | 트랙 T — 스케일·해상도 대응 | todo |
| `U1` · PHASE 16 | UISceneContext와 shadow registry | todo |
| `U2` · PHASE 16 | 등록 통합과 장기 raw pointer 제거 | progress |
| `U3` · PHASE 16 | InputRouter 통합 | todo |
| `U4a` · PHASE 16 | Layout dirty propagation | todo |
| `U4b` · PHASE 16 | Layout 컴포넌트 — LayoutGroup·ContentSizeFitter | todo |
| `U5a` · PHASE 16 | 값 타입 UIDrawSnapshot | todo |
| `U5b` · PHASE 16 | 렌더 기능 완성 | todo |
| `U6` · PHASE 16 | 에디터·직렬화 정합 + C# UI 표면 재정렬 | todo |
| `U7` · PHASE 16 | 전역 UIManager 퇴역 | todo |

대시보드 상태는 인계용 식별 정보다. 위 구현 기반에서 이미 충족한 하위 요구는 재작성하지 않고, 남은 제품/실패/비용 수용을 수행한다.

## master 소스 근거

모든 행의 파일·행 번호·Git blob은 고정 master에서 대조했다. 소스 존재·배선 확인과 실행 성공을 구분한다.

- [Engine/SceneRuntime/UIManager.h:16](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/UIManager.h#L16) — `class UIManager : public Singleton<UIManager>`
- [Engine/SceneRuntime/Canvas.h:7](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/Canvas.h#L7) — `#include "CanvasScaleMode.h"`
- [Engine/SceneRuntime/UIButton.h:9](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/UIButton.h#L9) — `class [[reflgen::reflect]] UIButton : public meta::identity<UIButton, UIComponent>`

## 검증 경계

이번 정리는 master 소스·문서 전체의 소유권/충돌/링크/HTML 검증이다. 엔진 빌드·GPU 픽셀·SDK/장치·성능·실제 GUI 수용을 새로 실행한 결과가 아니다. 과거 통과는 해당 소스/바이너리 신원에만 유효하다. 남은 제품 수용은 위 요구를 실제 산출물로 닫아야 한다.

<details><summary>이전 절 링크 호환</summary>

이전 절의 완료 이력·구안은 제거했다. 이 링크로 들어온 경우 위의 남은 작업과 단일 소유 계획을 따른다.

<span id="0-개정-사유"></span>
<span id="00-2026-08-30-전면-개정"></span>
<span id="01-최종-목표-유지"></span>
<span id="02-채택-모델-유지"></span>
<span id="03-인접-계획과의-경계"></span>
<span id="04-이번-개정에서-신설하는-것--트랙-t-텍스트-렌더"></span>
<span id="05-기존-계획에서-제거하는-항목"></span>
<span id="06-2026-09-15-3차-개정--레이어-판정--인접-페이즈-접점--트랙-t-승격"></span>
<span id="1-현재-코드-기준선--2026-08-30-실측"></span>
<span id="10-순서-제약"></span>
<span id="11-구조-지도"></span>
<span id="11-삭제-및-마이그레이션-표"></span>
<span id="12-이미-닫힌-기반-재확인"></span>
<span id="12-필수-불변식"></span>
<span id="13-살아-있는-결함--구-c1c10-재판정"></span>
<span id="13-위험과-대응"></span>
<span id="14-명시적-비범위"></span>
<span id="14-새로-드러난-것-기존-계획서에-없음"></span>
<span id="15-제거-후보-dead-path-재확인--전부-유효"></span>
<span id="16-회귀-기준선--2026-08-30-실행-결과"></span>
<span id="17-저작관측-표면-인벤토리"></span>
<span id="2-설계-원칙"></span>
<span id="21-단일-소유권"></span>
<span id="22-단일-hierarchy"></span>
<span id="23-명시적-mountunmount"></span>
<span id="24-resolve는-사용-지점에서-짧게"></span>
<span id="25-저작-데이터와-런타임-캐시-분리"></span>
<span id="26-타입-규약-신설"></span>
<span id="3-목표-참조-모델"></span>
<span id="31-엔티티-참조"></span>
<span id="32-ui-컴포넌트-참조"></span>
<span id="33-canvas-membership"></span>
<span id="34-navigation-참조"></span>
<span id="35-target-graphic"></span>
<span id="4-생명주기-계약"></span>
<span id="41-이벤트별-동작"></span>
<span id="42-ordering-불변식"></span>
<span id="43-thread-규칙"></span>
<span id="5-recttransform-좌표-계약"></span>
<span id="51-방향-규약-명시--기존-계획서-누락분"></span>
<span id="52-공개-의미"></span>
<span id="53-canonical-식"></span>
<span id="54-좌표-변환-규칙"></span>
<span id="55-검증-행렬"></span>
<span id="6-입력-아키텍처"></span>
<span id="61-uiinputrouter"></span>
<span id="62-capability-분리"></span>
<span id="63-상태-소유권"></span>
<span id="64-멀티-뷰-규칙-신설"></span>
<span id="7-layout과-invalidation"></span>
<span id="71-1차-목표"></span>
<span id="72-dirty-root-규칙"></span>
<span id="73-현재-구현에서-승계할-것"></span>
<span id="74-후속-기능-기초-안정화-후에만"></span>
<span id="8-렌더-제출-경계"></span>
<span id="81-최종-구조-타입-정정"></span>
<span id="82-전환-경로"></span>
<span id="83-공통-sort-key"></span>
<span id="84-클리핑-rhi-중립-제약"></span>
<span id="9-실행-계획"></span>
<span id="90-착수-전-판정-항목-works-배정-전에-결정한다"></span>
<span id="d-5-근거--ui-저작을-c-스크립트-레이어로-올린다가-지금-성립하지-않는-이유"></span>
<span id="sec-0-개정-사유"></span>
<span id="sec-1-현재-코드-기준선--2026-08-30-실측"></span>
<span id="sec-10-순서-제약"></span>
<span id="sec-11-삭제-및-마이그레이션-표"></span>
<span id="sec-12-필수-불변식"></span>
<span id="sec-13-위험과-대응"></span>
<span id="sec-14-명시적-비범위"></span>
<span id="sec-2-설계-원칙"></span>
<span id="sec-3-목표-참조-모델"></span>
<span id="sec-4-생명주기-계약"></span>
<span id="sec-5-recttransform-좌표-계약"></span>
<span id="sec-6-입력-아키텍처"></span>
<span id="sec-7-layout과-invalidation"></span>
<span id="sec-8-렌더-제출-경계"></span>
<span id="sec-9-실행-계획"></span>
<span id="u0-a--저작측정-표면-신설-선행--다른-모든-단계보다-먼저"></span>
<span id="u0-b--지혈"></span>
<span id="u0-c--게이트-보강-눈먼-초록-제거"></span>
<span id="u1--uiscenecontext와-shadow-registry"></span>
<span id="u2--등록-통합과-장기-raw-pointer-제거"></span>
<span id="u3--inputrouter-통합"></span>
<span id="u4a--layout-dirty-propagation"></span>
<span id="u4b--layout-컴포넌트"></span>
<span id="u5a--값-타입-uidrawsnapshot"></span>
<span id="u5b--렌더-기능-완성"></span>
<span id="u6--에디터와-직렬화"></span>
<span id="u7--전역-uimanager-퇴역"></span>
<span id="①-텍스트스프라이트시트는-소비자가-0이다--불일치가-아니라-생산만"></span>
<span id="②-updateuilayout은-ui가-아니라-씬-전체를-훑고-프레임당-3회-돈다"></span>
<span id="③-저작-ui-자산이-저장소에-하나도-없다"></span>
<span id="④-ddol-게이트에-사각지대--멤버십-재구축이-무측정"></span>
<span id="⑤-uimanager의-포인터-목록은-등록부가-아니라-작업-목록이다"></span>
<span id="⑥-위젯-하나의-팬아웃이-34파일이다"></span>
<span id="⑦-폰트-계통은-미구현이-아니라-은퇴가-남긴-고아다-9-15-실측"></span>
<span id="⑧-ui-런타임에-스레딩-코드가-0이다--enkits-보강-대상이-아니다"></span>
<span id="트랙-t--텍스트-렌더-신설--04--9-15-개정에서-u1-앞으로-승격"></span>

</details>
