# Core::ASSERT

`Core::ASSERT`는 프로그램에서 조건이 만족하지 않을 때, 오류 메시지를 출력하고 디버거를 중단하는 함수입니다. 이 코드는 `EASTL`의 `ASSERT` 동작을 모방하여 작성되었습니다.

## 동작 설명

`Core::ASSERT`는 주어진 조건이 `false`일 경우, 아래와 같은 동작을 수행합니다:

1. **오류 메시지 출력**: 조건이 `false`일 경우, 해당 조건을 출력하는 오류 메시지가 콘솔에 표시됩니다.
2. **디버깅 도구에 메시지 전달**: 디버거가 연결되어 있는 경우, `OutputDebugStringA`를 통해 디버깅 도구에 메시지가 전달됩니다.
3. **디버깅 중단**: `__debugbreak()`를 사용하여 디버거가 활성화된 경우 프로그램 실행을 중단합니다.
4. **프로그램 종료**: 프로그램을 종료시키기 위해 `std::abort()`를 호출합니다.

## 사용 방법

### 1. `AssertionFailureFunction`과 `AssertionFailure` 함수

- `AssertionFailureFunction`: Assertion 실패 시 호출될 함수 포인터입니다. 기본적으로 `AssertionFailureFunctionDefault`가 사용되며, 이를 사용자 정의 함수로 변경할 수 있습니다.
  
- `SetAssertionFailureFunction`: Assertion 실패 시 사용할 사용자 정의 함수와 그 컨텍스트를 설정하는 함수입니다.
  
- `AssertionFailure`: Assertion 실패 시 호출되어 위에서 설정한 함수 포인터를 통해 Assertion을 처리합니다.

### 2. `CORE_ASSERT` 매크로

`CORE_ASSERT`는 조건이 `false`일 때 Assertion을 발생시키는 매크로입니다. 예를 들어:

```cpp
CORE_ASSERT(expression);
```
### `expression`이 false일 경우의 동작

`CORE_ASSERT(expression)`에서 `expression`이 `false`일 경우, `AssertionFailure` 함수가 호출됩니다. 이 함수는 다음과 같은 동작을 수행합니다:

1. **Assertion 실패 메시지 출력**: `AssertionFailure` 함수는 `expression`을 포함한 오류 메시지를 출력합니다. 기본적으로 `printf`를 사용하여 메시지를 콘솔에 출력합니다.

2. **디버깅 환경에서 오류 메시지 출력**: 디버깅 빌드에서, 만약 디버거가 활성화되어 있으면 `OutputDebugStringA`를 호출하여 디버거에게 오류 메시지를 전달합니다. 이 메시지는 디버깅 도구에서 확인할 수 있습니다.

3. **디버거 중단**: `__debugbreak()`를 호출하여 디버거가 연결된 경우, 프로그램 실행을 중단시킵니다. 이를 통해 개발자는 오류 발생 지점에서 코드 실행을 멈추고 상태를 조사할 수 있습니다.

4. **프로그램 종료**: `std::abort()`를 호출하여 프로그램을 강제로 종료시킵니다. 이를 통해 심각한 오류가 발생했음을 알리고, 프로그램이 더 이상 실행되지 않도록 합니다.

### 예시

다음은 `CORE_ASSERT`가 작동하는 예시입니다:

```cpp
int value = 0;
CORE_ASSERT(value != 0);  // value가 0일 경우 Assertion 실패
```
`CORE_ASSERT(value != 0)`가 `false`일 경우, `AssertionFailure`가 호출됩니다. 그 후에 발생하는 일은 다음과 같습니다:

1. **오류 메시지 출력**: 
   - 오류 메시지인 `"Assertion failed: value != 0"`가 콘솔에 출력됩니다.
   
2. **디버깅 도구에서 오류 추적**:
   - 만약 디버깅 환경에서 실행 중이라면, `OutputDebugStringA`를 사용하여 해당 메시지가 디버깅 도구로 전달됩니다. 이를 통해 디버깅 도구에서 오류 메시지를 확인하고, 프로그램 상태를 점검할 수 있습니다.

3. **디버거 중단**:
   - `__debugbreak()`가 호출되면, 디버거가 활성화된 상태에서 프로그램이 중단됩니다. 개발자는 이 시점에서 프로그램 실행을 멈추고, 호출 스택이나 변수 값을 확인하여 오류를 추적할 수 있습니다.

4. **프로그램 종료**:
   - 마지막으로 `std::abort()`가 호출되며, 프로그램이 종료됩니다. 이는 심각한 오류를 감지하고, 더 이상 프로그램을 실행할 수 없다는 것을 알리기 위한 조치입니다.

이 과정은 `CORE_ASSERT`가 실패한 상황에서 개발자가 즉시 오류를 인식하고, 이를 디버깅하고 해결할 수 있도록 돕습니다. 디버깅 중에는 오류 메시지와 함께 프로그램 실행을 중단하고, 적절한 조치를 취할 수 있게 됩니다.

