namespace CreatorEngine.Scripts

open CreatorEngine

type TutorialUI() as this =
    inherit Component()

    let mutable rect: RectTransformComponent = null
    let mutable image: ImageComponent = null

    do this._screenOffset <- Float2(0.0f, -50.0f)

    [<DefaultValue; SerializeField>]
    val mutable public _screenOffset: Float2

    [<DefaultValue; SerializeField>]
    val mutable public _type: int

    [<DefaultValue; SerializeField>]
    val mutable public _target: Entity

    member this.OnBeginSimulation() =
        if isNull rect then rect <- this.GetComponent<RectTransformComponent>()
        if isNull image then image <- this.GetComponent<ImageComponent>()
        if not (isNull image) then image.SetTexture(this._type)

    member this.PostPhysics(_tick: single) =
        if not this._target.IsAlive || isNull rect || isNull image then
            this.Entity.Destroy()
        elif Camera.Exists then
            let targetTransform = this._target.Transform
            if not (isNull targetTransform) then
                let screen = Camera.WorldToScreenPoint(targetTransform.WorldPosition)
                if screen.Z > 0.0f then
                    rect.ScreenPosition <- Float2(screen.X + this._screenOffset.X, screen.Y + this._screenOffset.Y)

    member this.SetTarget(target: Entity) = this._target <- target

    member this.SetType(value: int) =
        this._type <- value
        if not (isNull image) then image.SetTexture(value)
