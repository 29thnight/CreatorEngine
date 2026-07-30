namespace CreatorEngine;

/// <summary>
/// 키보드·마우스·게임패드 입력.
///
/// 이름과 의미는 Unity를 따른다 — <c>GetKeyDown</c>은 누른 첫 프레임,
/// <c>GetKey</c>는 눌려 있는 동안 계속, <c>GetKeyUp</c>은 뗀 프레임이다.
///
/// 엔진의 <c>IsKeyPressed</c>를 그대로 노출하지 않은 이유가 있다. 엔진 상태 전이는
/// Idle → Down → Pressed → Released인데 <b>Pressed가 누른 첫 프레임을 제외</b>한다.
/// 그래서 엔진 술어만 쓰면 "누르고 있는 동안"이 매번 한 프레임씩 비는데, 눈에 잘
/// 띄지 않아 찾기 어려운 종류의 버그다. 여기서는 상태값을 통째로 받아 Down|Pressed로
/// 조합하므로 그 구멍이 없다.
/// </summary>
public static class Input
{
    // ── 키보드 ──

    /// <summary>누른 첫 프레임에만 true.</summary>
    public static bool GetKeyDown(KeyCode key) => Native.GetKeyState((int)key) == KeyState.Down;

    /// <summary>눌려 있는 동안 계속 true(누른 첫 프레임 포함).</summary>
    public static bool GetKey(KeyCode key)
    {
        KeyState state = Native.GetKeyState((int)key);
        return state is KeyState.Down or KeyState.Pressed;
    }

    /// <summary>뗀 프레임에만 true.</summary>
    public static bool GetKeyUp(KeyCode key) => Native.GetKeyState((int)key) == KeyState.Released;

    /// <summary>아무 키나 눌려 있는지.</summary>
    public static bool AnyKey => Native.InputIsAnyKeyPressed();

    // ── 마우스 ──

    public static bool GetMouseButtonDown(MouseButton button)
        => Native.GetMouseButtonState((int)button) == KeyState.Down;

    public static bool GetMouseButton(MouseButton button)
    {
        KeyState state = Native.GetMouseButtonState((int)button);
        return state is KeyState.Down or KeyState.Pressed;
    }

    public static bool GetMouseButtonUp(MouseButton button)
        => Native.GetMouseButtonState((int)button) == KeyState.Released;

    /// <summary>화면 좌표(픽셀). 좌상단이 원점이다.</summary>
    public static Float2 MousePosition => Native.InputGetMousePosition();

    /// <summary>이번 프레임의 마우스 이동량.</summary>
    public static Float2 MouseDelta => Native.InputGetMouseDelta();

    /// <summary>휠 회전량. 위로 굴리면 양수다.</summary>
    public static int WheelDelta => Native.InputGetWheelDelta();

    public static void SetCursorVisible(bool visible) => Native.InputSetCursorVisible(visible);

    // ── 게임패드 ──
    //
    // index는 패드 번호다(0부터). 로컬 협동을 쓰는 곳이 있어 항상 명시하게 두었다.

    public static bool IsGamepadConnected(int index) => Native.InputIsControllerConnected(index);

    public static bool GetButtonDown(int index, GamepadButton button)
        => Native.GetControllerButtonState(index, (int)button) == KeyState.Down;

    public static bool GetButton(int index, GamepadButton button)
    {
        KeyState state = Native.GetControllerButtonState(index, (int)button);
        return state is KeyState.Down or KeyState.Pressed;
    }

    public static bool GetButtonUp(int index, GamepadButton button)
        => Native.GetControllerButtonState(index, (int)button) == KeyState.Released;

    /// <summary>왼쪽 트리거가 임계값을 넘었는지. 엔진이 아날로그 값을 노출하지 않아 불리언이다.</summary>
    public static bool GetTriggerLeft(int index) => Native.InputIsControllerTriggerL(index);

    public static bool GetTriggerRight(int index) => Native.InputIsControllerTriggerR(index);

    /// <summary>왼쪽 스틱. 데드존은 엔진이 적용한 뒤 넘어온다.</summary>
    public static Float2 GetThumbLeft(int index) => Native.InputGetControllerThumbL(index);

    public static Float2 GetThumbRight(int index) => Native.InputGetControllerThumbR(index);
}
