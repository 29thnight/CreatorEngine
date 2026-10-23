namespace CreatorEngine.Scripts

open CreatorEngine

/// 기본 씬의 Main Camera에서 값 왕복과 전역 카메라 선택을 함께 확인한다.
type CameraScriptProbe() =
    inherit Component()

    static let mutable completed = 0
    static let mutable passed = 0
    static let mutable failed = 0

    static member private Near(actual: float32, expected: float32) = abs (actual - expected) < 1e-4f

    member private _.Assert(name, condition, detail) =
        if condition then
            passed <- passed + 1
        else
            failed <- failed + 1
            Component.LogError($"[CameraScriptProbe] 실패: {name} — {detail}")

    member private this.CheckDefaults(camera: CameraComponent) =
        let near = CameraScriptProbe.Near

        this.Assert("기본 fov=60", near (camera.FieldOfView, 60.0f), string camera.FieldOfView)
        this.Assert("기본 near=0.1", near (camera.NearPlane, 0.1f), string camera.NearPlane)
        this.Assert("기본 far=500", near (camera.FarPlane, 500.0f), string camera.FarPlane)
        this.Assert("기본 씬 카메라는 primary", camera.IsPrimary, "false")

    member private this.CheckRoundTrip(camera: CameraComponent) =
        let near = CameraScriptProbe.Near

        camera.FieldOfView <- 42.5f
        this.Assert("fov 왕복", near (camera.FieldOfView, 42.5f), string camera.FieldOfView)

        camera.NearPlane <- 0.25f
        this.Assert("near 왕복", near (camera.NearPlane, 0.25f), string camera.NearPlane)

        camera.FarPlane <- 1234.5f
        this.Assert("far 왕복", near (camera.FarPlane, 1234.5f), string camera.FarPlane)

    member private this.CheckPrimary(camera: CameraComponent) =
        camera.IsPrimary <- false
        this.Assert("primary 끄기 왕복", not camera.IsPrimary, "true")

        // 카메라가 하나면 primary를 꺼도 같은 카메라로 폴백해야 한다.
        let fallback = Camera.Main

        let selectsOwner =
            not (obj.ReferenceEquals(fallback, null)) && fallback.Entity = this.Entity

        this.Assert("꺼도 Camera.Main은 폴백으로 이 카메라", selectsOwner, string fallback)

        camera.IsPrimary <- true
        this.Assert("primary 켜기 왕복", camera.IsPrimary, "false")

    member private this.CheckGlobalAccess() =
        this.Assert("Camera.Exists", Camera.Exists, "false")

        let mainCamera = Camera.Main
        this.Assert("Camera.Main이 있다", not (obj.ReferenceEquals(mainCamera, null)), "null")

        if not (obj.ReferenceEquals(mainCamera, null)) then
            this.Assert("Camera.Main이 이 오브젝트", mainCamera.Entity = this.Entity, mainCamera.Entity.Name)

            let screenSize = Camera.ScreenSize
            this.Assert("ScreenSize가 양수", screenSize.X > 0.0f && screenSize.Y > 0.0f, string screenSize)

    member private this.CheckAbsence() =
        let lightOwner = Entity.Find("Directional Light")

        if not lightOwner.IsAlive then
            Component.LogError("[CameraScriptProbe] 'Directional Light'를 찾지 못했다 — 씬 구성이 바뀌었다")
            failed <- failed + 1
        else
            let hasNoCamera =
                obj.ReferenceEquals(lightOwner.GetComponent<CameraComponent>(), null)

            this.Assert($"'{lightOwner.Name}'에는 CameraComponent가 없다", hasNoCamera, "찾아졌습니다")

    [<EngineCallable>]
    static member Results() =
        $"{{\"completed\":{completed},\"passed\":{passed},\"failed\":{failed}}}"

    member this.OnInitialized() : unit =
        let camera = this.Entity.GetComponent<CameraComponent>()

        if obj.ReferenceEquals(camera, null) then
            Component.LogError($"[CameraScriptProbe] '{this.Entity.Name}'에 CameraComponent가 없다 — 대상 오브젝트가 맞는지 확인할 것")
        else
            Component.Log(
                $"[CameraScriptProbe] 시작 — fov={camera.FieldOfView} near={camera.NearPlane} far={camera.FarPlane} primary={camera.IsPrimary}"
            )

            this.CheckDefaults(camera)
            this.CheckRoundTrip(camera)
            this.CheckPrimary(camera)
            this.CheckGlobalAccess()
            this.CheckAbsence()
            completed <- completed + 1

            if failed = 0 then
                Component.Log($"[CameraScriptProbe] 전체 통과 ({passed}건)")
            else
                Component.LogError($"[CameraScriptProbe] {failed}건 실패 / {passed}건 통과")
