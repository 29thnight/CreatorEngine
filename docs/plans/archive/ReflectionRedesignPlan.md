# 리플렉션 재설계 이전 안내

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 호출부. 이 문서는 현재 구현을 설명하며, 이번 문서 갱신에서 빌드·테스트·제품 실행을 수행하지 않았다.

## 현재 정본

옛 CT 단계별 계획은 [ReflectionDesign](../../design/ReflectionDesign.md)으로 대체한다. 현재 코드는 수동 T::reflection/meta::schema를 새 정본으로 만드는 중간 단계가 아니라 reflgen annotation에서 생성한 descriptor를 사용한다. 옛 MetaGenerator/FactoryRegistry를 현재 필수 도구로 안내하지 않는다.

## 이전 개념의 대응

반영 선언은 `[[reflgen::reflect]]`, field 제외는 `[[reflgen::ignore]]`, 엔진 UI 속성은 creator namespace로 표현한다. 이름/type ID 조회는 Meta::Find/TypeOf, 등록 실체는 ReflgenRuntime.cpp의 registry다. generated register 함수가 이를 채우며 typed operations에는 post-load 정책이 남는다.

부모 포함 field 순회와 자기 field 순회는 `fields()`와 `LocalFields`로 구별된다. unknown type은 Require의 오류로 드러내며 부팅 검증은 serialization 누락을 탐지한다. 이전 “동적 API 제거”를 runtime 이름 조회까지 없앤 것으로 읽으면 안 된다.

## 지금도 중요한 이전 요구

직렬화 봉투와 component UUID, hook 순서, 컨테이너 읽기 규칙, 실제 most-derived 주소 보정은 유지할 호환 계약이다. 새 구현으로 바꿀 때 field 수 비교만 하지 말고 실제 save/reload를 확인해야 한다. runtime cache는 저장 대상이 아니며 managed 객체는 explicit domain factory로 생성한다.

## 완료 의미

코드에는 현재 구조가 있지만 이 문서의 과거 완료 체크는 새로운 build 결과가 아니다. 생성기·엔진·Editor 간 배선은 [통합 증거 안내](../../analysis/ReflgenPhase425Integration.md), 성능·구조 위험은 [현재 분석](../../analysis/ReflectionSystemAnalysis.md)에서 확인한다. 향후 language reflection/새 generator로의 교체는 별도 설계·검증을 필요로 하며 지금 지원되는 기능으로 소개하지 않는다.

## 구현 근거

- [Engine/Utility_Framework/ReflgenRuntime.h](../../../Engine/Utility_Framework/ReflgenRuntime.h)
- [Engine/Utility_Framework/ReflgenAuthoringSerializers.h](../../../Engine/Utility_Framework/ReflgenAuthoringSerializers.h)
- [Engine/SceneRuntime/RegisterReflectManual.h](../../../Engine/SceneRuntime/RegisterReflectManual.h)
- [Engine/SceneRuntime/ComponentFactory.cpp](../../../Engine/SceneRuntime/ComponentFactory.cpp)
- [Directory.Build.targets](../../../Directory.Build.targets)
