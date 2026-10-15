# Utility Framework의 현재 경계와 남은 정리

> 기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스 정적 조사. 이 문서의 재작성에서 빌드·실행·성능 측정은 하지 않았다.

## 이미 코드에 있는 구조

Core.Minimal은 정의·runtime·memory·coroutine·path·log·delegate의 얇은 묶음이다. reflection은 직접 include한다. 비동기 실행은 engine-owned job_scheduler와 job_group/job_handle로 표현하며, 문서 소유권은 Authoring::Document와 ParsedDocument로 분리한다. 이것이 현재 소비자가 의존할 실제 경계다.

HashingString은 빈 문자열을 정상 값으로 다루고 null C 문자열을 거부한다. 동등성은 hash뿐 아니라 원문도 비교한다. 해당 hash를 영속 asset identity나 파일 형식의 안정적인 키로 취급해서는 안 된다.

## 완료로 표시할 수 없는 항목

Core.Runtime.h는 여전히 std namespace에 null_exception을 선언한다. 기존 이름을 호환 가능한 engine namespace로 옮기는 작업은 미구현 의도다. Core.Memory의 AllocateAndCopy는 전달받은 포인터의 지역 사본에 할당하므로, 호출자에게 새 포인터를 돌려주는 API로 문서화할 수 없다. API 교체는 호출처 조사와 실패·소유권 규약을 먼저 요구한다.

PPL 전체 제거도 완료 상태가 아니다. proxy command와 일부 Editor import queue의 동시성 컨테이너는 별도 소비 계약을 가진다. scheduler 통합과 container 교체를 같은 작업으로 세지 않는다.

## 다음 변경의 수용 기준

유틸리티는 hidden singleton·암묵적인 thread·예외 소유권을 늘리지 않고 명시적 owner와 실패를 드러내야 한다. 이름 정리만으로 ABI나 의미를 바꾸지 않는다. 헤더 include 절감, allocation/latency 개선은 독립적인 측정 항목이다. 현재 재작성은 이러한 계약을 읽은 것으로 성능 개선이나 전체 현대화 완료를 인증하지 않는다.

관련 정본: [scheduler](../design/JobSchedulerDesign.md), [container](../design/ContainerLibraryDesign.md), [문서 직렬화](SerializationPlan.md).

## 구현 근거

- [Engine/Utility_Framework/Core.Minimal.h](../../Engine/Utility_Framework/Core.Minimal.h)
- [Engine/Utility_Framework/Core.Runtime.h](../../Engine/Utility_Framework/Core.Runtime.h)
- [Engine/Utility_Framework/Core.Memory.hpp](../../Engine/Utility_Framework/Core.Memory.hpp)
- [Engine/Utility_Framework/HashingString.h](../../Engine/Utility_Framework/HashingString.h)
- [Engine/Utility_Framework/ThreadPool.h](../../Engine/Utility_Framework/ThreadPool.h)
