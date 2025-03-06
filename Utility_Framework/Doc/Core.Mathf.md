# Mathf - 수학 Helper Utility

`Mathf` 네임스페이스는 3D 그래픽 및 게임 개발에 유용한 다양한 수학적 함수와 구조체들을 포함하고 있습니다. 주요 클래스와 함수들은 다음과 같습니다.

## 클래스 및 구조체

### Vector3

`Vector3` 클래스는 3D 벡터를 표현하는데 사용됩니다. `DirectX::XMVECTOR` 타입을 기반으로 하며, 여러 가지 유용한 연산자와 함수들을 제공합니다.

#### 멤버 변수

- `x`, `y`, `z`: 벡터의 각 요소를 나타내는 `float` 값.
- `_v`: 내부적으로 사용되는 `XMFLOAT3` 타입의 데이터.

#### 주요 함수

- **생성자**
  - `Vector3()`: 기본 생성자 (0, 0, 0).
  - `Vector3(float, float, float)`: 주어진 값으로 벡터를 초기화.
  - `Vector3(const XMFLOAT3&)`: `XMFLOAT3`에서 변환.
  - `Vector3(const XMVECTOR&)`: `XMVECTOR`에서 변환.

- **연산자**
  - `operator+`: 벡터 더하기 연산.
  - `operator+=`: 벡터 더하기 및 할당.
  - `operator==`: 벡터 비교.

- **유틸리티 함수**
  - `normalize()`: 벡터를 정규화.
  - `normalized()`: 정규화된 벡터 반환.
  - `print()`: 벡터를 문자열로 변환.

#### 정적 상수

- `Zero`: (0, 0, 0).
- `Up`: (0, 1, 0).
- `Down`: (0, -1, 0).
- `Left`: (-1, 0, 0).
- `Right`: (1, 0, 0).
- `Forward`: (0, 0, 1).
- `Back`: (0, 0, -1).

#### 기타 함수

- **Rand()**: (0, 1) 범위에서 랜덤한 3D 벡터를 생성하고 정규화합니다.

---

### Vector2

`Vector2` 클래스는 2D 벡터를 표현합니다. `DirectX::XMVECTOR`를 기반으로 하며, `Vector3`와 비슷한 연산자와 함수들을 포함합니다.

#### 멤버 변수

- `x`, `y`: 벡터의 각 요소를 나타내는 `float` 값.
- `_v`: 내부적으로 사용되는 `XMFLOAT2` 데이터.

#### 주요 함수

- **생성자**
  - `Vector2()`: 기본 생성자 (0, 0).
  - `Vector2(float, float)`: 주어진 값으로 벡터를 초기화.
  - `Vector2(const XMFLOAT2&)`: `XMFLOAT2`에서 변환.
  - `Vector2(const XMVECTOR&)`: `XMVECTOR`에서 변환.

- **연산자**
  - `operator+`: 벡터 더하기 연산.
  - `operator+=`: 벡터 더하기 및 할당.
  - `operator==`: 벡터 비교.

- **유틸리티 함수**
  - `normalize()`: 벡터를 정규화.
  - `normalized()`: 정규화된 벡터 반환.

#### 정적 상수

- `Zero`: (0, 0).
- `Up`: (0, 1).
- `Down`: (0, -1).
- `Left`: (-1, 0).
- `Right`: (1, 0).

---

### Quaternion

`Quaternion` 클래스는 회전 변환을 표현하기 위한 사원수(Quaternion)를 다룹니다. 이 클래스는 회전 행렬을 `XMMATRIX`로 변환하거나 사원수를 이용한 다양한 회전 관련 연산을 제공합니다.

#### 주요 함수

- **정적 함수**
  - `Identity()`: 단위 사원수를 반환.
  - `FromAxisAngle(const XMVECTOR& axis, const float angle)`: 축과 각도로부터 회전 사원수를 생성.
  - `Lerp(const Quaternion& from, const Quaternion& to, float t)`: 선형 보간법으로 사원수를 계산.
  - `Slerp(const Quaternion& from, const Quaternion& to, float t)`: 구면 선형 보간법으로 사원수를 계산.
  - `ToEulerRad()`: 사원수를 라디안 단위로 변환하여 오일러 각을 반환.
  - `ToEulerDeg()`: 사원수를 도 단위로 변환하여 오일러 각을 반환.

- **연산자**
  - `operator+`: 사원수 더하기.
  - `operator*`: 사원수 곱셈.
  - `operator*(float)`: 사원수 스칼라 곱셈.
  - `operator==`: 사원수 비교.

- **기타 함수**
  - `Dot(const Quaternion& q)`: 두 사원수의 내적.
  - `Len()`: 사원수의 길이.
  - `Inverse()`: 사원수의 역.
  - `Conjugate()`: 사원수의 켤레.
  - `Matrix()`: 사원수를 회전 행렬로 변환.
  - `Normalize()`: 사원수를 정규화.
  - `TransformVector(const Vector3& v)`: 사원수로 벡터를 변환.

---

### FrustumPlaneset

`FrustumPlaneset` 구조체는 3D 뷰 프러스텀(시점)을 나타냅니다. 이 구조체는 6개의 평면(좌/우, 상/하, 근/원)을 포함하고, 이를 통해 카메라의 시야 범위를 정의합니다.

#### 주요 함수

- **ExtractFromMatrix()**: 주어진 투영 행렬로부터 프러스텀 평면을 추출합니다.

---

### Helper 함수들

- **lerp(T low, T high, float t)**: 선형 보간을 수행합니다. `low`와 `high` 값 사이를 `t` 비율로 보간합니다.
- **ClampedIncrementOrDecrement(T& val, int upOrDown, int lo, int hi)**: 값이 주어진 범위 내에서 증가 또는 감소하도록 합니다.
- **Clamp(T& val, const T lo, const T hi)**: 값이 주어진 범위 내에 있도록 강제로 제한합니다.
- **Clamp(T val, const T lo, const T hi)**: 값을 반환하되, 범위를 벗어나지 않도록 클램프합니다.

---

## 상수

- **PI**: 원주율 (3.14159265358979323846).
- **Deg2Rad**: 도에서 라디안으로 변환하는 상수.
- **Rad2Deg**: 라디안에서 도로 변환하는 상수.

---
