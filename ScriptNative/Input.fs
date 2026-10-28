namespace CreatorEngine

open CreatorEngine.Native

[<AbstractClass; Sealed>]
type Input private () =
    static member private Key(key: KeyCode) =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Input_GetKeyState) -> enum<KeyState>(api.Input_GetKeyState.Invoke(int key))
        | _ -> KeyState.Idle
    static member private Mouse(button: MouseButton) =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Input_GetMouseButtonState) -> enum<KeyState>(api.Input_GetMouseButtonState.Invoke(int button))
        | _ -> KeyState.Idle
    static member private Button(index: int, button: GamepadButton) =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Input_GetControllerButtonState) -> enum<KeyState>(api.Input_GetControllerButtonState.Invoke(index, int button))
        | _ -> KeyState.Idle
    static member GetKeyDown(key: KeyCode) = Input.Key(key) = KeyState.Down
    static member GetKey(key: KeyCode) = let state = Input.Key(key) in state = KeyState.Down || state = KeyState.Pressed
    static member GetKeyUp(key: KeyCode) = Input.Key(key) = KeyState.Released
    static member AnyKey =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Input_IsAnyKeyPressed) -> api.Input_IsAnyKeyPressed.Invoke() <> 0
        | _ -> false
    static member GetMouseButtonDown(button: MouseButton) = Input.Mouse(button) = KeyState.Down
    static member GetMouseButton(button: MouseButton) = let state = Input.Mouse(button) in state = KeyState.Down || state = KeyState.Pressed
    static member GetMouseButtonUp(button: MouseButton) = Input.Mouse(button) = KeyState.Released
    static member MousePosition =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Input_GetMousePosition) -> Float2.FromNative(api.Input_GetMousePosition.Invoke())
        | _ -> Float2.Zero
    static member MouseDelta =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Input_GetMouseDelta) -> Float2.FromNative(api.Input_GetMouseDelta.Invoke())
        | _ -> Float2.Zero
    static member WheelDelta =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Input_GetWheelDelta) -> api.Input_GetWheelDelta.Invoke()
        | _ -> 0
    static member SetCursorVisible(visible: bool) =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Input_SetCursorVisible) -> api.Input_SetCursorVisible.Invoke(if visible then 1 else 0)
        | _ -> ()
    static member IsGamepadConnected(index: int) =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Input_IsControllerConnected) -> api.Input_IsControllerConnected.Invoke(index) <> 0
        | _ -> false
    static member GetButtonDown(index: int, button: GamepadButton) = Input.Button(index, button) = KeyState.Down
    static member GetButton(index: int, button: GamepadButton) = let state = Input.Button(index, button) in state = KeyState.Down || state = KeyState.Pressed
    static member GetButtonUp(index: int, button: GamepadButton) = Input.Button(index, button) = KeyState.Released
    static member GetTriggerLeft(index: int) =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Input_IsControllerTriggerL) -> api.Input_IsControllerTriggerL.Invoke(index) <> 0
        | _ -> false
    static member GetTriggerRight(index: int) =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Input_IsControllerTriggerR) -> api.Input_IsControllerTriggerR.Invoke(index) <> 0
        | _ -> false
    static member GetThumbLeft(index: int) =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Input_GetControllerThumbL) -> Float2.FromNative(api.Input_GetControllerThumbL.Invoke(index))
        | _ -> Float2.Zero
    static member GetThumbRight(index: int) =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Input_GetControllerThumbR) -> Float2.FromNative(api.Input_GetControllerThumbR.Invoke(index))
        | _ -> Float2.Zero
