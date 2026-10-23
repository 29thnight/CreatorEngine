namespace CreatorEngine.Scripts

open System
open System.Collections.Generic
open CreatorEngine

type Spawner() as this =
    inherit Component()

    let spawned = Queue<Entity>()
    let mutable prefab = Unchecked.defaultof<Prefab>
    let mutable loadedName: string = null
    let mutable timer = 0.0f
    let mutable spawnCount = 0

    do
        this._prefabName <- ""
        this._interval <- 1.0f
        this._maxAlive <- 5
        this._spawnOffset <- Float3(0.0f, 0.0f, 2.0f)

    [<DefaultValue; SerializeField>]
    val mutable public _prefabName: string

    [<DefaultValue; SerializeField>]
    val mutable public _interval: float32

    [<DefaultValue; SerializeField>]
    val mutable public _maxAlive: int

    [<DefaultValue; SerializeField>]
    val mutable public _spawnOffset: Float3

    member private this.EnsurePrefab() =
        if loadedName <> this._prefabName then
            loadedName <- this._prefabName
            prefab <- Unchecked.defaultof<Prefab>

            if not (String.IsNullOrEmpty(this._prefabName)) then
                prefab <- Prefab.Load(this._prefabName)

                if prefab.IsValid then
                    Component.Log($"[Spawner] 준비됨 — '{this._prefabName}', {this._interval}초 간격, 최대 {this._maxAlive}개")
                else
                    Component.LogError($"[Spawner] 프리팹을 찾을 수 없습니다: {this._prefabName}")

    member private this.Spawn() =
        let instance = prefab.Instantiate($"{this._prefabName}_{spawnCount}")

        if not instance.IsAlive then
            Component.LogWarning("[Spawner] 인스턴스 생성 실패 — 스폰을 멈춥니다")
            prefab <- Unchecked.defaultof<Prefab>
            loadedName <- this._prefabName
        else
            let transform = instance.Transform

            if not (obj.ReferenceEquals(transform, null)) then
                transform.SetLocalPosition(
                    this.Transform.LocalPosition
                    + this._spawnOffset * (1.0f + float32 spawnCount * 0.1f)
                )

            spawned.Enqueue(instance)
            spawnCount <- spawnCount + 1
            Component.Log($"[Spawner] 스폰 {instance.Name} (총 {spawnCount}, 생존 {spawned.Count})")

    member private this.TrimOldest() =
        while spawned.Count > this._maxAlive do
            let oldest = spawned.Dequeue()

            if oldest.IsAlive then
                Component.Log($"[Spawner] 정리 {oldest.Name}")
                oldest.Destroy()

    member this.PostPhysics(dt: single) : unit =
        this.EnsurePrefab()

        if prefab.IsValid then
            timer <- timer + dt

            if timer >= this._interval then
                timer <- 0.0f
                this.Spawn()
                this.TrimOldest()

    member _.OnUninitializing() : unit =
        while spawned.Count > 0 do
            let owner = spawned.Dequeue()

            if owner.IsAlive then
                owner.Destroy()
