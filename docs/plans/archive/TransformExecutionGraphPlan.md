# (통합됨) Sparse Compiled Transform Execution Graph 제안

> 2026-10-20 정리 · master `27de3f5a02aadde7456aab680db3184c36ee735e` · 종료/승계 문서

판정 기준은 위 master의 커밋된 소스다. 작업 중인 로컬 코드 변경은 포함하지 않는다. 완료된 구현 단계·날짜별 실행 일지는 제거했다. 대시보드는 SRP 저작 전환 취소에 해당하는 항목만 제거하고, 나머지 상태·공수·링크를 보존한다.

[전체 문서 대조·충돌 판정](../MasterPlanAudit20261020.html) · [정리 전 원문](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/docs/plans/archive/TransformExecutionGraphPlan.md)

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


</details>
