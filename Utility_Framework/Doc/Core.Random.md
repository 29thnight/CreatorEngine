
# Random 클래스

`Random` 클래스는 주어진 범위 내에서 난수를 생성하는 클래스로, 정수 및 실수 타입을 지원합니다. 이 클래스는 C++의 `std::random_device`와 `std::mt19937`을 기반으로 구현되어 있으며, `std::uniform_int_distribution` 또는 `std::uniform_real_distribution`을 사용하여 난수를 생성합니다.

## 클래스 개요

`Random` 클래스는 지정된 최소값과 최대값 사이에서 난수를 생성합니다. 클래스는 생성자에서 난수 생성기의 시드를 초기화하고, 난수를 생성할 때마다 분포를 사용하여 값들을 반환합니다.

### 템플릿 매개변수

- `T`: 난수를 생성할 타입으로, `std::is_arithmetic_v<T>`에 의해 숫자 타입 (정수 및 실수)만 허용됩니다.

## 멤버 함수

### `Random(T min, T max)`

- 생성자.
- 지정된 최소값과 최대값 (`min`, `max`)을 사용하여 난수 생성기를 초기화합니다.

#### 매개변수:
- `min`: 생성할 난수의 최소값.
- `max`: 생성할 난수의 최대값.

### `std::vector<T> Generate(size_t count)`

- `count` 개수만큼 난수를 생성하여 `std::vector`로 반환합니다.
- `std::uniform_int_distribution` 또는 `std::uniform_real_distribution`에 의해 범위 내에서 난수가 생성됩니다.

#### 매개변수:
- `count`: 생성할 난수의 개수.

#### 반환값:
- `std::vector<T>`: 생성된 난수들의 벡터.

### `T Generate()`

- 단일 난수를 생성하여 반환합니다.

#### 반환값:
- 생성된 난수 (타입 `T`).

## 멤버 변수

- `_device`: 난수 생성에 사용되는 `std::random_device`.
- `_generator`: 난수를 생성하는 `std::mt19937` 엔진.
- `_distribution`: 정수형일 경우 `std::uniform_int_distribution<T>`, 실수형일 경우 `std::uniform_real_distribution<T>`.

## 사용 예시

### 정수 난수 생성

```cpp
#include <iostream>
#include "Random.h"

int main()
{
    Random<int> randomInt(1, 100); // 1과 100 사이의 정수 난수 생성
    std::vector<int> numbers = randomInt.Generate(5); // 5개의 난수 생성

    for (int num : numbers)
    {
        std::cout << num << std::endl;
    }

    return 0;
}
```

### 실수 난수 생성

```cpp
#include <iostream>
#include "Random.h"

int main()
{
    Random<float> randomFloat(0.0f, 1.0f); // 0과 1 사이의 실수 난수 생성
    std::vector<float> numbers = randomFloat.Generate(5); // 5개의 난수 생성

    for (float num : numbers)
    {
        std::cout << num << std::endl;
    }

    return 0;
}
```

### 단일 난수 생성

```cpp
#include <iostream>
#include "Random.h"

int main()
{
    Random<int> randomInt(1, 100);
    int singleRandom = randomInt.Generate(); // 단일 난수 생성
    std::cout << singleRandom << std::endl;

    return 0;
}
```

## 결론

`Random` 클래스는 주어진 범위 내에서 효율적으로 난수를 생성할 수 있는 유용한 클래스입니다. 정수와 실수 타입을 지원하며, 다양한 용도에 맞게 난수를 쉽게 생성할 수 있습니다.
