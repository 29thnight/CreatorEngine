# DirectX11Helper

## 개요

이 헤더 파일은 DirectX 11 및 관련 라이브러리에서 발생할 수 있는 예외 처리, 파일 읽기 작업, 리소스 매핑, 텍스처 생성 등을 지원하는 여러 유틸리티 함수와 클래스를 제공합니다.

## 네임스페이스

### `DirectX11`

이 네임스페이스는 DirectX 11과 관련된 예외 처리, 파일 읽기, 리소스 매핑 등을 담당하는 클래스와 함수들을 포함합니다.

#### `ComException` 클래스
`ComException` 클래스는 HRESULT 오류 코드에 대한 예외를 나타냅니다. DirectX 11 함수에서 오류가 발생할 때 이 예외를 던질 수 있습니다.

- **메서드**
  - `what()`: 예외가 발생한 HRESULT 값을 포맷하여 문자열로 반환합니다.
  - `CreateException(HRESULT hr)`: HRESULT 오류 코드를 이용해 `ComException`을 생성하는 정적 함수.

#### `ThrowIfFailed(HRESULT hr)` 함수
- **설명**: HRESULT 값이 실패 상태일 경우 `ComException`을 던지는 함수입니다.
- **매개변수**: `hr`: 오류 상태를 나타내는 HRESULT 코드.
- **반환값**: 없음.

#### `ReadDataAsync(const file::path& fileName)` 함수
- **설명**: 비동기적으로 파일을 읽고 `std::vector<byte>` 형식으로 데이터를 반환하는 함수입니다.
- **매개변수**: `fileName`: 읽을 파일의 경로.
- **반환값**: `std::vector<byte>`: 읽은 파일의 바이너리 데이터를 포함하는 벡터.

#### `ConvertDipsToPixels(float dips, float dpi)` 함수
- **설명**: 디스플레이 인치 비율(DPI)을 사용하여 DIPs(디스플레이 인치 단위)를 화면 픽셀 단위로 변환합니다.
- **매개변수**:
  - `dips`: 변환할 DIPs 값.
  - `dpi`: 화면 DPI.
- **반환값**: 변환된 픽셀 수.

#### `SharedMap` 클래스
- **설명**: Direct3D 리소스를 매핑 및 언매핑하는 유틸리티 클래스입니다. 리소스를 안전하게 처리하도록 돕습니다.
- **생성자**: 
  - `SharedMap(ID3D11DeviceContext* pDeviceContext, ID3D11Resource* pResource, D3D11_MAPPED_SUBRESOURCE* pMappedResource, uint32 subresource = 0, D3D11_MAP mapType = D3D11_MAP_WRITE_DISCARD, uint32 mapFlags = 0)`
    - `pDeviceContext`: 디바이스 컨텍스트.
    - `pResource`: 매핑할 리소스.
    - `pMappedResource`: 리소스의 매핑된 데이터.
    - `subresource`: 서브리소스 인덱스.
    - `mapType`: 리소스를 매핑할 유형 (예: `D3D11_MAP_WRITE_DISCARD`).
    - `mapFlags`: 매핑 플래그.
- **소멸자**: 리소스를 언매핑합니다.

#### `SdkLayersAvailable()` 함수
- **설명**: 디버그 레이어가 사용할 수 있는지 확인하는 함수입니다. 디버그 빌드를 사용할 때만 활성화됩니다.
- **반환값**: `bool`: 디버그 레이어가 활성화되어 있으면 `true`, 아니면 `false`.

### `DirectX` 네임스페이스

이 네임스페이스는 DirectX 관련 텍스처 생성 작업을 위한 유틸리티 함수를 제공합니다.

#### `CreateTGATextureFormFile(ID3D11Device* pDevice, const wchar_t* pTexturePath, ID3D11ShaderResourceView** ppTexture)` 함수
- **설명**: TGA 파일을 읽어 `ID3D11ShaderResourceView`를 생성하는 함수입니다.
- **매개변수**:
  - `pDevice`: Direct3D 11 디바이스.
  - `pTexturePath`: 텍스처 파일 경로.
  - `ppTexture`: 생성된 `ID3D11ShaderResourceView`를 받을 포인터.
- **반환값**: `HRESULT`: 성공 또는 오류 상태를 나타내는 HRESULT 코드.

## 사용 예시

```cpp
#include "DirectX11Helper.h"

// 파일 데이터를 비동기적으로 읽기
auto dataTask = DirectX11::ReadDataAsync(L"example.tga");
dataTask.then([](std::vector<byte> data) {
    // 데이터 처리 로직
});

// TGA 텍스처 로드
ID3D11ShaderResourceView* texture = nullptr;
HRESULT hr = DirectX::CreateTGATextureFormFile(device, L"texture.tga", &texture);
if (FAILED(hr)) {
    // 오류 처리
}
