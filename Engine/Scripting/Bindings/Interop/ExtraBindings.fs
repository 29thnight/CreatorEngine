namespace CreatorEngine

open System

[<AbstractClass; Sealed>]
type internal ExtraBindings private () =
    static member private RegisterOne(kind: Type, create: ObjectHandle -> ExtraNativeComponent) =
        Component.RegisterNative(kind, Func<ObjectHandle, Component>(fun owner ->
            let component = create owner
            if component.IsPresent then component :> Component else null))
    static member Register() =
        ExtraBindings.RegisterOne(typeof<BehaviorTreeComponent>, fun h -> BehaviorTreeComponent(h))
        ExtraBindings.RegisterOne(typeof<BoneComponent>, fun h -> BoneComponent(h))
        ExtraBindings.RegisterOne(typeof<DecalComponent>, fun h -> DecalComponent(h))
        ExtraBindings.RegisterOne(typeof<FoliageComponent>, fun h -> FoliageComponent(h))
        ExtraBindings.RegisterOne(typeof<PlayerInputComponent>, fun h -> PlayerInputComponent(h))
        ExtraBindings.RegisterOne(typeof<SceneRenderProfileComponent>, fun h -> SceneRenderProfileComponent(h))
        ExtraBindings.RegisterOne(typeof<SpriteRenderer>, fun h -> SpriteRenderer(h))
        ExtraBindings.RegisterOne(typeof<SpriteSheetComponent>, fun h -> SpriteSheetComponent(h))
        ExtraBindings.RegisterOne(typeof<StateMachineComponent>, fun h -> StateMachineComponent(h))
        ExtraBindings.RegisterOne(typeof<TerrainComponent>, fun h -> TerrainComponent(h))
