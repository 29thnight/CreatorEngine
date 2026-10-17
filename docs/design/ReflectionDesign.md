# reflgen 서술·등록·생성 계약

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 호출부. 이 문서는 현재 구현을 설명하며, 이번 문서 갱신에서 빌드·테스트·제품 실행을 수행하지 않았다.

## 현재 리플렉션의 정본

반영 대상 C++ 선언에 `[[reflgen::reflect]]`를 붙이고 생성기가 schema와 등록 함수를 만든다. 엔진 런타임 조회 창구는 `Meta::Types`, `Meta::Find`, `Meta::TypeOf`, `Meta::Require`다. 구 Meta::Type/Meta::Registry나 수동 meta::schema를 현재 저장소의 정본으로 새로 작성하지 않는다.

`Meta::Types()`의 registry 실체는 ReflgenRuntime.cpp에 하나만 정의된다. 헤더 inline registry나 `reflgen::default_registry()`를 별도로 사용하면 모듈별 registry가 갈라질 수 있다. 소비자 TU는 등록된 descriptor를 찾는다. 임의 TU에서 `type_descriptor_of<T>()`로 descriptor를 재생성하면 엔진 serializer 특수화 가시성 차이가 ODR 문제를 만들 수 있다.

## 선언과 속성

private 멤버 접근은 `friend struct reflgen::access`를 사용한다. 저장에서 제외할 runtime cache·포인터·세션 핸들은 `[[reflgen::ignore]]`로 표시한다. 숨겨진 저작 필드는 `hidden`과 저장 제외를 혼동하지 않는다.

엔진 속성 `creator::debug_only`, `wide`, `read_only_in_inspector`, `hide_in_inspector`는 Editor 그리기 정책이다. `creator::units`는 현재 선언 메타데이터를 받을 뿐 소비자가 구현되어 있다고 보장하지 않는다. 속성 이름이 존재한다는 사실만으로 UI 동작을 추정하지 않는다.

## 생성과 모듈 주입

Directory.Build.targets가 vcpkg 설치의 `share/reflgen/msbuild/reflgen.targets`를 가져온다. 현재 overlay port는 reflgen v1.0.0과 allocation patch를 고정하며 generator·libclang·headers·MSBuild targets를 함께 설치한다. IDE VSIX 버전과 엔진 generator dependency 버전은 별개다.

반영 헤더를 ClInclude로 가진 RenderEngine, SceneRuntime, Editor 모듈이 생성 주체다. native 소비자는 필요한 module injection을 받으며 Editor 전용 주입은 Editor 프로젝트에만 적용한다. Physics/diagnostics 등 제외 조건을 모든 프로젝트에 일괄 확대하지 않는다. ReflgenReference는 상대 생성물을 기다리는 연결이지 무조건 일반 프로젝트 링크 의존을 만드는 표가 아니다.

`ReflgenRegistrationHeaders`는 엔진 serializer 특수화를 등록 TU에 보이게 하고 `ReflgenAttributeHeaders`는 creator 속성 정의를 먼저 보이게 한다. vcpkg가 MSBuild 평가 이후 처음 설치/갱신한 경우 targets가 재빌드를 요구할 수 있다. 설치 완료와 실제 컴파일 성공을 분리해서 보고한다.

## 등록과 정체성

`RegisterReflectManual`은 생성된 register_RenderEngine/register_SceneRuntime을 엔진 registry에 호출하고, 닫힌 REFLECT_TYPE_LIST의 typed post-load operations를 등록한다. 이름과 type ID는 생성 schema와 엔진 TypeTrait 값이 같아야 한다. 등록 헤더의 static_assert가 qualified name과 FNV 기반 ID를 대조한다.

`VerifyReflectRegistration`은 부팅 시 각 descriptor의 읽기·쓰기 가능성을 검사한다. 실패 필드와 타입을 로그로 남기고 abort한다. 타입이 등록돼 보인다는 것만으로 모든 필드 serializer가 가시적이라는 보증은 없다. `Require`는 미등록 타입을 logic_error로 드러낸다.

