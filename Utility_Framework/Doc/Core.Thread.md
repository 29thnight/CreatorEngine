
# Task 클래스 및 ThreadPool 클래스

## Task 클래스

`Task` 클래스는 비동기 작업을 처리하고, 그 결과를 `Future` 객체로 반환하여, 후속 작업을 연결할 수 있게 하는 클래스입니다.

### 생성자
```cpp
explicit Task(std::shared_ptr<std::packaged_task<ResultType()>> task);
```
- `task`: 실행할 작업을 포장한 `packaged_task` 객체를 전달합니다.

### then 메서드
`then` 메서드는 현재 작업이 완료된 후, 그 결과를 받아서 실행할 후속 작업을 정의할 수 있게 해줍니다.

```cpp
template<typename NextFunc>
auto then(NextFunc&& nextFunc) -> Task<decltype(nextFunc(std::declval<ResultType>()))>;
```
- `nextFunc`: 현재 작업의 결과를 받아서 실행할 후속 작업을 정의하는 함수입니다.

### get_future 메서드
현재 작업의 결과를 `future`로 반환합니다.

```cpp
std::future<ResultType> get_future();
```

### execute 메서드
현재 작업을 즉시 실행합니다.

```cpp
void execute();
```

---

## ThreadPool 클래스

`ThreadPool` 클래스는 다수의 작업을 여러 스레드를 통해 비동기적으로 처리할 수 있도록 해주는 스레드 풀입니다. 내부적으로 `Singleton` 패턴을 사용하며, 외부에서 생성할 수 없습니다.

### 생성자
```cpp
explicit ThreadPool(size_t numThreads = std::thread::hardware_concurrency() - 2);
```
- `numThreads`: 풀에 생성할 스레드의 수. 기본적으로 시스템의 하드웨어 코어 수에서 2를 뺀 값으로 설정됩니다.

### 소멸자
```cpp
~ThreadPool();
```
스레드를 종료하고, 모든 작업이 완료될 때까지 기다립니다.

### AddTask 메서드
`AddTask` 메서드는 `Task` 객체를 생성하여 작업을 스레드 풀에 추가합니다.

```cpp
template<typename T>
auto AddTask(T task) -> Task<decltype(task())>;
```
- `task`: 실행할 작업을 전달합니다.

### WorkerLoop 메서드
작업 큐에서 작업을 가져와 처리하는 루프입니다.

```cpp
void WorkerLoop();
```

### CreateTask 함수
`ThreadPool`에 작업을 추가하고 결과를 반환하는 유틸리티 함수입니다.

```cpp
template<typename T>
inline auto CreateTask(T task) -> Task<decltype(task())>;
```

---

## 사용 예제

### 예제 1: 기본적인 작업 생성 및 실행

```cpp
#include <iostream>
#include "Core.Thread.h"

int main() {
    // 작업 생성
    auto task = CreateTask([]() {
        std::cout << "Task 1 실행!" << std::endl;
        return 42;
    });

    // 작업 완료 후 실행될 후속 작업 연결
    auto nextTask = task.then([](int result) {
        std::cout << "Task 1 결과: " << result << std::endl;
        std::cout << "Task 2 실행!" << std::endl;
    });

    // 작업 실행
    nextTask.execute();
    
    // 결과를 기다리고 출력
    nextTask.get_future().get();

    return 0;
}
```

### 예제 2: 여러 작업을 추가하여 비동기 처리

```cpp
#include <iostream>
#include "Core.Thread.h"

int main() {
    // 작업 1
    auto task1 = CreateTask([]() {
        std::cout << "Task 1 실행!" << std::endl;
        return 1;
    });

    // 작업 2 (Task 1 후속 작업)
    auto task2 = task1.then([](int result) {
        std::cout << "Task 1 결과: " << result << std::endl;
        std::cout << "Task 2 실행!" << std::endl;
        return 2;
    });

    // 작업 3 (Task 2 후속 작업)
    auto task3 = task2.then([](int result) {
        std::cout << "Task 2 결과: " << result << std::endl;
        std::cout << "Task 3 실행!" << std::endl;
        return 3;
    });

    // 작업 3 실행
    task3.execute();
    
    // 결과를 기다리고 출력
    task3.get_future().get();

    return 0;
}
```

### 예제 3: 스레드 풀을 활용하여 여러 작업을 동시에 실행

```cpp
#include <iostream>
#include "Core.Thread.h"

int main() {
    // 스레드 풀에서 작업 추가
    auto task1 = CreateTask([]() {
        std::cout << "Task 1 실행!" << std::endl;
        return 1;
    });

    auto task2 = CreateTask([]() {
        std::cout << "Task 2 실행!" << std::endl;
        return 2;
    });

    auto task3 = CreateTask([]() {
        std::cout << "Task 3 실행!" << std::endl;
        return 3;
    });

    // 결과를 기다리고 출력
    task1.get_future().get();
    task2.get_future().get();
    task3.get_future().get();

    return 0;
}
```

---

## 결론

`Task` 클래스는 비동기적으로 작업을 실행하고 그 결과를 후속 작업에 전달하는 기능을 제공합니다. `ThreadPool` 클래스는 여러 스레드를 관리하며 비동기 작업을 실행할 수 있도록 도와주는 유용한 도구입니다. 이를 통해 멀티스레딩 환경에서 효율적인 작업 처리가 가능해집니다.
