# Banchmark 클래스

`Banchmark` 클래스는 코드 실행 시간을 측정하는 데 사용됩니다. 이 클래스는 C++의 `<chrono>` 라이브러리를 사용하여 고해상도 시간 측정을 수행하며, 객체가 소멸될 때까지의 경과 시간을 밀리초 단위로 출력합니다.

## 사용 방법

이 클래스를 사용하면 코드 블록의 실행 시간을 자동으로 측정할 수 있습니다. 클래스 객체를 생성하고, 해당 객체의 소멸 시점에 경과 시간을 출력합니다.

### 예시 코드

```cpp
#include "Banchmark.h"

void SomeFunction()
{
    Banchmark benchmark; // 시간 측정을 시작합니다.

    // 실행할 코드...
}

int main()
{
    SomeFunction();
    return 0;
}
```
### 클래스 설명

#### `Banchmark` 클래스

`Banchmark` 클래스는 코드 실행 시간을 측정하는 클래스입니다. 생성된 객체가 소멸될 때까지의 시간 차이를 측정하여 밀리초 단위로 출력합니다. 이 클래스는 C++의 `<chrono>` 라이브러리의 `high_resolution_clock`을 사용하여 고해상도의 시간을 측정합니다.

- **생성자**: 객체가 생성되면 `high_resolution_clock`을 사용하여 현재 시간을 기록합니다. 이 시점부터 경과된 시간이 추적됩니다.
- **소멸자**: 객체가 소멸될 때, 생성 시점부터 현재 시간까지의 차이를 계산하여 경과 시간을 밀리초(ms) 단위로 출력합니다.

따라서 이 클래스는 특정 코드 블록의 실행 시간을 측정하고, 그 결과를 자동으로 출력하는 용도로 유용합니다. 주로 성능 벤치마크나 코드 최적화, 시간 측정을 위한 디버깅에 사용될 수 있습니다.

#### 주요 멤버 변수

- **`_start`**: `std::chrono::high_resolution_clock::time_point` 타입의 변수로, 객체가 생성될 때 기록된 시작 시간을 저장합니다.

#### 주요 타입 정의

- **`_clock`**: `std::chrono::high_resolution_clock`을 정의하여 고해상도 시간을 측정합니다.
- **`_duration`**: `std::chrono::duration<double, std::milli>`로, 시간 차이를 밀리초 단위로 나타냅니다.
- **`_timePoint`**: `std::chrono::high_resolution_clock::time_point` 타입으로, 특정 시간의 순간을 나타냅니다.
