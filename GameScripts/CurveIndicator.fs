namespace CreatorEngine.Scripts

open System
open System.Collections.Generic
open CreatorEngine

type CurveIndicator() as this =
    inherit Component()
    let indicators = List<Entity>()
    let renderers = List<MeshRenderer>()
    let scales = List<Float3>()
    let mutable start = Float3.Zero
    let mutable finish = Float3.Zero
    let mutable height = 0.0f
    let mutable enabled = false
    do
        this._interval <- 1.0f
        this._minScale <- 0.7f
    [<DefaultValue; SerializeField>]
    val mutable public _interval: single
    [<DefaultValue; SerializeField>]
    val mutable public _minScale: single

    member this.OnBeginSimulation() =
        for child in this.Entity.Children do
            let renderer = child.GetComponent<MeshRenderer>()
            if not (isNull renderer) then
                renderer.InstantiateMaterial("IndicatorMaterial") |> ignore
                let transform = child.Transform
                if not (isNull transform) then
                    indicators.Add(child)
                    renderers.Add(renderer)
                    scales.Add(transform.LocalScale)

    member this.PostPhysics(_tick: single) =
        if enabled then
            let direction = finish - start
            let length = direction.Length
            if length >= 1e-4f then
                let mutable control = start + direction * 0.5f
                control.Y <- control.Y + height
                for i = 0 to indicators.Count - 1 do
                    let indicator = indicators[i]
                    if indicator.IsAlive then
                        indicator.SetEnabled(true)
                        let t = this._interval * single i / length
                        let a = Float3.Lerp(start, control, t)
                        let b = Float3.Lerp(control, finish, t)
                        let transform = indicator.Transform
                        let alpha = MathF.Sin(t)
                        if not (isNull transform) then
                            transform.SetLocalPosition(Float3.Lerp(a, b, t))
                            let scale = Math.Clamp(alpha * 3.0f, this._minScale, 1.0f)
                            transform.SetLocalScale(scales[i] * scale)
                        renderers[i].BaseColor <- renderers[i].BaseColor.WithAlpha(alpha)

    member _.SetIndicator(first: Float3, last: Float3, curveHeight: single) =
        start <- first
        start.Y <- start.Y + 0.5f
        finish <- last
        finish.Y <- finish.Y + 0.5f
        height <- curveHeight

    member _.EnableIndicator(value: bool) =
        enabled <- value
        for indicator in indicators do
            if indicator.IsAlive then indicator.SetEnabled(value)
