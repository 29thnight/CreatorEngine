# Transform 갱신 구조·성능 재설계 (PHASE 8.75)

> 2026-10-20 정리 · master `27de3f5a02aadde7456aab680db3184c36ee735e` · 종료/승계 문서

판정 기준은 위 master의 커밋된 소스다. 작업 중인 로컬 코드 변경은 포함하지 않는다. 완료된 구현 단계·날짜별 실행 일지는 제거했다. 대시보드는 SRP 저작 전환 취소에 해당하는 항목만 제거하고, 나머지 상태·공수·링크를 보존한다.

[전체 문서 대조·충돌 판정](../MasterPlanAudit20261020.html) · [정리 전 원문](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/docs/plans/archive/TransformUpdatePlan.md)

## 남은 작업과 완료 판정

**단일 소유 계획:** [NativeObjectMemoryPolicyPlan](../NativeObjectMemoryPolicyPlan.md). 이 문서는 이전 진입 링크를 보존하는 인계 문서다.

- 이 문서의 완료·대체된 구현 단계는 활성 계획에서 제거했다. 남은 후속은 아래 단일 소유 계획을 따른다.

## 적용할 구조와 경계

`SceneManager → GC roots → Scene → Entity → Component; handle invalidation and lifecycle cleanup precede collection`

위 경로는 소스에서 확인한 구현 기반이다. 아래 요구의 수용 결과를 뜻하지 않는다. 구현을 다시 만드는 항목과 제품 검증을 혼합하지 않는다.

## master 소스 근거

모든 행의 파일·행 번호·Git blob은 고정 master에서 대조했다. 소스 존재·배선 확인과 실행 성공을 구분한다.

