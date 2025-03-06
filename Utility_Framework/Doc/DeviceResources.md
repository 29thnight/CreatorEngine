# `DeviceResources` 클래스

`DeviceResources` 클래스는 DirectX 11 기반 애플리케이션에서 장치와 관련된 리소스를 관리하는 역할을 합니다. 이 클래스는 주로 Direct3D 장치, 스왑 체인, 렌더 타겟, D2D 및 DWrite 리소스 등의 관리 및 설정을 담당합니다. 또한, 장치가 손실되었을 때 복구를 위한 로직을 포함하고 있습니다.

## 클래스 구조

### 1. `Size 구조체`
- 화면 크기를 나타내는 구조체입니다. `width`와 `height` 값을 포함합니다.

```cpp
struct Size
{
    float width;   // 화면의 너비
    float height;  // 화면의 높이
};
```

### 2. `IDeviceNotify 인터페이스`
- 장치가 손실되거나 복구되었을 때 알림을 받기 위한 인터페이스입니다. `OnDeviceLost`와 `OnDeviceRestored` 두 개의 추상 메서드를 정의합니다.

```cpp
interface IDeviceNotify
{
    virtual void OnDeviceLost() = 0;       // 장치가 손실되었을 때 호출됩니다.
    virtual void OnDeviceRestored() = 0;   // 장치가 복구되었을 때 호출됩니다.
};
```

### 3. `DeviceResources 클래스`
- DirectX 11 장치와 관련된 리소스를 관리하는 핵심 클래스입니다. 이 클래스는 장치 리소스를 생성하고, 화면 크기를 관리하며, 장치 손실을 처리합니다.

#### 주요 메서드

- `SetWindow(CoreWindow& window)`: 윈도우를 설정합니다.
- `SetLogicalSize(Size logicalSize)`: 논리적 화면 크기를 설정합니다.
- `SetDpi(float dpi)`: DPI(밀도)를 설정합니다.
- `ValidateDevice()`: 장치가 유효한지 확인합니다.
- `HandleDeviceLost()`: 장치 손실 처리 로직을 실행합니다.
- `RegisterDeviceNotify(IDeviceNotify* deviceNotify)`: 장치 손실 및 복구 알림을 받을 객체를 등록합니다.
- `Trim()`: 장치 리소스를 정리합니다.
- `Present()`: 화면을 렌더링합니다.

#### 주요 속성

- `GetOutputSize()`: 출력 크기를 반환합니다.
- `GetLogicalSize()`: 논리적 화면 크기를 반환합니다.
- `GetDpi()`: DPI 값을 반환합니다.
- `GetD3DDevice()`: Direct3D 11 장치를 반환합니다.
- `GetD3DDeviceContext()`: Direct3D 11 장치 컨텍스트를 반환합니다.
- `GetSwapChain()`: 스왑 체인을 반환합니다.
- `GetDeviceFeatureLevel()`: 장치 기능 레벨을 반환합니다.
- `GetBackBufferRenderTargetView()`: 백 버퍼 렌더 타겟 뷰를 반환합니다.
- `GetDepthStencilView()`: 깊이 스텐실 뷰를 반환합니다.
- `GetScreenViewport()`: 화면 뷰포트를 반환합니다.
- `GetD2DFactory()`: D2D 팩토리를 반환합니다.
- `GetD2DDevice()`: D2D 디바이스를 반환합니다.
- `GetD2DDeviceContext()`: D2D 디바이스 컨텍스트를 반환합니다.
- `GetD2DTargetBitmap()`: D2D 타겟 비트맵을 반환합니다.
- `GetDWriteFactory()`: DWrite 팩토리를 반환합니다.
- `GetWicImagingFactory()`: WIC 이미징 팩토리를 반환합니다.

### 4. `기타 내부 메서드`
- `CreateDeviceIndependentResources()`: 장치 독립적인 리소스를 생성합니다.
- `CreateDeviceResources()`: 장치 관련 리소스를 생성합니다.
- `CreateWindowSizeDependentResources()`: 창 크기에 종속적인 리소스를 생성합니다.
- `UpdateRenderTargetSize()`: 렌더 타겟 크기를 갱신합니다.

### 5. `멤버 변수`

- `m_d3dDevice`, `m_d3dContext`, `m_swapChain`: Direct3D 11 장치, 장치 컨텍스트, 스왑 체인.
- `m_d3dRenderTargetView`, `m_d3dDepthStencilView`: 렌더 타겟 뷰, 깊이 스텐실 뷰.
- `m_d2dFactory`, `m_d2dDevice`, `m_d2dContext`: D2D 관련 리소스.
- `m_dwriteFactory`, `m_wicFactory`: DWrite, WIC 관련 리소스.
- `m_window`: `CoreWindow` 객체 포인터.
- `m_d3dFeatureLevel`: 장치의 기능 레벨.
- `m_d3dRenderTargetSize`, `m_outputSize`, `m_logicalSize`: 화면 크기 관련 변수들.
- `m_dpi`, `m_effectiveDpi`: DPI 값.
- `m_deviceNotify`: 장치 알림 객체 포인터.

## 사용 예시

```cpp
DirectX11::DeviceResources deviceResources;
deviceResources.SetWindow(window);
deviceResources.SetLogicalSize({ 1920, 1080 });
deviceResources.SetDpi(96.0f);
deviceResources.ValidateDevice();
deviceResources.Present();
```

이 예시에서는 `DeviceResources` 객체를 생성하고, 윈도우와 화면 크기를 설정한 후, 장치를 확인하고 화면을 렌더링합니다.
```
