namespace CreatorEngine.Scripts;

/// <summary>
/// 입력 바인딩 검증.
///
/// 실제 키 입력은 무인 실행에서 넣을 수 없으므로, 자동으로 확인할 수 있는 것만 단정한다 —
/// 아무도 누르지 않은 상태에서 모든 술어가 false인지, 호출 경로가 죽지 않는지,
/// 마우스·패드 질의가 그럴듯한 값을 주는지.
/// 사람이 직접 눌러 볼 때를 위해 상태 변화를 로그로 남기는 모드도 둔다.
/// </summary>
public sealed partial class InputProbe : Behaviour
{
    /// <summary>켜면 매 프레임 입력 변화를 로그로 남긴다. 손으로 확인할 때 쓴다.</summary>
    [SerializeField] private bool _watch;

    /// <summary>몇 프레임 뒤에 자동 검사를 돌릴지.</summary>
    [SerializeField] private int _checkAfterFrames = 30;

    private int _frame;
    private bool _checked;
    private int _passed;
    private int _failed;

    public override void PostPhysics(float tick)
    {
        if (_watch) Watch();

        if (_checked || ++_frame < _checkAfterFrames) return;
        _checked = true;

        CheckIdleState();
        CheckMouse();
        CheckGamepad();

        if (_failed == 0) Log($"[InputProbe] 전체 통과 ({_passed}건)");
        else LogError($"[InputProbe] {_failed}건 실패 / {_passed}건 통과");
    }

    /// <summary>아무 입력이 없으면 세 술어가 모두 false여야 한다.</summary>
    private void CheckIdleState()
    {
        // 대표 키 몇 개만 본다. 전부 도는 건 의미 없이 느리기만 하다.
        foreach (KeyCode key in new[] { KeyCode.A, KeyCode.Space, KeyCode.Escape, KeyCode.F1, KeyCode.Alpha1 })
        {
            bool any = Input.GetKeyDown(key) || Input.GetKey(key) || Input.GetKeyUp(key);
            Assert($"{key}: 입력 없음", !any,
                $"Down={Input.GetKeyDown(key)} Key={Input.GetKey(key)} Up={Input.GetKeyUp(key)}");
        }

        Assert("AnyKey = false", !Input.AnyKey, "true가 나왔습니다");
    }

    private void CheckMouse()
    {
        Float2 position = Input.MousePosition;
        Log($"[InputProbe] 마우스 위치 {position} 델타 {Input.MouseDelta} 휠 {Input.WheelDelta}");

        // 창 밖으로 나가 있을 수 있어 범위는 단정하지 않는다. NaN만 걸러 낸다.
        Assert("마우스 위치가 유효한 수", !float.IsNaN(position.X) && !float.IsNaN(position.Y), $"{position}");

        foreach (MouseButton button in new[] { MouseButton.Left, MouseButton.Right, MouseButton.Middle })
        {
            bool any = Input.GetMouseButtonDown(button) || Input.GetMouseButton(button) || Input.GetMouseButtonUp(button);
            Assert($"마우스 {button}: 입력 없음", !any, "눌린 것으로 읽힘");
        }
    }

    private void CheckGamepad()
    {
        // 패드가 없어도 죽지 않아야 하고, 없으면 전부 false·0이어야 한다.
        for (int index = 0; index < 2; ++index)
        {
            bool connected = Input.IsGamepadConnected(index);
            Log($"[InputProbe] 패드 {index} 연결={connected} " +
                $"L스틱={Input.GetThumbLeft(index)} R스틱={Input.GetThumbRight(index)}");

            if (connected) continue;

            Assert($"패드 {index}: 미연결 시 버튼 false",
                !Input.GetButton(index, GamepadButton.A) && !Input.GetButtonDown(index, GamepadButton.Start),
                "눌린 것으로 읽힘");
        }

        // 음수 인덱스로도 죽지 않아야 한다.
        Assert("잘못된 패드 인덱스 안전", !Input.GetButton(-1, GamepadButton.A), "true가 나왔습니다");
    }

    /// <summary>손으로 눌러 확인할 때 쓰는 모드. 상태가 바뀔 때만 찍는다.</summary>
    private void Watch()
    {
        foreach (KeyCode key in new[] { KeyCode.W, KeyCode.A, KeyCode.S, KeyCode.D, KeyCode.Space })
        {
            if (Input.GetKeyDown(key)) Log($"[InputProbe] {key} Down (frame {FrameCount})");
            if (Input.GetKeyUp(key)) Log($"[InputProbe] {key} Up (frame {FrameCount})");
        }

        if (Input.GetMouseButtonDown(MouseButton.Left)) Log($"[InputProbe] 마우스 좌클릭 @ {Input.MousePosition}");
    }

    private void Assert(string name, bool ok, string detail)
    {
        if (ok) { ++_passed; return; }

        ++_failed;
        LogError($"[InputProbe] 실패: {name} — {detail}");
    }
}