- [Engine/SceneRuntime/SceneManager.h:33](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/SceneManager.h#L33) — `gc::domain m_gcDomain;`
- [Engine/SceneRuntime/Scene.h:276](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/Scene.h#L276) — `class [[reflgen::reflect]] Scene : public gc::managed`
- [Engine/SceneRuntime/Entity.h:21](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/Entity.h#L21) — `class [[reflgen::reflect]] Entity : public Object, public gc::managed`
- [Engine/SceneRuntime/Component.h:14](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/Component.h#L14) — `class [[reflgen::reflect]] Component : public meta::identity<Component, Object>, public gc::managed`
- [Engine/SceneRuntime/SceneGC.h:5](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/SceneGC.h#L5) — `#include "../../ThirdParty/GCCE/include/gc/gc.hpp"`
- [Engine/SceneRuntime/DetachedEntityTransfer.h:12](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/DetachedEntityTransfer.h#L12) — `gc::root_ref<Entity> entity{};`
- [Engine/SceneRuntime/SceneManager.cpp:304](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/SceneManager.cpp#L304) — `gc::domain& SceneManager::ManagedDomain()`
- [Editor/RenderTests/Tasks/SceneGCSelfTest.cpp:161](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/RenderTests/Tasks/SceneGCSelfTest.cpp#L161) — `void gc_trace(gc::tracer& tracer) const override`
- [Tools/regression/verify-scene-gc.ps1:18](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-scene-gc.ps1#L18) — `'Editor/RenderTests/Tasks/SceneGCSelfTest.cpp',`

## 검증 경계

이번 정리는 master 소스·문서 전체의 소유권/충돌/링크/HTML 검증이다. 엔진 빌드·GPU 픽셀·SDK/장치·성능·실제 GUI 수용을 새로 실행한 결과가 아니다. 과거 통과는 해당 소스/바이너리 신원에만 유효하다. 남은 제품 수용은 위 요구를 실제 산출물로 닫아야 한다.

<details><summary>이전 절 링크 호환</summary>

이전 절의 완료 이력·구안은 제거했다. 이 링크로 들어온 경우 위의 남은 작업과 단일 소유 계획을 따른다.

<span id="0-판정-요약"></span>
<span id="1-현재-구조"></span>
<span id="10-데이터-표현"></span>
<span id="101-첫-착지는-world-matrix-유지"></span>
<span id="102-world-trs-전환은-기본-목표가-아니다"></span>
<span id="103-sparse-membership"></span>
<span id="11-계층-mutation-계약"></span>
<span id="111-단일-reparent"></span>
<span id="112-raw-mutation-폐쇄"></span>
<span id="12-실행-슬라이스"></span>
<span id="13-성능회귀-매트릭스"></span>
<span id="131-transform-시나리오"></span>
<span id="132-ui-시나리오"></span>
<span id="133-writer-시나리오"></span>
<span id="134-render-시나리오"></span>
<span id="135-판정-원칙"></span>
<span id="14-위험과-롤백-경계"></span>
<span id="15-이-계획이-하지-않는-것--기각"></span>
<span id="16-함정"></span>
<span id="17-scenegraphredesignplan-트랙-s와의-관계"></span>
<span id="2-비용-해부"></span>
<span id="21-한-번의-allupdateworldmatrix가-하는-일"></span>
<span id="22-spatial-순회의-노드당-고정비-x0가-분리-측정할-대상"></span>
<span id="23-이동-노드의-decompose"></span>
<span id="24-렌더-프록시-전량-무조건-갱신--x8-이전-기준선"></span>
<span id="25-순회-밖-갱신-경로-셋--설계-제약"></span>
<span id="26-transform-없는-entity도-스토어-슬롯을-갖는다"></span>
<span id="3-측정구조-전제-교정"></span>
<span id="31-40588µs는-두-도메인의-합이다--소스-확인"></span>
<span id="32-transform-dirty로-allupdateworldmatrix-전체를-반환하면-안-된다--소스-확인"></span>
<span id="33-순서-배열은-데이터-배열을-연속으로-만들지-않는다"></span>
<span id="34-전역-dirty-하나는-sparse-movement를-on에서-구하지-못한다"></span>
<span id="35-parent-before-child는-cycle-검증의-결과이지-원인이-아니다"></span>
<span id="36-moved-list에는-프레임-수명-계약이-필요하다--소스-확인"></span>
<span id="37-깊이-밴드와-static-partition은-조건부-최적화다"></span>
<span id="38-x1의-비용은-작다--실측-2026-08-25"></span>
<span id="39-아직-재지-않은-것--topology-변경-빈도--미측정"></span>
<span id="4-외부-엔진-비교-2026-08-25-소스-판독"></span>
<span id="41-vanishingground--참고가-아니라-반례"></span>
<span id="42-unity--배치가-핵심이지만-그것만으로는-부족하다"></span>
<span id="43-ue5--전수-순회-자체가-없다"></span>
<span id="44-lumina--이식-가능한-아이디어"></span>
<span id="5-목표와-비목표"></span>
<span id="51-목표"></span>
<span id="52-비목표"></span>
<span id="6-목표-소유-구조"></span>
<span id="61-저작-정본과-실행-투영의-분리"></span>
<span id="62-안정-identity와-실행-위치"></span>
<span id="63-transform-없는-중간-노드"></span>
<span id="64-ui-projection"></span>
<span id="7-쓰기-계약"></span>
<span id="71-모든-spatial-변경은-publishlocalwrite로-합류한다"></span>
<span id="72-dirtycount-대신-queue--epoch"></span>
<span id="73-즉시-월드-읽기"></span>
<span id="8-resolve-알고리즘"></span>
<span id="81-프레임-플로우--x0-착수-전-기준선"></span>
<span id="82-프레임-플로우--목표"></span>
<span id="83-syncderivedstate-내부"></span>
<span id="84-spatial-sparse-resolve"></span>
<span id="85-topology-compile"></span>
<span id="86-병렬화-우선순위"></span>
<span id="9-render-publication"></span>
<span id="91-moved-slot이-아니라-proxy-dirty-mask"></span>
<span id="92-단일-publish-지점"></span>
<span id="sec-0-판정-요약"></span>
<span id="sec-1-현재-구조"></span>
<span id="sec-10-데이터-표현"></span>
<span id="sec-11-계층-mutation-계약"></span>
<span id="sec-12-실행-슬라이스"></span>
<span id="sec-13-성능회귀-매트릭스"></span>
<span id="sec-14-위험과-롤백-경계"></span>
<span id="sec-15-이-계획이-하지-않는-것--기각"></span>
<span id="sec-16-함정"></span>
<span id="sec-17-scenegraphredesignplan-트랙-s와의-관계"></span>
<span id="sec-2-비용-해부"></span>
<span id="sec-3-측정구조-전제-교정"></span>
<span id="sec-4-외부-엔진-비교-2026-08-25-소스-판독"></span>
<span id="sec-5-목표와-비목표"></span>
<span id="sec-6-목표-소유-구조"></span>
<span id="sec-7-쓰기-계약"></span>
<span id="sec-8-resolve-알고리즘"></span>
<span id="sec-9-render-publication"></span>
<span id="sec-부록-a-폐기된-1차-실행-계획t0t5과-판정"></span>
<span id="x0--측정-기준선-교정-p0--2일"></span>
<span id="x0-완료-기록--2026-09-01"></span>
<span id="x1--쓰기-길목-단일화-p0--15일--완료-2026-09-01"></span>
<span id="x1-구현검증-기록"></span>
<span id="x2--uispatial-resolver-분리-p0--2일--완료-2026-09-01"></span>
<span id="x2-구현검증-기록"></span>
<span id="x3--canonical-hierarchy-mutation-p0--2일--완료-2026-09-01"></span>
<span id="x4--sparse-compiled-projections-p0--4일--완료-2026-09-01"></span>
<span id="x4-구현검증-기록"></span>
<span id="x5--dirty-root-sparse-resolver-p0--4일--완료-2026-09-01"></span>
<span id="x5-구현검증-기록"></span>
<span id="x6--즉시-pull-통합-p1--15일--완료-2026-09-01"></span>
<span id="x6-구현검증-기록"></span>
<span id="x7--animatorphysics-bulk-writer-p1--3일--구현로컬-검증-완료-2026-09-01"></span>
<span id="x7-tsan-적용성-점검--2026-09-01"></span>
<span id="x7-구현검증-기록"></span>
<span id="x8--render-proxy-dirty-mask-p1--25일--트랙-s의-s4-승계--완료"></span>
<span id="x8-완료-기록--2026-09-01"></span>
<span id="x9--선택-확장-p2--4일--x8-이후"></span>
<span id="권장-구조"></span>
<span id="부록-a-폐기된-1차-실행-계획t0t5과-판정"></span>

</details>
