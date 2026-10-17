# 저작 문서와 cooked 문서의 직렬화 경계

> 기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스 정적 조사. 이 문서의 재작성에서 빌드·실행·성능 측정은 하지 않았다.

## 두 소유자와 빌린 노드

Authoring::Document는 prefab 정의·scene backup 등 장기간 보관할 저작 문서의 move-only 소유자다. pimpl로 backend를 공개 헤더에서 숨긴다. ParsedDocument는 한 번 읽은 ryml tree의 scoped move-only 소유자다. Root()로 얻는 ReadNode는 tree를 소유하지 않으며 원본 문서보다 오래 살 수 없다. 노드만 비동기 lambda나 캐시에 보관해서는 안 된다.

없는 map key의 invalid node와 명시적 null은 서로 다른 입력이다. scalar 변환과 equality도 Authoring 계층의 계약을 사용한다. yaml-cpp의 값 복사 의미를 ryml view에 그대로 적용하는 코드는 안전한 전환으로 볼 수 없다.

## 텍스트 입력과 runtime 입력

ParseFile, native filesystem path를 받는 ParseFilePath, ParseText는 예상 가능한 읽기·문법 실패를 invalid document와 error로 보고한다. 호출자가 파일 없음과 손상 파일의 제품 정책을 결정한다. ParseCooked는 CEDO만 받는 strict runtime 경계다. 구형 YAML을 자동 재해석하는 fallback은 없으며 producer 형식 변경 시 다시 cook해야 한다.

CEDO magic/version 1과 embedded `CEDO1:` base64는 엔진의 typed document 표현이다. raw C++ struct의 padding을 그대로 저장하는 방식이 아니다. embedded payload의 base64도 엄격하게 검사한다. reflection 기반 값 변환은 ReflectionYml/typed serializer 계층이 맡으며 object ownership을 serializer가 대신하지 않는다.

## 패키징 데이터 흐름

저작 scene·prefab·설정 → BuildTool 문서 cook → typed cooked entry → PAK/manifest closure → Player의 ParseCooked 순서로 이어진다. CEMF catalog는 논리 identity와 artifact 연결, CEDO는 문서 tree, PAK는 배포 container다. 이름이 비슷한 별개 version을 하나의 “직렬화 버전”으로 합치지 않는다.

Player verification은 cooked scene 소비와 text parse count 0을 gate로 검사하는 코드가 있다. 이 문서는 그 검증의 존재를 설명하며 현재 실행 통과를 주장하지 않는다. shader source 등 bootstrap에 의도적으로 포함하는 텍스트까지 모두 금지하는 뜻도 아니다.

## 유지할 요구

schema 진화, unknown field, 숫자 범위, duplicate key 및 손상 payload의 처리 정책은 각 typed reader의 검증을 따라야 한다. 전역적인 자동 migration이나 임의 형식 호환을 약속하지 않는다. 새 필드는 authoring 저장, cook, Player 읽기와 실패 진단까지 한 묶음으로 바꾸어야 한다.

## 구현 근거

- [Engine/Utility_Framework/AuthoringDocument.h](../../Engine/Utility_Framework/AuthoringDocument.h)
- [Engine/Utility_Framework/AuthoringParsedDocument.h](../../Engine/Utility_Framework/AuthoringParsedDocument.h)
- [Engine/Utility_Framework/AuthoringReadNode.h](../../Engine/Utility_Framework/AuthoringReadNode.h)
- [Engine/Utility_Framework/AuthoringCookedDocument.h](../../Engine/Utility_Framework/AuthoringCookedDocument.h)
- [Engine/Utility_Framework/AuthoringCookedDocument.cpp](../../Engine/Utility_Framework/AuthoringCookedDocument.cpp)
- [Engine/Utility_Framework/ReflectionYml.cpp](../../Engine/Utility_Framework/ReflectionYml.cpp)
- [BuildTool/AssetCooking.cs](../../BuildTool/AssetCooking.cs)
- [BuildTool/PlayerVerification.cs](../../BuildTool/PlayerVerification.cs)
