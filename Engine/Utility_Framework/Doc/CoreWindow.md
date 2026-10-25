# CoreWindow와 AppBackendWinAPI

`CoreWindow`는 `Creator.Core.Window` 모듈의 `Core::Window` 별칭이다.
`IWindowBackend`를 소유하며 창 표시·크기 조회·이벤트 폴링과 `Then()` 루프를 제공한다.
Windows SDK 타입은 이 계약에 포함하지 않는다.

`AppBackendWinAPI`는 Windows 전용 `IWindowBackend` 구현이다.
파일명과 클래스명에 백엔드를 표시하며, 다른 OS 구현은 같은 계약을 구현하는 별도 백엔드로 추가한다.
현재 호스트는 Windows 메시지 핸들러를 등록하므로 이 구현을 직접 참조한다.

## 정의와 소비

- `Engine/Contracts/Creator.Core.Window.ixx`: 창 계약·DPI 계산·템플릿을 직접 정의한다.
- `Engine/Contracts/Creator.Platform.Windows.ixx`: Windows 창 설정·핸들 변환·apartment·AppWindow 호스트를 직접 정의한다.
- `Engine/Contracts/Creator.Platform.Windows.AppWindow.cpp`: Windows App SDK와 C++/WinRT 연동을 구현한다.
- `Engine/Utility_Framework/Creator.AppBackend.WinAPI.ixx`: 공개 API와 불투명한 상태 포인터만 정의한다.
- `Engine/Utility_Framework/AppBackendWinAPI.cpp`: HWND·콜백·AppWindow·창 수명을 소유하는 비공개 상태를 구현한다.
- `Engine/Utility_Framework/AppBackendWinAPI.Crash.cpp`: 로그·덤프 기록자와 연결하는 백엔드 내부 구현이다.

일반 빌드에서는 엔진 API를 직접 import한다. `CoreWindow.h`, `WindowDesc.h`,
`WindowsWindow.h`, `WindowsApartment.h` 어댑터는 사용하지 않는다.

```cpp
import std;
import Creator.Core.Window;
import Creator.Platform.Windows;

import "Creator.WinAPI.h";
import Creator.AppBackend.WinAPI;

void RunWindow(HINSTANCE instance)
{
    WindowsWindowDesc desc;
    desc.title = L"Creator";
    auto backend = std::make_unique<AppBackendWinAPI>(instance, desc);
    backend->RegisterHandler(WM_CLOSE, [](HWND, WPARAM, LPARAM) -> LRESULT
    {
        PostQuitMessage(0);
        return 0;
    });
    CoreWindow window(std::move(backend));
    window.Show();
    window.Then([]
    {
        // 메시지가 없을 때 프레임 작업을 수행한다.
    });
}
```

## 도구용 헤더

reflgen의 Clang 파서와 독립 회귀 검사는 `CE_CONTRACT_STANDALONE` 모드를 사용한다.
이 모드는 `Tools/build/ModuleAdapters/`의 생성 헤더를 읽는다.
모듈 인터페이스가 정의의 원본이며, 생성 헤더를 직접 수정하지 않는다.

```powershell
py Tools/build/generate-module-adapters.py
py Tools/build/generate-module-adapters.py --check
```

소비 파일의 조건부 분기는 이 도구 모드에만 적용된다. 일반 컴파일에서는
`import Creator.Core.Window`, `import Creator.Platform.Windows`,
`import Creator.Contracts`, `import Creator.Concurrency`, `import Creator.AppBackend.WinAPI`를 직접 처리한다.

## 수명

UI 스레드가 네이티브 창을 생성하고 이벤트를 처리한다.
종료 시 App과 렌더링 소비자를 정리한 뒤 콜백을 분리하고,
AppWindow 연결 해제 → HWND 파괴 → Windows App Runtime·apartment 해제 순으로 정리한다.
`EngineBootstrap`의 `WindowCallbackGuard`는 App이 살아 있을 때 콜백을 해제한다.

## Windows SDK 모듈 경계

Windows SDK의 텍스트 include는 `Engine/Platform/WindowsSDK/`에 모은다.
일반 엔진·에디터·플레이어 코드는 해당 C++ 헤더 유닛을 직접 import한다.
헤더 유닛은 SDK 타입뿐 아니라 `WM_*`, `HRESULT` 검사, `IID_PPV_ARGS` 같은
매크로도 전달한다. 이름 있는 엔진 모듈의 API와 이 SDK 매크로 경계는 구분한다.

- `Creator.WinAPI.h`: WinAPI·COM·WRL·입력·파일·프로세스·진단 SDK.
- `Creator.GraphicsSDK.h`: D3D12·DXGI·WIC·그래픽 진단 SDK.
- `Creator.LegacyGraphicsSDK.h`: 명시적으로 D3D11 인터페이스를 쓰는 소비자.
- `Creator.AppRuntimeSDK.h`: AppWindow 구현에서 사용하는 Windows App Runtime 부트스트랩.
- `Creator.GameInputSDK.h`: 엔진의 vcpkg GameInput v3 헤더. Windows SDK의 구버전 헤더와 분리한다.

다섯 헤더 유닛은 EngineContracts에서 구성별로 빌드하고 소비 프로젝트에 공개한다.
정적 CRT를 사용하는 독립 런처는 같은 WinAPI 유닛 소스와 `std` 모듈을 자신의 CRT 설정으로 별도 빌드한다.
유니코드·NOMINMAX·표준 전처리기 설정을 native 프로젝트 사이에 통일한다.
외부 라이브러리와 reflgen·독립 검사 모드의 원본 헤더는 수정하지 않는다.
C++/WinRT 투영은 기존 `winrt.*` 이름 있는 모듈을 유지한다. HWND interop 헤더만
`Creator.Platform.Windows.AppWindow.cpp` 내부에서 `WINRT_IMPORT_MODULE`로 연결한다.

`import winrt_base;`는 네이티브 COM에도 쓸 수 있는 `winrt::com_ptr`를 제공한다.
엔진의 COM 소유 포인터는 `winrt::com_ptr`로 통일한다. `get()`은 빌려 보며,
`copy_from()`은 빌린 포인터의 참조를 추가하고, `attach()`와 `detach()`는 참조를 이전한다.
`put()`은 보유 중인 참조를 먼저 해제하므로 출력 인수에 사용하고, 해제는 `nullptr` 대입으로 명시한다.
기존 `As()`의 HRESULT 분기는 `QueryComInterface()`로 연결한다. 이 함수는 `as<T>()`의
`winrt::hresult_error`를 잡아 원래 오류 코드를 반환하며 실패한 대상 포인터는 비운다.
메모리 부족으로 던져지는 `std::bad_alloc`은 `E_OUTOFMEMORY`로 반환한다.

검증:

```powershell
py Tools/regression/verify-windows-sdk-modules.py
MSBuild Tools/regression/WinApiModuleProbe.vcxproj /t:Build /p:Configuration=Debug /p:Platform=x64
Build/Validation/WinApiModules/x64-Debug/WinApiModuleProbe.exe
```

이 검사는 원본 SDK include 없이 실제 COM 생성·QueryInterface, 숨김 창 생성,
생성 중 싱글턴 조회, 콜백 전달·분리, HWND 파괴를 확인한다.
COM 소유권 검사는 복사·이동·빌린 참조 유지·참조 이전·출력 덮어쓰기·인터페이스 조회 실패와 최종 파괴 횟수를 확인한다.
