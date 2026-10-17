# 현재 GCCE 객체 수명과 자원 소유권 정책

> 기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스 정적 조사. 이 문서의 재작성에서 빌드·실행·성능 측정은 하지 않았다.

## 현재 구현: 미래 GC 도입 문서에서 전환

현재 Scene, Entity, Component는 `gc::managed`를 사용한다. SceneManager는 하나의 GameThread `gc::domain`과 `gc::root_ref<Scene>` 목록을 소유한다. Scene의 entity 목록과 Entity의 component 목록은 `gc::trace_ref`이며, Component의 owner는 `gc::weak_ref<Entity>`다. 이전 문서의 unique_ptr 기준선 및 “네이티브 GC 미착수” 판정은 이 revision에 맞지 않는다.

구현체는 저장소의 GCCE이며, 과거 SGCL 실험을 제품 채택 증거로 인용하지 않는다. `Engine/GCCE/GCCE.vcxproj`와 공용 DLL 설정이 collector state의 모듈 경계를 형성한다.

## root와 논리적 제거

`SceneManager::AdoptScene`은 외부 root를 인수하고 borrowed Scene 포인터를 제공한다. `RetireScene`은 owner-thread scene-structure 경계에서 cleanup을 먼저 수행하고 root를 해제한다. `CollectManagedAtFrameBoundary`는 host가 render/scene-structure 작업을 join한 경계에서만 실행하도록 선언되어 있다.

DDOL와 scene 사이 이송은 `DetachedEntityTransfer`의 root_ref가 생존을 보장한다. 모든 scene은 같은 manager domain을 사용하므로 이송을 domain 간 객체 이동으로 처리하지 않는다. GC에 살아 있는 객체라도 destroy-mark/등록 해제 뒤 gameplay에서 유효한 객체라고 볼 수 없다. 메모리 생존과 논리적 수명을 분리해야 한다.

## 다른 소유권 체계와의 경계

C#은 CoreCLR GC를 유지한다. script handle generation 및 네이티브 lifecycle 정리는 native 객체 GC와 별개다. wrapper의 finalizer가 Scene의 Destroy 정본이 되지 않도록 유지해야 한다.

AssetDepot은 `own::shared_owner`, `weak_owner`, unique owner로 immutable generation/work/cache 수명을 표현한다. 공용 `Ownership.h`는 C++ 엔진 내부 경계에서만 사용하도록 명시하며 managed/C ABI에 own handle을 노출하지 않는다. GPU/COM/SDK 자원은 해당 API의 explicit release와 실제 사용 완료 retirement를 계속 따른다. native GC 도입이 GPU fence나 디스크 backing lease를 대체하지 않는다.

## 남겨 둔 수용 요구

root 누락·장기 보존, 교차 CLR/native strong cycle, DDOL transfer 실패, async candidate 취소, Play/Stop, script reload 및 종료 순서를 검증해야 한다. collector 단일성, DLL/TLS 종료와 worker 접근은 소스 구조 확인을 넘어 실제 Debug/Release Windows 실행으로 판정한다.

메모리는 requested/live/retained/commit/working-set/CLR/GPU를 분리하고, 회수 지연·frame p95/p99·dense/sparse survivor를 같은 workload에서 측정한다. 현재 수치를 측정하지 않았으므로 “STW 없음”, 성능 배수, 단편화 개선 또는 전체 수용 완료를 약속하지 않는다. 기존 미래 계획의 M0~M5/A0~A1은 현재 타입 교체를 다시 요구하는 단계가 아니라 이 root·lifecycle·성능 검증 항목으로 읽는다.

## 구현 근거

- [Engine/SceneRuntime/Scene.h](../../Engine/SceneRuntime/Scene.h)
- [Engine/SceneRuntime/Entity.h](../../Engine/SceneRuntime/Entity.h)
- [Engine/SceneRuntime/Component.h](../../Engine/SceneRuntime/Component.h)
- [Engine/SceneRuntime/SceneManager.h](../../Engine/SceneRuntime/SceneManager.h)
- [Engine/SceneRuntime/SceneManager.cpp](../../Engine/SceneRuntime/SceneManager.cpp)
- [Engine/SceneRuntime/DetachedEntityTransfer.h](../../Engine/SceneRuntime/DetachedEntityTransfer.h)
- [Engine/GCCE/GCCE.vcxproj](../../Engine/GCCE/GCCE.vcxproj)
- [ThirdParty/GCCE/include/gc/gc.hpp](../../ThirdParty/GCCE/include/gc/gc.hpp)
- [Engine/Utility_Framework/Ownership.h](../../Engine/Utility_Framework/Ownership.h)
