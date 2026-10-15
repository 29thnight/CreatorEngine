# GCCE Scene graph 소유권과 정리 경계

> 기준: `493536e300e274cbf59989cdd62b20310c8ae64f`의 코드·호출 연결 정적 검토. 이 문서 갱신에서는 빌드·GPU 실행·성능 측정을 수행하지 않았다.

## 적용 범위와 생성

GCCE는 Scene → Entity → Component의 GC graph를 관리한다. 공통 Object 전체, asset/Texture owners, renderer snapshot, native SDK/GPU 자원의 수명을 대체하지 않는다. engine-facing include는 `SceneGC.h`이며 자산은 `Ownership.h` 계약을 계속 사용한다.

SceneManager는 owner thread의 `gc::domain`과 Scene root registry를 가진다. domain은 roots보다 먼저 선언되어 마지막에 파괴된다. Scene/Entity factory는 domain에서 root를 만들고 manager/Scene이 이를 채택한다. Entity의 Component vector와 missing-Transform fallback은 traced strong edge다.

`Component::CreateManaged<T>`는 각 concrete type이 자기 `gc_trace`를 선언하도록 compile-time 검사하고 생성 즉시 cleanup obligation을 시작한다. 단순 inherited empty tracer로 새 subtype을 추가할 수 없다. ordinary new/stack/unique_ptr에 managed object를 넣는 방식은 사용하지 않는다.

## tracing과 reflection

각 tracer는 base hook과 strong GC field를 방문한다. serialization에서 ignore된 field도 trace 대상이면 포함해야 한다. reflection metadata가 이를 자동 추론하지 않는다. Component owner는 weak identity이고 `GetOwner`/Transform cache는 nonowning borrow다.

SceneStore generation/slot과 기존 entity/component handle은 논리 identity의 기준이다. GC weak_ref를 CLR/public handle로 내보내 domain 이후까지 남기지 않는다. reflection은 저장 구조를 유지하되 graph 복구의 allocation/adoption은 Scene/ComponentFactory의 명시 domain 경계에서 수행한다.

## thread와 borrow

graph mutation/collection은 SceneManager owner thread에서 수행한다. presentation 편집은 owned value와 generation/context epoch를 담은 명령으로 전달하고 실행 시 대상을 다시 resolve한다. animation/transform worker는 graph edge detach/collection 전에 join해야 한다. immutable AI/render snapshot은 별도 수명 계약을 유지한다.

raw owner borrow는 pin 또는 root를 대신하지 않는다. 비동기 borrow가 edge보다 오래 살면 실제 대상의 pin을 owner thread에서 유지·해제해야 한다. memory pin은 Destroy 상태나 data race를 해결하지 않는다. scene borrow fence를 잡고 PresentationThread 응답을 동기 대기하는 새 경로를 만들지 않는다.

## 결정적 cleanup과 GC

Destroy는 기존 lifecycle hook을 유지한다. `FinalizeManagedDestroy`는 idempotent native release 후 owner observations를 지우고 GC lifecycle을 destroyed로 전이한다. collector destructor가 정상 engine lifecycle callback을 실행하도록 위임하지 않는다. cleanup 실패는 root를 유지하고 retired 성공으로 표시하지 않는다.

Scene retirement guard는 callback 이전에 켜며 reentry/re-adoption을 거부한다. slot release는 generation handle을 무효화한다. DDOL의 이동 root와 lifecycle은 완전 파괴와 구분한다.

정상 frame boundary는 `collect_step(250μs,16 units)`를 사용한다. allocation trigger는 1 MiB, maximum cycle interval은 1초다. soft budget이므로 큰 tracer/destructor/allocator 작업이 넘을 수 있다. full collection을 매 frame 돌리거나 latency 향상을 보장하는 설계가 아니다.

종료는 admission 중단, loads/jobs/PT/RT drain, lifecycle/native cleanup, graph root 해제, full collection, callback root 정리와 추가 collection, CLR/audio 등 서비스 종료 순서를 지켜 domain이 마지막에 사라지게 한다. module object 수만 0이라고 worker/callback code를 unload할 수 없다.

## 빌드·배포·제약

`Engine/GCCE/GCCE.vcxproj`는 vendored domain.cpp/block_allocator.cpp를 C++23 shared CRT DLL로 한 번 빌드한다. 구현만 GCCE_BUILDING, consumer는 GCCE_SHARED와 구성에 맞는 GC_DEBUG_CHECKS를 쓴다. 별도 static copy는 process-wide registry/thread state를 분리하므로 금지한다.

고정 upstream commit은 PROVENANCE에 기록되어 있다. 해당 pin에 upstream LICENSE가 없다는 제약을 그대로 보존한다. 허가 범위를 넘어 배포하기 전 적용 조건 확인이 필요하며 문서가 새 라이선스를 부여하지 않는다. 기존 runtime manifest/publisher가 같은 DLL/import closure를 배포한다.

이번 정리는 static ownership/call 경계 확인이다. [검증 안내](../analysis/GCCEValidation20261009.md)의 과거 결과와 현재 stress/latency/배포 검증을 구분한다.

## 구현 근거

- [`Engine/SceneRuntime/SceneGC.h`](../../Engine/SceneRuntime/SceneGC.h)
- [`Engine/SceneRuntime/SceneManager.h`](../../Engine/SceneRuntime/SceneManager.h)
- [`Engine/SceneRuntime/SceneManager.cpp`](../../Engine/SceneRuntime/SceneManager.cpp)
- [`Engine/SceneRuntime/Scene.h`](../../Engine/SceneRuntime/Scene.h)
- [`Engine/SceneRuntime/Entity.h`](../../Engine/SceneRuntime/Entity.h)
- [`Engine/SceneRuntime/Component.h`](../../Engine/SceneRuntime/Component.h)
- [`Engine/SceneRuntime/Component.cpp`](../../Engine/SceneRuntime/Component.cpp)
- [`Engine/GCCE/GCCE.vcxproj`](../../Engine/GCCE/GCCE.vcxproj)
- [`ThirdParty/GCCE/PROVENANCE.md`](../../ThirdParty/GCCE/PROVENANCE.md)
