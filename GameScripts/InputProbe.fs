namespace CreatorEngine.Scripts

open System
open CreatorEngine

type InputProbe() as this =
    inherit Component()

    let mutable frame = 0
    let mutable checkedOnce = false
    let mutable passed = 0
    let mutable failed = 0

    do this._checkAfterFrames <- 30

    [<DefaultValue; SerializeField>]
    val mutable public _watch: bool

    [<DefaultValue; SerializeField>]
    val mutable public _checkAfterFrames: int

    member private _.Assert(name, condition, detail) =
        if condition then
            passed <- passed + 1
        else
            failed <- failed + 1
            Component.LogError($"[InputProbe] 실패: {name} — {detail}")

    member private this.CheckIdleState() =
        for key in [| KeyCode.A; KeyCode.Space; KeyCode.Escape; KeyCode.F1; KeyCode.Alpha1 |] do
            let anyInput = Input.GetKeyDown(key) || Input.GetKey(key) || Input.GetKeyUp(key)

            let detail =
                $"Down={Input.GetKeyDown(key)} Key={Input.GetKey(key)} Up={Input.GetKeyUp(key)}"

            this.Assert($"{key}: 입력 없음", not anyInput, detail)

        this.Assert("AnyKey = false", not Input.AnyKey, "true가 나왔습니다")

    member private this.CheckMouse() =
        let position = Input.MousePosition
        Component.Log($"[InputProbe] 마우스 위치 {position} 델타 {Input.MouseDelta} 휠 {Input.WheelDelta}")

        let positionIsFinite =
            not (Single.IsNaN(position.X)) && not (Single.IsNaN(position.Y))

        this.Assert("마우스 위치가 유효한 수", positionIsFinite, string position)

        for button in [| MouseButton.Left; MouseButton.Right; MouseButton.Middle |] do
            let anyInput =
                Input.GetMouseButtonDown(button)
                || Input.GetMouseButton(button)
                || Input.GetMouseButtonUp(button)

            this.Assert($"마우스 {button}: 입력 없음", not anyInput, "눌린 것으로 읽힘")

    member private this.CheckGamepad() =
        for index in 0..1 do
            let connected = Input.IsGamepadConnected(index)

            Component.Log(
                $"[InputProbe] 패드 {index} 연결={connected} L스틱={Input.GetThumbLeft(index)} R스틱={Input.GetThumbRight(index)}"
            )

            if not connected then
                let noButtons =
                    not (Input.GetButton(index, GamepadButton.A))
                    && not (Input.GetButtonDown(index, GamepadButton.Start))

                this.Assert($"패드 {index}: 미연결 시 버튼 false", noButtons, "눌린 것으로 읽힘")

        this.Assert("잘못된 패드 인덱스 안전", not (Input.GetButton(-1, GamepadButton.A)), "true가 나왔습니다")

    member private _.Watch() =
        for key in [| KeyCode.W; KeyCode.A; KeyCode.S; KeyCode.D; KeyCode.Space |] do
            if Input.GetKeyDown(key) then
                Component.Log($"[InputProbe] {key} Down (frame {Component.FrameCount})")

            if Input.GetKeyUp(key) then
                Component.Log($"[InputProbe] {key} Up (frame {Component.FrameCount})")

        if Input.GetMouseButtonDown(MouseButton.Left) then
            Component.Log($"[InputProbe] 마우스 좌클릭 @ {Input.MousePosition}")

    member this.PostPhysics(_: single) : unit =
        if this._watch then
            this.Watch()

        if not checkedOnce then
            frame <- frame + 1

            if frame >= this._checkAfterFrames then
                checkedOnce <- true
                this.CheckIdleState()
                this.CheckMouse()
                this.CheckGamepad()

                if failed = 0 then
                    Component.Log($"[InputProbe] 전체 통과 ({passed}건)")
                else
                    Component.LogError($"[InputProbe] {failed}건 실패 / {passed}건 통과")
