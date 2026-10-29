namespace CreatorEngine

open System

[<AbstractClass; Sealed>]
type internal ReflectedComponentBindings private () =
    static member Register() =
        Component.RegisterNative(typeof<ScriptComponent>, Func<ObjectHandle, Component>(fun owner ->
            ScriptComponent.Find(Entity(owner)) :> Component))
        Component.RegisterNative(typeof<InvalidScriptComponent>, Func<ObjectHandle, Component>(fun owner ->
            InvalidScriptComponent.Find(Entity(owner)) :> Component))
