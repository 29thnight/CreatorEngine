namespace CreatorEngine.Scripts

open System
open CreatorEngine

type FSharpNativePlayerProbe() =
    inherit Component()

    let mutable request: AssetRequest<Texture> = null
    let mutable completed = false
    let mutable ticks = 0
    let mutable releaseTick = 0
    let mutable width = 0u
    let mutable height = 0u

    member _.OnBeginSimulation() : unit =
        let id = AssetId.Parse("eeadbf9a-d493-4ace-8a78-b5fb9711f1cf")
        request <- AssetDepot.RequestAsync(AssetLink<Texture>(id), TextureAssetVariant(Compress = false))

    member _.OnTickSimulate(_: single) : unit =
        if not completed then
            ticks <- ticks + 1
            if releaseTick = 0 then
                let snapshot = request.Snapshot()
                if snapshot.Status = AssetRequestStatus.Failed || snapshot.Status = AssetRequestStatus.Stale then
                    failwithf "Texture request failed: %A %s" snapshot.Error snapshot.Message
                if snapshot.Status = AssetRequestStatus.Ready then
                    let mutable texture: AssetHandle<Texture> = null
                    if request.TryAcquireResult(&texture) then
                        let descriptor = AssetDepot.Describe(texture)
                        if descriptor.Width = 0u || descriptor.Height = 0u then
                            failwith "Texture descriptor is empty"
                        width <- descriptor.Width
                        height <- descriptor.Height
                        System.Threading.Tasks.Task.Run(Action(fun () -> texture.Dispose())).GetAwaiter().GetResult()
                        if not texture.IsDisposed then
                            failwith "Worker-thread texture release did not complete"
                        request.Dispose()
                        releaseTick <- ticks
            if releaseTick > 0 && ticks >= releaseTick + 2 then
                completed <- true
                Console.WriteLine($"[FSHARP_PLAYER_OK] asset texture={width}x{height} workerRelease=true ticks={ticks} api=47")

    member _.OnEndSimulation() : unit =
        if not (isNull request) && not request.IsDisposed then
            request.Dispose()
