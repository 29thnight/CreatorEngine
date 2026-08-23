# `Memory` 네임스페이스 및 `DeferredDeleter` 클래스

## `Memory` 네임스페이스

### 1. `AllocateAndCopy`

메모리를 할당하고, 주어진 크기만큼 데이터를 복사합니다.

```cpp
inline void AllocateAndCopy(void* pDst, const void* pSrc, uint32 size);
```

- `pDst`: 데이터를 복사할 대상 포인터.
- `pSrc`: 복사할 원본 데이터 포인터.
- `size`: 복사할 데이터의 크기.

### 2. `SafeDelete`

타입에 따라 객체를 안전하게 삭제합니다. `IUnknown`을 상속받은 객체는 `Release()`를 호출하고, `void*`는 `free()`, 그 외 객체는 `delete`를 호출합니다.

```cpp
template<typename T>
inline void SafeDelete(T*& ptr);
```

- `ptr`: 삭제할 객체의 포인터.

## `DeferredDeleter` 클래스

`DeferredDeleter`는 컨테이너에 저장된 포인터들을 지정된 조건에 따라 지연 삭제하는 클래스입니다.

### 생성자

```cpp
DeferredDeleter(Container* container, Func func = [](T* ptr) { return true; });
```

- `container`: 삭제할 포인터들이 들어있는 컨테이너.
- `func`: 삭제 조건을 지정하는 함수 객체 (기본값: 모든 포인터 삭제).

### 소멸자

컨테이너의 포인터들을 지정된 조건에 맞게 삭제합니다.

### `operator()`

```cpp
void operator()(Container* container);
```

- `container`: 지연 삭제할 포인터들을 가진 컨테이너를 설정합니다.

## 사용 예시

```cpp
std::vector<MyClass*> container;
DeferredDeleter<MyClass, std::vector<MyClass*>> deleter(&container);
```

`DeferredDeleter`는 컨테이너의 포인터들을 지정된 조건에 따라 삭제합니다. `container`가 범위를 벗어나면 자동으로 포인터가 삭제됩니다.
```
