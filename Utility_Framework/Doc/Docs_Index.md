## Utility_Framework 문서 목차 표

| **파일**                    | **설명**                                                                                                   |
|-----------------------------|-----------------------------------------------------------------------------------------------------------|
| [**ClassProperty.h**](ClassProperty.md)  | 클래스 속성을 관리하기 위한 헬퍼 클래스들입니다.                                                          |
| [**Banchmark.hpp**](Banchmark.md)        | 특정 코드 블록의 실행 시간을 측정하고 콘솔에 출력하는 헬퍼 클래스입니다.                                   |
| [**Core.Assert.hpp**](Core.Assert.md)    | 조건이 실패하면 사용자 정의 오류 처리 함수(AssertionFailureFunction)를 호출하여 오류 메시지 출력 및 디버거 중단을 제공하는 매크로입니다. |
| [**Core.Console.hpp**](Core.Console.md)  | C#의 `Console` 클래스처럼 다양한 형식의 문자열을 파일이나 표준 출력으로 출력할 수 있는 `Write` 및 `WriteLine` 메서드를 제공합니다. |
| [**Core.Mathf.h**](Core.Mathf.md)        | DirectXMath 라이브러리와 DirectX SimpleMath 라이브러리를 편리하게 관리하기 위한 헬퍼 클래스들입니다.          |
| [**Core.Memory.hpp**](Core.Memory.md)    | 안전한 메모리 관리를 위한 헬퍼 함수와 `DeferredDeleter` 클래스를 포함하고 있습니다.                         |
| [**Core.Random.h**](Core.Random.md)      | 정수 및 실수 타입의 난수를 범위 내에서 생성하기 위한 템플릿 `Random` 클래스입니다.                          |
| [**Core.Thread.h**](Core.Thread.md)      | 작업을 큐에 추가하고 병렬로 실행하는 싱글톤 스레드 풀(`ThreadPool`)과 작업을 체인 형태로 연결할 수 있는 `Task` 클래스를 제공합니다. |
| [**CoreWindow.h**](CoreWindow.md)        | 윈도우 애플리케이션을 생성하고 메시지 핸들러를 등록하여 메시지를 처리하며, 메시지 루프를 실행하는 `CoreWindow` 클래스를 제공합니다. |
| [**DeviceResources.h**](DeviceResources.md)        | DirectX11의 `DeviceResources` 클래스는 **DirectX 11 및 2D 장치를 관리하며, 장치 리소스 생성, 윈도우 크기 변경 처리, DPI 설정, 렌더링 대상 관리, 장치 복구, 및 표시 작업**을 담당하는 클래스입니다. |
| [**DireectXHelper.h**](DirectXHelper.md)        | DirectX11 헬퍼 유틸리티는 **COM 예외 처리, 비동기 파일 읽기, DPI 변환, Direct3D 리소스 관리, 디버그 SDK 확인, 그리고 TGA 파일로부터 텍스처를 생성**하는 등의 기능을 제공하는 도구 집합입니다. |
| [**LinkedListLip.hpp**](LinkedListLib.md)        | **사용자 정의 링크드 리스트 구현**으로, 객체가 `LinkProperty`를 상속받아 리스트에 연결되며, **순회, 링크, 언링크** 및 **전체 삭제**와 같은 작업을 지원합니다. |
| [**TimeSystem.h**](TimeSystem.md)        | `TimeSystem` 클래스는 **고정 및 가변 타임스텝**을 지원하며 **프레임 시간**과 **초당 프레임 수**(FPS)를 추적하는 DirectX 11용 타이밍 시스템 클래스. |
