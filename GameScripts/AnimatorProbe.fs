namespace CreatorEngine.Scripts

open CreatorEngine

type AnimatorProbe() as this =
    inherit Component()

    let mutable animator = Unchecked.defaultof<Animator>
    let mutable passed = 0
    let mutable failed = 0

    do
        this._boolParameter <- ""
        this._floatParameter <- ""
        this._triggerParameter <- ""

    [<DefaultValue; SerializeField>]
    val mutable public _boolParameter: string

    [<DefaultValue; SerializeField>]
    val mutable public _floatParameter: string

    [<DefaultValue; SerializeField>]
    val mutable public _triggerParameter: string

    member private _.Assert(name, condition, detail) =
        if condition then
            passed <- passed + 1
        else
            failed <- failed + 1
            Component.LogError($"[AnimatorProbe] 실패: {name} — {detail}")

    member private _.HasConfiguredParameter(kind, name: string) =
        if name.Length = 0 then
            Component.Log($"[AnimatorProbe] {kind} 파라미터 미지정 — 왕복 검사 건너뜀")
            false
        elif not (animator.HasParameter(name)) then
            Component.LogWarning($"[AnimatorProbe] 컨트롤러에 '{name}' 파라미터가 없습니다 — 건너뜀")
            false
        else
            true

    member private this.CheckMissingParameter() =
        let missing = "__없는파라미터__"
        this.Assert("없는 파라미터 HasParameter=false", not (animator.HasParameter(missing)), "true가 나왔습니다")

        animator.SetBool(missing, true)
        animator.SetFloat(missing, 1.0f)
        animator.SetTrigger(missing)

        let returnsDefaults =
            not (animator.GetBool(missing)) && animator.GetFloat(missing) = 0.0f

        this.Assert(
            "없는 파라미터 읽기 기본값",
            returnsDefaults,
            $"bool={animator.GetBool(missing)} float={animator.GetFloat(missing)}"
        )

    member private this.CheckBool() =
        if this.HasConfiguredParameter("bool", this._boolParameter) then
            animator.SetBool(this._boolParameter, true)
            this.Assert($"bool '{this._boolParameter}' = true", animator.GetBool(this._boolParameter), "false로 읽힘")

            animator.SetBool(this._boolParameter, false)

            this.Assert(
                $"bool '{this._boolParameter}' = false",
                not (animator.GetBool(this._boolParameter)),
                "true로 읽힘"
            )

    member private this.CheckFloat() =
        if this.HasConfiguredParameter("float", this._floatParameter) then
            animator.SetFloat(this._floatParameter, 2.5f)
            let value = animator.GetFloat(this._floatParameter)
            this.Assert($"float '{this._floatParameter}' 왕복", abs (value - 2.5f) < 1e-4f, string value)

            animator.SetFloat(this._floatParameter, 0.0f)

    member private this.CheckTrigger() =
        if this.HasConfiguredParameter("trigger", this._triggerParameter) then
            animator.SetTrigger(this._triggerParameter)
            this.Assert($"trigger '{this._triggerParameter}' 세움", animator.GetBool(this._triggerParameter), "false로 읽힘")

            animator.ResetTrigger(this._triggerParameter)

            this.Assert(
                $"trigger '{this._triggerParameter}' 되돌림",
                not (animator.GetBool(this._triggerParameter)),
                "true로 읽힘"
            )

    member private this.CheckLayerAndStop() =
        // 반환값이 없는 API이므로 예외 없이 실제 호출되는지만 확인한다.
        animator.SetUseLayer(1, false)
        animator.SetUseLayer(1, true)
        animator.StopAnimation(0.1f)
        this.Assert("SetUseLayer·StopAnimation 호출", true, "")

    member this.OnInitialized() : unit =
        animator <- this.GetComponent<Animator>()

        if obj.ReferenceEquals(animator, null) then
            Component.LogWarning("[AnimatorProbe] Animator가 없습니다 — component.add로 먼저 붙여 주세요.")
        else
            Component.Log("[AnimatorProbe] 래퍼 획득")

            this.CheckMissingParameter()
            this.CheckBool()
            this.CheckFloat()
            this.CheckTrigger()
            this.CheckLayerAndStop()

            if failed = 0 then
                Component.Log($"[AnimatorProbe] 전체 통과 ({passed}건)")
            else
                Component.LogError($"[AnimatorProbe] {failed}건 실패 / {passed}건 통과")
