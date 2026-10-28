namespace CreatorEngine.Scripts

open CreatorEngine

type NativeComponentBindingsProbe() =
    inherit Component()

    member this.OnBeginSimulation() =
        let bone = this.GetComponent<BoneComponent>()
        let decal = this.GetComponent<DecalComponent>()
        if isNull decal then
            Component.LogError("[NativeComponentBindingsProbe] missing component")
        else
            if not (isNull bone) then
                bone.Pinned <- true
            decal.SliceX <- 4u
            decal.SliceY <- 5u
            decal.SliceNumber <- 6
            decal.SlicePerSeconds <- 2.5f
            decal.UseAnimation <- true
            decal.IsLoop <- false
            if (isNull bone || bone.Pinned) && decal.SliceX = 4u && decal.SliceY = 5u && decal.SliceNumber = 6 && decal.SlicePerSeconds = 2.5f && decal.UseAnimation && not decal.IsLoop then
                Component.Log("[NativeComponentBindingsProbe] OK")
            else
                Component.LogError("[NativeComponentBindingsProbe] round trip failed")
