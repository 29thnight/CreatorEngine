# CoreWindow 클래스

## 개요

`CoreWindow` 클래스는 Windows 애플리케이션 창(`HWND`)을 간편하게 래핑한 클래스로, 창 생성, 메시지 처리 및 메시지 루프 실행을 지원합니다. 커스텀 메시지 핸들러 등록과 초기화 작업을 유연하게 처리할 수 있는 메커니즘을 제공합니다.

## 메서드

### `CoreWindow(HINSTANCE hInstance, const wchar_t* title, int width, int height)`
- **설명**: 주어진 인스턴스, 제목, 너비 및 높이로 창을 초기화합니다.
- **매개변수**:
  - `hInstance`: 애플리케이션의 인스턴스 핸들.
  - `title`: 창의 제목.
  - `width`: 창의 너비.
  - `height`: 창의 높이.

### `~CoreWindow()`
- **설명**: 생성된 창을 정리하고 윈도우 클래스를 등록 해제하는 소멸자입니다.

### `template <typename Instance> void RegisterHandler(UINT message, Instance* instance, LRESULT(Instance::* handler)(HWND, WPARAM, LPARAM))`
- **설명**: 특정 메시지에 대한 커스텀 메시지 핸들러를 등록합니다.
- **매개변수**:
  - `message`: 처리할 메시지 (예: `WM_SIZE`).
  - `instance`: 메시지를 처리할 객체의 인스턴스.
  - `handler`: 메시지가 도착했을 때 호출할 핸들러 함수.

### `template <typename Initializer> CoreWindow InitializeTask(Initializer fn_initializer)`
- **설명**: 주어진 초기화 함수로 작업을 초기화합니다.
- **매개변수**:
  - `fn_initializer`: 창을 초기화하거나 기타 설정 작업을 수행하는 함수.

### `template <typename MessageLoop> void Then(MessageLoop fn_messageLoop)`
- **설명**: 커스텀 메시지 루프를 실행합니다. 메시지가 없을 경우 주어진 함수가 실행됩니다.
- **매개변수**:
  - `fn_messageLoop`: 메시지 루프가 없을 때 실행될 함수.

### `HWND GetHandle() const`
- **설명**: 창 핸들(`HWND`)을 반환합니다.

### `int GetWidth() const`
- **설명**: 창의 너비를 반환합니다.

### `int GetHeight() const`
- **설명**: 창의 높이를 반환합니다.

### `static CoreWindow* GetForCurrentInstance()`
- **설명**: 현재 `CoreWindow` 인스턴스를 반환합니다.

## 사용 예시

```cpp
#include "CoreWindow.h"

MAIN_ENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow)
{
	Core::App app;
	app.Initialize(hInstance, L"HeroP Editor", 1280, 720);

	return 0;
}

void Core::App::Initialize(HINSTANCE hInstance, const wchar_t* title, int width, int height)
{
	CoreWindow coreWindow(hInstance, L"HeroP Editor", 1280, 720);
	SetWindow(coreWindow);
	InputManagement->Initialize(coreWindow.GetHandle());
	Run();
}

void Core::App::SetWindow(CoreWindow& coreWindow)
{
	coreWindow.RegisterHandler(WM_INPUT, this, &App::ProcessRawInput);
	coreWindow.RegisterHandler(WM_CLOSE, this, &App::Shutdown);
}

void Core::App::Run()
{
	CoreWindow::GetForCurrentInstance()->InitializeTask([&]
	{
		// 초기화 작업
	})
	.Then([&]
	{
		// 메시지 루프
	});
}

LRESULT Core::App::Shutdown(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
	m_windowClosed = true;
	PostQuitMessage(0);
	return 0;
}

LRESULT Core::App::ProcessRawInput(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
	InputManagement->ProcessRawInput(lParam);
	return 0;
}
