namespace CreatorEngine.Scripts

open System
open System.Globalization
open CreatorEngine

[<AbstractClass; Sealed>]
type EngineCallableProbe private () =

    [<EngineCallable>]
    static member Ping() = "pong"

    [<EngineCallable>]
    static member Echo(text: string, count: int, upper: bool, scale: double) =
        let body = String.Concat(Array.create count text)
        let body = if upper then body.ToUpperInvariant() else body
        body + "|" + scale.ToString("R", CultureInfo.InvariantCulture)

    [<EngineCallable>]
    static member Throw() : unit = invalidOp "의도된 프로브 예외"

    [<EngineCallable>]
    static member Touch() = ()
    // Exists but is unmarked: verifies denial rather than a misspelled name.
    static member Unmarked() = "이 값은 밖으로 나가면 안 된다"

    [<EngineCallable>]
    static member UnsupportedParameter(whenValue: DateTime) = whenValue.ToString("O")
