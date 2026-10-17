# 리플렉션 소비 경계와 변경 위험 분석

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 호출부. 이 문서는 현재 구현을 설명하며, 이번 문서 갱신에서 빌드·테스트·제품 실행을 수행하지 않았다.

## 현재 구조에서 비용이 생기는 곳

리플렉션은 런타임 이름/ID 조회와 저작 데이터 순회, Editor Inspector, component 생성·post-load를 연결한다. 핵심 정본은 [ReflectionDesign](../design/ReflectionDesign.md)이다. 옛 Reflection.hpp 계열의 이름이 남아 있어도 현재 descriptor는 reflgen이며 이전 std::any 접근 분석을 그대로 적용하지 않는다.

부팅은 RegisterReflectManual→generated registration→typed operations→VerifyReflectRegistration 순이다. 저장·로드는 엔진 authoring adapter가 record envelope와 필드를 처리한다. Inspector는 descriptor/attribute를 읽고, ComponentFactory는 native closed type list와 GC domain에서 객체를 만든다. 이 경계마다 최적화 목적이 다르다. 부팅 검증을 없애 hot path를 줄였다고 주장하거나, save/load 비용을 매 프레임 비용으로 합치면 안 된다.

## 안전상 중요한 세 분리

첫째, 서술과 인스턴스 소유권을 분리한다. descriptor는 type의 구조를 알지만 Scene/Entity/Component의 domain allocation을 소유하지 않는다. GC managed factory를 generic reflection allocator로 대체할 수 없다.

둘째, 등록 TU와 소비 TU를 분리한다. serializer가 보이는 등록 TU에서 descriptor를 만들고 소비자는 Meta::Find/TypeOf로 찾는다. 임의 위치의 descriptor 생성은 필드 지원 여부를 다르게 만들 수 있다. registration-time serializability 검사는 이 오류를 기동 시 드러낸다.

셋째, 정체성을 분리한다. runtime FNV type ID, 저장 component UUID, asset FileGuid, EntityHandle은 서로 다른 안정성·수명 계약이다. 하나를 다른 용도로 재사용하면 rename·reload·다중 Scene 경계에서 잘못된 객체를 찾을 수 있다.

## 현재 소스에서 남는 설계 선택

ComponentFactory의 Add Component 메뉴는 typed operations 등록 여부와 이름 패턴/예외를 함께 사용한다. descriptor 존재만으로 모든 반영 클래스를 사용자 부착 가능 타입으로 취급하지 않는다. Transform/RectTransform/Bone처럼 생성 경로가 소유하는 타입이 있다.

`Meta::Parent`가 첫 base를 반환하는 것과 fields가 모든 inherited field를 포함하는 것은 다른 계약이다. 다중 상속 소비자는 둘을 같은 선형 구조로 가정하지 않아야 한다. `CanSnapshotImmutable`은 반영된 구조와 컨테이너 원소의 복사 가능성을 검사하지만 임의 copy constructor body나 비반영 mutable 데이터의 의미까지 증명하지 않는다.

## 측정 계획

변경을 평가하려면 cold registration, scene save/load, Inspector field draw, component post-load를 별도 marker로 측정한다. 같은 소스·같은 데이터·같은 Editor 표시 조건에서 비교하고 생성 시간·native compile time도 런타임 시간과 분리한다. 이번 개정에서는 파일 수·속도 배수·쿼리 횟수의 과거 실측을 재사용하지 않았고 새 실행도 하지 않았다.

## 구현 근거

- [Engine/Utility_Framework/ReflgenRuntime.h](../../Engine/Utility_Framework/ReflgenRuntime.h)
- [Engine/Utility_Framework/ReflgenAuthoringSerializers.h](../../Engine/Utility_Framework/ReflgenAuthoringSerializers.h)
- [Engine/SceneRuntime/RegisterReflectManual.h](../../Engine/SceneRuntime/RegisterReflectManual.h)
- [Engine/SceneRuntime/ComponentFactory.cpp](../../Engine/SceneRuntime/ComponentFactory.cpp)
- [Editor/EngineGUIWindow/ReflectionImGuiHelper.h](../../Editor/EngineGUIWindow/ReflectionImGuiHelper.h)
