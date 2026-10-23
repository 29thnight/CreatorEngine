namespace CreatorEngine.Scripts

open System
open CreatorEngine

type HealthComponent() as this =
    inherit Component()

    let mutable currentHp = 0
    do this._maxHp <- 100

    [<DefaultValue; SerializeField>]
    val mutable public _maxHp: int

    member _.CurrentHp = currentHp
    member this.OnInitialized() : unit = currentHp <- this._maxHp

    member this.TakeDamage(amount: int) =
        currentHp <- max 0 (currentHp - amount)
        Component.Log($"[Health] {this.Entity.Name} 피해 {amount} → 남은 체력 {currentHp}/{this._maxHp}")
