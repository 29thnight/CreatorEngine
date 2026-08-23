# `TimeSystem` 클래스

`TimeSystem` 클래스는 고정 및 가변 타임스텝 기반의 시간 관리를 지원하는 시스템입니다. 이 클래스는 프레임 간 시간 차이를 계산하고, 총 경과 시간 및 초당 프레임 수(FPS)를 추적하며, 타임스텝을 설정할 수 있는 기능을 제공합니다.

## 생성자

### `TimeSystem()`

`TimeSystem` 객체를 생성합니다. `QueryPerformanceFrequency`와 `QueryPerformanceCounter`를 사용하여 하드웨어 타이머를 초기화하고, 타이머와 관련된 변수를 설정합니다.

## 메서드

### `uint64_t GetElapsedTicks() const`

- **설명**: 이전 프레임과 현재 프레임 사이의 경과 시간을 "틱" 단위로 반환합니다.
  
### `double GetElapsedSeconds() const`

- **설명**: 이전 프레임과 현재 프레임 사이의 경과 시간을 초 단위로 반환합니다. (틱을 초로 변환)

### `uint64_t GetTotalTicks() const`

- **설명**: 프로그램 시작부터 지금까지의 총 시간을 "틱" 단위로 반환합니다.
  
### `double GetTotalSeconds() const`

- **설명**: 프로그램 시작부터 지금까지의 총 시간을 초 단위로 반환합니다.

### `uint32_t GetFrameCount() const`

- **설명**: 현재까지 업데이트된 프레임 수를 반환합니다.

### `uint32_t GetFramesPerSecond() const`

- **설명**: 현재 초당 프레임 수(FPS)를 반환합니다.

### `void SetFixedTimeStep(bool isFixedTimestep)`

- **설명**: 고정 타임스텝 모드로 설정할지, 가변 타임스텝 모드로 설정할지 선택합니다.
  
### `void SetTargetElapsedTicks(uint64_t targetElapsed)`

- **설명**: 고정 타임스텝 모드에서 목표 경과 시간을 "틱" 단위로 설정합니다.
  
### `void SetTargetElapsedSeconds(double targetElapsed)`

- **설명**: 고정 타임스텝 모드에서 목표 경과 시간을 초 단위로 설정합니다.

### `void ResetElapsedTime()`

- **설명**: 타이머를 초기화하고, 시간과 FPS 카운터를 재설정합니다.

### `template<typename TUpdate> void Tick(const TUpdate& update)`

- **설명**: 지정된 `update` 함수가 일정 횟수로 호출되어, 타이머 상태를 업데이트합니다. 고정 타임스텝 또는 가변 타임스텝 모드에 따라 경과 시간을 계산하고, 이를 바탕으로 업데이트를 실행합니다.

## 상수

### `static const uint64 TicksPerSecond`

- **설명**: 초당 10,000,000 "틱" 단위를 사용하여 시간을 나타냅니다.

## 유틸리티 함수

### `static double TicksToSeconds(uint64 ticks)`

- **설명**: "틱" 단위를 초 단위로 변환합니다.

### `static uint64 SecondsToTicks(double seconds)`

- **설명**: 초 단위를 "틱" 단위로 변환합니다.

## 내부 데이터

- `LARGE_INTEGER m_qpcFrequency`: 시스템의 타이머 주파수를 저장합니다.
- `LARGE_INTEGER m_qpcLastTime`: 마지막으로 측정된 시간 값을 저장합니다.
- `uint64 m_qpcMaxDelta`: 허용된 최대 시간 델타(일시 중지 후의 타이밍 재개에서 발생할 수 있는 큰 시간 차이를 처리하기 위한 값)
- `uint64 m_elapsedTicks`: 최근 프레임과 현재 프레임 사이의 경과 시간을 "틱" 단위로 저장합니다.
- `uint64 m_totalTicks`: 프로그램 시작부터 현재까지의 총 시간을 "틱" 단위로 저장합니다.
- `uint64 m_leftOverTicks`: 남은 시간 델타 값을 저장합니다.
- `uint32 m_frameCount`: 총 프레임 수를 추적합니다.
- `uint32 m_framesPerSecond`: 초당 프레임 수(FPS)를 추적합니다.
- `uint32 m_framesThisSecond`: 현재 초 내의 프레임 수를 추적합니다.
- `uint64 m_qpcSecondCounter`: 초당 프레임 수를 추적하기 위한 카운터입니다.
- `bool m_isFixedTimeStep`: 고정 타임스텝 모드 여부를 나타냅니다.
- `uint64 m_targetElapsedTicks`: 고정 타임스텝 모드에서의 목표 경과 시간(틱 단위)입니다.

## 사용 예시

```cpp
DirectX11::TimeSystem timeSystem;

// 고정 타임스텝 설정
timeSystem.SetFixedTimeStep(true);

// 목표 경과 시간 설정 (초 단위)
timeSystem.SetTargetElapsedSeconds(1.0 / 60.0);

// 매 프레임마다 Tick 호출
timeSystem.Tick([](){
    // 게임 업데이트 로직
});
