namespace CreatorEngine.Scripts

open CreatorEngine

type NativeAnimationProbe() =
    inherit AniBehavior()
    override _.Enter() = ()

type NativeConditionProbe() =
    inherit ConditionNode()
    override _.ConditionCheck(_, board) = board.GetBool("ready")

type NativeAssetProbe() =
    inherit Component()
    [<DefaultValue; SerializeField>]
    val mutable public TextureLink: AssetLink<Texture>

[<AbstractClass; Sealed>]
type NativeConstructorProbe private () =
    static member TextureVariant() = TextureAssetVariant(Role = 1u)

[<AbstractClass; Sealed>]
type NativeCallableProbe private () =
    [<EngineCallable>]
    static member Add(a: int, b: int) = a + b
    static member Log() = Debug.PrintLog(Debug.LogLevel.Info, "native F# log")
    static member Hidden() = 42
