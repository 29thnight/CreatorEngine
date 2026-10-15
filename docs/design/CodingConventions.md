# 코드 경계와 명명 규칙

> 기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스 정적 조사. 이 문서의 재작성에서 빌드·실행·성능 측정은 하지 않았다.

## 적용 범위

엔진 객체와 범용 유틸리티는 서로 다른 독자를 가진다. Scene·Entity·Component 같은 제품 API는 기존 PascalCase와 `m_` 멤버 관례를 따른다. scheduler·ownership·container처럼 독립적인 값 타입은 실제 선언에 맞는 lower_snake_case와 STL 유사 인터페이스를 사용한다. 새 문서가 기존 API의 대소문자를 임의로 바꾸어서는 안 된다.

이 구분은 개발 지침이다. 저장소 전체가 자동 검사로 통일되었다는 뜻은 아니다. 예를 들어 HashingString은 현재 PascalCase 메서드를 갖고, Core.Runtime.h의 `std::null_exception`은 사용자 타입을 std에 추가하는 남은 정리 대상이다. 이를 이미 제거된 문제로 기록하지 않는다.

## 헤더·소유권·오류

공개 헤더에는 필요한 선언만 노출한다. Core.Minimal.h는 Reflection.hpp를 자동으로 포함하지 않으므로 reflection 사용처가 직접 의존성을 밝혀야 한다. Authoring::Document는 backend를 pimpl에 숨기고, ParsedDocument/ReadNode는 소유자와 빌린 view의 수명을 분리한다.

raw pointer는 소유권을 설명하지 않는다. Scene 객체는 GCCE의 root/trace/weak 관계, native 자원은 명시적 owning handle, display image는 GPU 완료까지 유지하는 lease로 구분한다. 서로 다른 수명을 shared_ptr 한 종류로 통일하지 않는다.

예상 가능한 입력 실패는 해당 API의 result/error 규약으로 반환한다. job_handle의 failure, AssetRequest의 terminal state, BuildTool의 BuildException과 종료 코드를 섞지 않는다. 로그에 성공이라고 쓰기 전에 호출자가 어떤 완료 경계까지 확인했는지 명시한다.

## 변경 검토

헤더 변경은 DLL ABI, managed bridge, 저장 형식에 미치는 영향을 각각 확인한다. C++23은 빌드 설정의 언어 수준이며 임의의 STL 객체를 DLL/C ABI로 전달할 권한은 아니다. 예제는 실제 시그니처와 ownership을 보여야 하고, 벤치마크·회귀 통과는 실행 증거가 있을 때만 별도로 기록한다.

## 구현 근거

- [Directory.Build.props](../../Directory.Build.props)
- [Engine/Utility_Framework/Core.Minimal.h](../../Engine/Utility_Framework/Core.Minimal.h)
- [Engine/Utility_Framework/Core.Runtime.h](../../Engine/Utility_Framework/Core.Runtime.h)
- [Engine/Utility_Framework/HashingString.h](../../Engine/Utility_Framework/HashingString.h)
- [Tools/runtime/HostAbi.h](../../Tools/runtime/HostAbi.h)
- [Engine/Utility_Framework/AuthoringDocument.h](../../Engine/Utility_Framework/AuthoringDocument.h)
