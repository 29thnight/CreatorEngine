# 네이티브 객체 GC 및 에셋 소유권 정책 (PHASE 25)

> 2026-10-20 정리 · master `27de3f5a02aadde7456aab680db3184c36ee735e` · 잔여 구현·수용 계획

판정 기준은 위 master의 커밋된 소스다. 작업 중인 로컬 코드 변경은 포함하지 않는다. 완료된 구현 단계·날짜별 실행 일지는 제거했다. 대시보드는 SRP 저작 전환 취소에 해당하는 항목만 제거하고, 나머지 상태·공수·링크를 보존한다.

[전체 문서 대조·충돌 판정](MasterPlanAudit20261020.html) · [정리 전 원문](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/docs/plans/NativeObjectMemoryPolicyPlan.md)

## 남은 작업과 완료 판정

- GCCE 기반 Scene/Entity/Component·domain/root/trace 참조와 논리적 제거 구현은 완료된 구조로 제외한다. SGCL 채택·unique_ptr 일괄 치환이라는 옛 방안은 폐기한다.
- 실제 Editor/Player의 DDOL·Prefab·Undo·save/load·reload·destroy/collection 순서와 CLR 병행 수집을 현재 master에서 수용한다. native handle 무효화와 root 해제를 분리한다.
- 대표 workload에서 p95/p99·최악 지연·회수 지연·live/committed/reserve·dense/sparse survivor를 측정하고 root 진단/운영 예산을 확정한다. 에셋 owner 전환은 AssetDepot 정본을 소비한다.

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

<span id="1-확정한-대상과-책임"></span>
<span id="2-현재-구현-기준선"></span>
<span id="3-네이티브-객체-gc와-논리적-생명주기"></span>
<span id="31-생존-참조와-root"></span>
<span id="32-제거와-메모리-회수를-분리"></span>
<span id="4-c-gc-병행-및-계층-경계"></span>
<span id="5-에셋-스마트-포인터와-ownership_cpp-적용-정책"></span>
<span id="6-단계와-수용-게이트"></span>
<span id="7-이행후퇴와-기존-계획-연결"></span>
<span id="8-기존-실험-증거와-한계"></span>
<span id="sec-1-확정한-대상과-책임"></span>
<span id="sec-2-현재-구현-기준선"></span>
<span id="sec-3-네이티브-객체-gc와-논리적-생명주기"></span>
<span id="sec-4-c-gc-병행-및-계층-경계"></span>
<span id="sec-5-에셋-스마트-포인터와-ownership_cpp-적용-정책"></span>
<span id="sec-6-단계와-수용-게이트"></span>
<span id="sec-7-이행후퇴와-기존-계획-연결"></span>
<span id="sec-8-기존-실험-증거와-한계"></span>
<span id="단편화지연회수-측정"></span>

</details>
