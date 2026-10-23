namespace CreatorEngine.Scripts

open CreatorEngine

type LightScriptProbe() =
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
            Component.LogError($"[LightScriptProbe] 실패: {name} — {detail}")

    member private this.CheckAuthoredValues(light: LightComponent) =
        let near = LightScriptProbe.Near

        this.Assert("저작 intensity=1", near (light.Intensity, 1.0f), string light.Intensity)
        this.Assert("저작 range=10", near (light.Range, 10.0f), string light.Range)
        this.Assert("저작 spotAngle=30", near (light.SpotAngle, 30.0f), string light.SpotAngle)
        this.Assert("저작 type=Directional", light.Type = LightType.Directional, string light.Type)
        this.Assert("저작 status=StaticShadows", light.Status = LightStatus.StaticShadows, string light.Status)

        let color = light.Color

        let isWhite =
            near (color.R, 1.0f)
            && near (color.G, 1.0f)
            && near (color.B, 1.0f)
            && near (color.A, 1.0f)

        this.Assert("저작 color=(1,1,1,1)", isWhite, string color)

    member private this.CheckRoundTrip(light: LightComponent) =
        let near = LightScriptProbe.Near

        light.Intensity <- 4.25f
        this.Assert("intensity 왕복", near (light.Intensity, 4.25f), string light.Intensity)

        light.Range <- 33.5f
        this.Assert("range 왕복", near (light.Range, 33.5f), string light.Range)

        light.SpotAngle <- 47.5f
        this.Assert("spotAngle 왕복", near (light.SpotAngle, 47.5f), string light.SpotAngle)

        light.Color <- Color4(0.25f, 0.5f, 0.75f, 1.0f)
        let color = light.Color

        let matchesWrittenColor =
            near (color.R, 0.25f) && near (color.G, 0.5f) && near (color.B, 0.75f)

        this.Assert("color 왕복", matchesWrittenColor, string color)

        light.Type <- LightType.Spot
        this.Assert("type 왕복", light.Type = LightType.Spot, string light.Type)

        light.Status <- LightStatus.Enabled
        this.Assert("status 왕복", light.Status = LightStatus.Enabled, string light.Status)

        // 외부 회귀가 프록시에서 찾는 값이므로 검사 후에도 남겨 둔다.
        light.Intensity <- 4.25f
        light.Type <- LightType.Spot

    member private this.CheckEnumGuards(light: LightComponent) =
        let previousType = light.Type
        light.Type <- enum<LightType> 99
        this.Assert("범위 밖 type은 무시", (light.Type = previousType), string light.Type)

        let previousStatus = light.Status
        light.Status <- enum<LightStatus> -1
        this.Assert("범위 밖 status는 무시", (light.Status = previousStatus), string light.Status)

    member private this.CheckAbsence() =
        let cameraOwner = Entity.Find("Main Camera")

        if not cameraOwner.IsAlive then
            Component.LogError("[LightScriptProbe] 'Main Camera'를 찾지 못했다 — 씬 구성이 바뀌었다")
            failed <- failed + 1
        else
            let hasNoLight =
                obj.ReferenceEquals(cameraOwner.GetComponent<LightComponent>(), null)

            this.Assert($"'{cameraOwner.Name}'에는 LightComponent가 없다", hasNoLight, "찾아졌습니다")

    [<EngineCallable>]
    static member Results() =
        $"{{\"completed\":{completed},\"passed\":{passed},\"failed\":{failed}}}"

    member this.OnInitialized() : unit =
        let light = this.Entity.GetComponent<LightComponent>()

        if obj.ReferenceEquals(light, null) then
            Component.LogError($"[LightScriptProbe] '{this.Entity.Name}'에 LightComponent가 없다 — 대상 오브젝트가 맞는지 확인할 것")
        else
            Component.Log(
                $"[LightScriptProbe] 시작 — color={light.Color} intensity={light.Intensity} range={light.Range} spot={light.SpotAngle} type={light.Type} status={light.Status}"
            )

            this.CheckAuthoredValues(light)
            this.CheckRoundTrip(light)
            this.CheckEnumGuards(light)
            this.CheckAbsence()
            completed <- completed + 1

            if failed = 0 then
                Component.Log($"[LightScriptProbe] 전체 통과 ({passed}건)")
            else
                Component.LogError($"[LightScriptProbe] {failed}건 실패 / {passed}건 통과")
