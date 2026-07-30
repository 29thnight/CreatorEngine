namespace CreatorEngine;

/// <summary>
/// 키보드 키. 값은 Windows 가상 키 코드이며 엔진 <c>KeyBoard</c> 열거형과 같다.
/// </summary>
public enum KeyCode
{
    // 알파벳
    A = 0x41, B = 0x42, C = 0x43, D = 0x44, E = 0x45, F = 0x46, G = 0x47,
    H = 0x48, I = 0x49, J = 0x4A, K = 0x4B, L = 0x4C, M = 0x4D, N = 0x4E,
    O = 0x4F, P = 0x50, Q = 0x51, R = 0x52, S = 0x53, T = 0x54, U = 0x55,
    V = 0x56, W = 0x57, X = 0x58, Y = 0x59, Z = 0x5A,

    // 상단 숫자열. 엔진 열거형에는 없지만 스크립트가 '1' 같은 문자 리터럴로 쓰던 자리다.
    Alpha0 = 0x30, Alpha1 = 0x31, Alpha2 = 0x32, Alpha3 = 0x33, Alpha4 = 0x34,
    Alpha5 = 0x35, Alpha6 = 0x36, Alpha7 = 0x37, Alpha8 = 0x38, Alpha9 = 0x39,

    // 방향
    LeftArrow = 0x25, UpArrow = 0x26, RightArrow = 0x27, DownArrow = 0x28,

    // 편집·제어
    Space = 0x20, Enter = 0x0D, Backspace = 0x08, Tab = 0x09, Escape = 0x1B,
    CapsLock = 0x14, Insert = 0x2D, Delete = 0x2E,
    Home = 0x24, End = 0x23, PageUp = 0x21, PageDown = 0x22,
    NumLock = 0x90, ScrollLock = 0x91,

    LeftControl = 0xA2, RightControl = 0xA3,
    LeftShift = 0xA0, RightShift = 0xA1,
    LeftAlt = 0xA4, RightAlt = 0xA5,

    // 기능
    F1 = 0x70, F2 = 0x71, F3 = 0x72, F4 = 0x73, F5 = 0x74, F6 = 0x75,
    F7 = 0x76, F8 = 0x77, F9 = 0x78, F10 = 0x79, F11 = 0x7A, F12 = 0x7B,

    // 숫자 패드
    Keypad0 = 0x60, Keypad1 = 0x61, Keypad2 = 0x62, Keypad3 = 0x63, Keypad4 = 0x64,
    Keypad5 = 0x65, Keypad6 = 0x66, Keypad7 = 0x67, Keypad8 = 0x68, Keypad9 = 0x69,
}

/// <summary>마우스 버튼. 엔진 <c>MouseKey</c>와 값이 같다.</summary>
public enum MouseButton
{
    Left = 0,
    Right = 1,
    Middle = 2,
}

/// <summary>게임패드 버튼. 엔진 <c>ControllerButton</c>과 값이 같다.</summary>
public enum GamepadButton
{
    A = 0, B = 1, X = 2, Y = 3,
    DPadUp = 4, DPadDown = 5, DPadLeft = 6, DPadRight = 7,
    Start = 8, Back = 9,
    LeftShoulder = 10, RightShoulder = 11,
    LeftThumb = 12, RightThumb = 13,
}

/// <summary>
/// 엔진이 관리하는 키 상태. 네이티브 <c>KeyState</c>와 값이 같아야 한다.
///
/// 전이는 Idle → Down(누른 첫 프레임) → Pressed(계속 누르는 중) → Released(뗀 프레임) → Idle.
/// <b>Pressed는 첫 프레임을 포함하지 않는다</b> — "눌려 있는가"를 물으려면 Down도 함께 봐야 한다.
/// </summary>
internal enum KeyState
{
    Idle = 0,
    Down = 1,
    Pressed = 2,
    Released = 3,
}
