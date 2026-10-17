# 컨테이너 선택: STL 우선과 자체 dynamic_array 보류

> 기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스 정적 조사. 이 문서의 재작성에서 빌드·실행·성능 측정은 하지 않았다.

## 현재 결론

이 문서는 `ce::dynamic_array`를 새로 구현하라는 계획이 아니다. 기존 결정대로 해당 타입 도입은 보류/기각 상태를 유지한다. 현재 resource·scheduler·scene 코드는 std::vector, map, unordered 계열과 도메인 소유권 handle을 사용한다. 별도 container 이름을 도입하면 reflection/serialization과 ABI의 비용까지 생긴다.

과거 allocator microbenchmark와 frame 가중 이득은 당시 workload에서 나온 역사적 판단 자료다. 현재 engine의 GC·AssetDepot·공용 DLL 구조가 바뀌었으므로 그 숫자를 새 성능 사실로 반복하지 않는다. 재개하려면 현재 allocation 분포와 실제 소비자를 다시 측정해야 한다.

## 필요한 기능을 타입과 분리

swap-and-pop 제거, reserve, contiguous iteration 같은 요구는 표준 container와 작은 알고리즘으로 충족할 수 있다. 순서 보존이 필요한 목록에 swap 제거를 사용하지 않고, iterator/reference 무효화와 entity/asset handle 안정성을 별도로 판단한다. GC trace_ref나 shared owner를 vector에 넣었다고 vector의 원소 주소가 안정되는 것은 아니다.

새 container가 정말 필요하면 첫 소비자, 측정된 병목, exception/move/destruction 계약, allocator 경계, reflection trait 및 wire format 회귀를 먼저 정의한다. 운영 성능 이득 없이 크기나 명명 통일만을 위해 전면 교체하지 않는다.

## 남겨 둔 의도

small buffer, fixed capacity, flat hash 등은 각 소비자 workload가 요구할 때 독립 검토한다. 전역 allocator override나 모든 STL의 alias replacement는 이 문서의 승인된 구현 방향이 아니다. [UtilityFrameworkModernizationPlan](../plans/UtilityFrameworkModernizationPlan.md)은 사용성·의존·오류 의미를 기준으로 정리를 진행한다.

## 구현 근거

- [Engine/Utility_Framework/JobScheduler.h](../../Engine/Utility_Framework/JobScheduler.h)
- [Engine/RenderEngine/AssetDepot/ModelAssetRuntime.h](../../Engine/RenderEngine/AssetDepot/ModelAssetRuntime.h)
- [Engine/SceneRuntime/Scene.h](../../Engine/SceneRuntime/Scene.h)
- [Engine/Utility_Framework/ReflectionContainer.h](../../Engine/Utility_Framework/ReflectionContainer.h)
