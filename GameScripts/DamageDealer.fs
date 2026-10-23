namespace CreatorEngine.Scripts

open CreatorEngine

type DamageDealer() as this =
    inherit Component()

    let mutable ownHealth = Unchecked.defaultof<HealthComponent>
    let mutable timer = 0.0f
    let mutable reported = false

    do
        this._damage <- 7
        this._interval <- 0.5f

    [<DefaultValue; SerializeField>]
    val mutable public _target: Entity

    [<DefaultValue; SerializeField>]
    val mutable public _damage: int

    [<DefaultValue; SerializeField>]
    val mutable public _interval: float32

    member this.OnBeginSimulation() : unit =
        ownHealth <- this.GetComponent<HealthComponent>()

        let status =
            if obj.ReferenceEquals(ownHealth, null) then
                "없음"
            else
                $"있음(체력 {ownHealth.CurrentHp})"

        Component.Log($"[Damage] 자기 HealthComponent: {status}")

    member this.PostPhysics(dt: single) : unit =
        timer <- timer + dt

        if timer >= this._interval then
            timer <- 0.0f

            if not this._target.IsAlive then
                if not reported then
                    reported <- true
                    Component.LogWarning("[Damage] 대상이 지정되지 않았습니다")
            else
                let health = this._target.GetComponent<HealthComponent>()

                if obj.ReferenceEquals(health, null) then
                    if not reported then
                        reported <- true
                        Component.LogWarning($"[Damage] {this._target.Name}에 HealthComponent가 없습니다")
                else
                    reported <- false
                    health.TakeDamage(this._damage)
