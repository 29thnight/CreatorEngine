namespace CreatorEngine.Scripts

open System
open CreatorEngine

type TransformProbe() =
    inherit Component()

    let mutable passed = 0
    let mutable failed = 0

    static member private NearPosition(actual: Float3, expected: Float3) = (actual - expected).Length < 1e-3f

    static member private NearRotation(actual: Quaternion, expected: Quaternion) =
        // q와 -q는 같은 회전을 나타내므로 내적의 절댓값으로 비교한다.
        let dot =
            actual.X * expected.X
            + actual.Y * expected.Y
            + actual.Z * expected.Z
            + actual.W * expected.W

        abs (abs dot - 1.0f) < 1e-3f

    member private _.Assert(name, condition, detail) =
        if condition then
            passed <- passed + 1
        else
            failed <- failed + 1
            Component.LogError($"[TransformProbe] 실패: {name} — {detail}")

    member private this.CheckScale() =
        let expectedScale = Float3(2.0f, 3.0f, 4.0f)
        this.Transform.LocalScale <- expectedScale

        this.Assert(
            "LocalScale 왕복",
            TransformProbe.NearPosition(this.Transform.LocalScale, expectedScale),
            string this.Transform.LocalScale
        )

        this.Transform.LocalScale <- Float3.One

    member private this.CheckRotation() =
        let yaw90 = Quaternion.CreateFromAxisAngle(Float3.Up, MathF.PI * 0.5f)
        this.Transform.LocalRotation <- yaw90

        this.Assert(
            "LocalRotation 왕복",
            TransformProbe.NearRotation(this.Transform.LocalRotation, yaw90),
            $"{this.Transform.LocalRotation} vs {yaw90}"
        )

        this.Transform.LocalRotation <- Quaternion.Identity

    member private this.CheckCompose() =
        let yaw45 = Quaternion.CreateFromAxisAngle(Float3.Up, MathF.PI * 0.25f)
        let yaw90 = Quaternion.CreateFromAxisAngle(Float3.Up, MathF.PI * 0.5f)

        this.Transform.LocalRotation <- Quaternion.Identity
        this.Transform.Rotate(yaw45)
        this.Transform.Rotate(yaw45)

        this.Assert(
            "Rotate 합성 45+45=90",
            TransformProbe.NearRotation(this.Transform.LocalRotation, yaw90),
            $"{this.Transform.LocalRotation} vs {yaw90}"
        )

        this.Transform.LocalRotation <- Quaternion.Identity

    member private this.CheckTranslate() =
        let originalPosition = this.Transform.LocalPosition
        let displacement = Float3(1.0f, 2.0f, 3.0f)
        this.Transform.Translate(displacement)

        this.Assert(
            "Translate 누적",
            TransformProbe.NearPosition(this.Transform.LocalPosition, originalPosition + displacement),
            $"{originalPosition} -> {this.Transform.LocalPosition}"
        )

        this.Transform.LocalPosition <- originalPosition

    member private this.CheckWorld() =
        let originalPosition = this.Transform.WorldPosition
        let targetPosition = originalPosition + Float3(5.0f, 0.0f, 0.0f)

        this.Transform.WorldPosition <- targetPosition

        this.Assert(
            "WorldPosition 왕복",
            TransformProbe.NearPosition(this.Transform.WorldPosition, targetPosition),
            $"{this.Transform.WorldPosition} vs {targetPosition}"
        )

        this.Transform.WorldPosition <- originalPosition

        let originalRotation = this.Transform.WorldRotation
        let targetRotation = Quaternion.CreateFromAxisAngle(Float3.Up, MathF.PI * 0.5f)

        this.Transform.WorldRotation <- targetRotation

        this.Assert(
            "WorldRotation 왕복",
            TransformProbe.NearRotation(this.Transform.WorldRotation, targetRotation),
            $"{this.Transform.WorldRotation} vs {targetRotation}"
        )

        this.Transform.WorldRotation <- originalRotation

        let originalScale = this.Transform.WorldScale
        let targetScale = originalScale * 2.0f

        this.Transform.WorldScale <- targetScale

        this.Assert(
            "WorldScale 왕복",
            TransformProbe.NearPosition(this.Transform.WorldScale, targetScale),
            $"{this.Transform.WorldScale} vs {targetScale}"
        )

        this.Transform.WorldScale <- originalScale

    member private this.ReportAxes() =
        this.Transform.LocalRotation <- Quaternion.Identity

        Component.Log(
            $"[TransformProbe] 무회전 축 — forward={this.Transform.Forward} right={this.Transform.Right} up={this.Transform.Up}"
        )

        this.Transform.LocalRotation <- Quaternion.CreateFromAxisAngle(Float3.Up, MathF.PI * 0.5f)

        Component.Log(
            $"[TransformProbe] Yaw 90° 축 — forward={this.Transform.Forward} right={this.Transform.Right} up={this.Transform.Up}"
        )

        this.Transform.LocalRotation <- Quaternion.Identity

    member this.OnInitialized() : unit =
        Component.Log(
            $"[TransformProbe] 시작 — pos={this.Transform.LocalPosition} rot={this.Transform.LocalRotation} scale={this.Transform.LocalScale}"
        )

        this.CheckScale()
        this.CheckRotation()
        this.CheckCompose()
        this.CheckTranslate()
        this.CheckWorld()
        this.ReportAxes()

        if failed = 0 then
            Component.Log($"[TransformProbe] 전체 통과 ({passed}건)")
        else
            Component.LogError($"[TransformProbe] {failed}건 실패 / {passed}건 통과")