컴포넌트의 영속 UUID는 type name hash와 별개다. 저장 리네임 호환성이나 미래 network wire ID를 runtime type ID 하나에 맡기지 않는다.

## 순회와 직렬화

`fields()`/`methods()`는 부모를 포함한다. `Meta::LocalFields`/`LocalMethods`는 inherited prefix를 제외한 자기 선언만 반환하고 `Parent`는 첫 반영 base를 반환한다. 다중 상속의 실제 객체 주소는 descriptor base chain과 MostDerived 보정 규약을 따라야 한다.

엔진 authoring serializer는 BeforeSerialize→record header→field→AfterSerialize 순서를 유지한다. component 봉투에는 이름/typeID와 type UUID, enum에는 underlying integer를 쓴다. FileGuid/HashingString은 문자열이며 math 값은 정해진 flow map/sequence다. pointer sequence의 entity/component 복원은 generic reflection reader가 아니라 SceneManager/ComponentFactory가 담당한다.

`OnBeforeDeserialize`, field 읽기/변경 통지, typed postLoad/`OnDeserialized`는 서로 다른 단계다. 단순 필드 대입만 하고 후처리를 생략하면 asset binding·dirty publication이 빠질 수 있다. editor 직렬화와 cooked runtime format은 같은 포맷이 아니다.

## 생성과 GC 소유권

`Meta::Create<Base>`는 unmanaged 객체용이며 gc::managed_type에는 static_assert로 막는다. 현재 Scene/Entity/Component는 GCCE domain 경계를 사용한다. `ComponentFactory::CreateManaged(domain,type)`는 닫힌 native 목록을 type ID로 대조하고 `Component::CreateManaged<T>`를 호출한다. descriptor가 임의 managed 객체를 new/delete하는 경로를 만들지 않는다.

SceneManager가 공유 GameThread domain과 Scene root를 보유하며, entity/component graph는 trace_ref로 추적된다. 리플렉션의 pointer-like serialization 지원은 tracing 소유권을 대신하지 않는다. 런타임 캐시와 assets의 own::shared_owner도 별도 수명 체계다.

## 새 타입을 추가하는 순서

1. 선언/속성 및 gc_trace 필요 여부를 정한다
2. 모듈 프로젝트의 생성 입력에 헤더가 포함되는지 확인한다
3. factory/serialization 대상이면 REFLECT_TYPE_LIST, lifecycle 등록, component UUID 필요성을 검토한다
4. field 타입의 serializer가 registration TU에 보이는지 확인한다
5. type name/ID, 생성 필드 목록, save/reload와 post-load, Inspector를 각각 검증한다

컴파일만으로 save/load나 UI를 수용하지 않는다. 이번 문서화는 생성기·빌드·부팅 검증을 실행하지 않았다. 과거 통합 원자료는 [ReflgenPhase425Integration](../analysis/ReflgenPhase425Integration.md)에 연결한다.

## 구현 근거

- [Engine/Utility_Framework/ReflgenRuntime.h](../../Engine/Utility_Framework/ReflgenRuntime.h)
- [Engine/Utility_Framework/ReflgenRuntime.cpp](../../Engine/Utility_Framework/ReflgenRuntime.cpp)
- [Engine/Utility_Framework/ReflgenAttributes.h](../../Engine/Utility_Framework/ReflgenAttributes.h)
- [Engine/Utility_Framework/ReflgenAuthoringSerializers.h](../../Engine/Utility_Framework/ReflgenAuthoringSerializers.h)
- [Engine/SceneRuntime/RegisterReflectManual.h](../../Engine/SceneRuntime/RegisterReflectManual.h)
- [Engine/SceneRuntime/ComponentFactory.cpp](../../Engine/SceneRuntime/ComponentFactory.cpp)
- [Engine/SceneRuntime/SceneGC.h](../../Engine/SceneRuntime/SceneGC.h)
- [Directory.Build.targets](../../Directory.Build.targets)
- [ports/reflgen/portfile.cmake](../../ports/reflgen/portfile.cmake)
