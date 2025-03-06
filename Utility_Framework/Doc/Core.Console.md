
# `CoreConsole` 클래스

`CoreConsole` 클래스는 C#의 `Console` 클래스와 유사한 기능을 제공하며, 다양한 형식의 출력을 지원합니다. 이 클래스는 `fmt` 라이브러리를 이용하여 포맷된 출력을 구현합니다.

## 클래스 설명

`CoreConsole` 클래스는 템플릿 기반의 `Write` 및 `WriteLine` 메서드를 제공하여 콘솔에 출력할 수 있습니다. `Write` 메서드는 지정된 `FILE*`에 포맷된 문자열을 출력하고, `WriteLine` 메서드는 출력 후 새 줄을 추가합니다.

## 주요 메서드

### `Write`

```cpp
template<typename... T>
void Write(format_string<T...> fmt, T&&... args);
```

`Write` 메서드는 지정된 포맷 문자열(`fmt`)과 그에 맞는 인자(`args...`)를 받아 콘솔에 출력합니다.

#### 예시

```cpp
CoreConsole Console;
Console.Write("Hello, {}!", "World");  // 콘솔에 "Hello, World!"를 출력
```

### `WriteLine`

```cpp
template<typename... T>
void WriteLine(format_string<T...> fmt, T&&... args);
```

`WriteLine` 메서드는 `Write`와 유사하지만, 출력 후 자동으로 새로운 줄을 추가합니다.

#### 예시

```cpp
CoreConsole Console;
Console.WriteLine("This is a line with a number: {}", 42);  // 콘솔에 "This is a line with a number: 42"를 출력하고 새 줄을 추가
```

## 예제

```cpp
CoreConsole Console;
Console.WriteLine("Hello, World!");  // 콘솔에 출력
Console.WriteLine("Value: {}", 42);  // 값을 포맷하여 출력
```

## 주의사항

- `Write`와 `WriteLine`은 포맷된 문자열을 출력할 때 UTF-8을 사용합니다.
- `Write` 메서드는 `FILE*`을 지정하여 다른 파일에 출력할 수 있습니다.
- `WriteLine` 메서드는 콘솔 출력 후 줄 바꿈을 자동으로 처리합니다.

---

`CoreConsole` 클래스를 사용하여 다양한 형식의 출력을 쉽게 구현할 수 있습니다. 템플릿 기반으로 설계되어 유연하게 다양한 데이터 타입을 처리할 수 있습니다.
