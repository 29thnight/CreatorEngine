# 엔진 리플렉션을 유지하는 범위

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 호출부. 이 문서는 현재 구현을 설명하며, 이번 문서 갱신에서 빌드·테스트·제품 실행을 수행하지 않았다.

## 결정

생성된 타입 서술과 엔진 등록 창구는 유지한다. 목적은 Inspector, authoring serialization, 타입 식별과 안전한 생성/후처리다. 모든 gameplay 연산을 동적 호출로 바꾸거나 type descriptor만으로 RPC·네트워크 복제를 자동 허용하는 결정은 아니다.

현재 구현은 [reflgen 계약](ReflectionDesign.md)에 있다. 옛 헤더툴 유지 여부나 std::any 표면을 지금의 선택으로 읽지 않는다. canonical schema는 reflgen 생성물이고 Meta namespace는 엔진 어댑터 창구로 남아 있다.

## 유지 근거

저장 파일의 타입/필드 이름을 런타임에 해석해야 하고 Editor는 구체 C++ 타입을 모르는 상태에서도 필드와 속성을 표시해야 한다. 컴파일타임 서술은 생성·형식 검사를 돕지만 이름→descriptor 등록소와 post-load 연결을 없애지 않는다. component의 영속 UUID와 엔진 전용 envelope도 일반 serializer 밖의 정책이다.

반대로 객체 메모리 수명은 GCCE domain, asset generation은 own::shared_owner 등 각 소유 모델이 담당한다. descriptor의 일반 factory가 managed 객체를 직접 할당하지 않게 한 제한은 유지해야 한다.

## 변경 시 지킬 기준

- 새 reflected type은 generator 입력, registry, typed post-load와 실제 소비 경계를 함께 확인한다
- 숨김 속성과 serialization 제외를 구분하고 runtime cache는 저장하지 않는다
- registration TU에서 serializer를 한 번 확정하고 소비 TU의 독자 descriptor 생성을 피한다
- 이름 기반 메뉴 규칙을 개선하더라도 자동 spatial/bone 부착의 소유 경계를 보존한다
- UI 비용 개선은 Editor 층에서, codec 변경은 authoring adapter에서, 수명 변경은 domain/factory에서 각각 판단한다

과거 R1/R2 같은 작업명은 고정된 미완료 목록이 아니다. 현재 ComponentFactory에는 typed 후처리와 managed closed-list allocation이 있고 과거의 타입별 asset-load 분기를 그대로 다시 도입할 이유가 없다. 새로운 자동화는 source gate와 save/reload·Editor 회귀로 별도 검증해야 한다.

## 구현 근거

- [Engine/Utility_Framework/ReflgenRuntime.h](../../Engine/Utility_Framework/ReflgenRuntime.h)
- [Engine/Utility_Framework/ReflgenAuthoringSerializers.h](../../Engine/Utility_Framework/ReflgenAuthoringSerializers.h)
- [Engine/SceneRuntime/ComponentFactory.cpp](../../Engine/SceneRuntime/ComponentFactory.cpp)
- [Engine/SceneRuntime/RegisterReflectManual.h](../../Engine/SceneRuntime/RegisterReflectManual.h)
