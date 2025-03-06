# ClassProperty

이 파일은 **스레드 안전한 싱글톤 템플릿 클래스**와 복사 및 이동을 방지하기 위한 `Noncopyable` 클래스를 제공합니다.  
이를 통해 특정 클래스의 단일 인스턴스를 안전하게 관리할 수 있습니다.

---

## 클래스 설명

### `Noncopyable`

`Noncopyable`은 복사 및 이동 연산을 방지하기 위한 유틸리티 클래스입니다.  
이 클래스를 상속받으면 파생 클래스는 복사와 이동이 불가능합니다.

#### 멤버
- **생성자**: 
  - `protected` 접근 제한자로 기본 생성.
- **소멸자**: 
  - `protected` 접근 제한자로 기본 소멸.
- **삭제된 멤버**:
  - 복사 생성자 (`Noncopyable(const Noncopyable&) = delete;`)
  - 이동 생성자 (`Noncopyable(Noncopyable&&) = delete;`)
  - 복사 대입 연산자 (`Noncopyable& operator=(const Noncopyable&) = delete;`)
  - 이동 대입 연산자 (`Noncopyable& operator=(Noncopyable&&) = delete;`)

#### 사용 예시
```cpp
class MyClass : public Noncopyable
{
    // 복사 및 이동이 불가능한 클래스
};
```

### `Singleton<T>`

`Singleton`은 제네릭 타입 `T`에 대해 단일 인스턴스를 생성하고 관리할 수 있는 템플릿 클래스입니다.  
멀티스레드 환경에서도 안전하게 동작하도록 설계되었습니다.

#### 멤버

- **정적 함수**
  - `inline static const std::shared_ptr<T>& GetInstance()`  
    싱글톤 인스턴스를 반환하며, 호출 시 단일 인스턴스를 초기화합니다.
    ```cpp
    auto instance = MySingleton::GetInstance();
    ```

- **`Deleter` 구조체**
  - 역할: 커스텀 삭제자로, 싱글톤 인스턴스의 삭제를 책임집니다.
    ```cpp
    struct Deleter
    {
        void operator()(T* instance)
        {
            delete instance;
        }
    };
    ```

- **정적 변수**
  - `std::shared_ptr<T> s_instance`: 싱글톤 인스턴스를 저장하는 공유 포인터입니다.
  - `std::once_flag s_onceFlag`: 싱글톤 인스턴스를 단 한 번만 초기화하도록 보장하는 플래그입니다.

#### 사용 예시

```cpp
#include "ClassProperty.h"

// 싱글톤 클래스를 정의
class MySingleton : public Singleton<MySingleton>
{
    friend class Singleton<MySingleton>; // Singleton이 private 생성자에 접근할 수 있도록 설정
private:
    MySingleton() = default; // 생성자를 private으로 제한
public:
    void DoSomething() { /* 작업 수행 */ }
};

int main()
{
    // 싱글톤 인스턴스 가져오기
    auto instance = MySingleton::GetInstance();
    instance->DoSomething(); // 작업 수행

    return 0;
}
```

