namespace CreatorEngine

open System
open System.Collections.Generic

type BlackBoardType =
    | None = 0
    | Bool = 1
    | Int = 2
    | Float = 3
    | String = 4
    | Vector2 = 5
    | Vector3 = 6
    | Vector4 = 7
    | Entity = 8
    | Transform = 9

[<AllowNullLiteral; Sealed>]
type BlackBoard() =
    let values = Dictionary<string, struct (BlackBoardType * obj)>(StringComparer.Ordinal)
    let read key expected fallback =
        match values.TryGetValue(key) with
        | false, _ ->
            NativeLog.write 2 $"[BlackBoard] '{key}' 키가 없습니다."
            fallback
        | true, struct (actual, value) when actual <> expected ->
            NativeLog.write 2 $"[BlackBoard] '{key}'는 {actual}인데 {expected}로 읽었습니다."
            fallback
        | true, struct (_, value) ->
            match value with
            | :? 'T as typed -> typed
            | _ -> fallback
    member val Name = String.Empty with get, internal set
    member _.HasKey(key: string) = values.ContainsKey(key)
    member _.GetType(key: string) =
        match values.TryGetValue(key) with
        | true, struct (kind, _) -> kind
        | _ -> BlackBoardType.None
    member _.RemoveKey(key: string) = values.Remove(key) |> ignore
    member _.Clear() = values.Clear()
    member _.Keys = values.Keys :> IReadOnlyCollection<string>
    member _.SetBool(key: string, value: bool) = values[key] <- struct (BlackBoardType.Bool, box value)
    member _.SetInt(key: string, value: int) = values[key] <- struct (BlackBoardType.Int, box value)
    member _.SetFloat(key: string, value: single) = values[key] <- struct (BlackBoardType.Float, box value)
    member _.SetString(key: string, value: string) = values[key] <- struct (BlackBoardType.String, box value)
    member _.SetVector2(key: string, value: Float2) = values[key] <- struct (BlackBoardType.Vector2, box value)
    member _.SetVector3(key: string, value: Float3) = values[key] <- struct (BlackBoardType.Vector3, box value)
    member _.SetEntity(key: string, value: Entity) = values[key] <- struct (BlackBoardType.Entity, box value)
    member _.GetBool(key: string) = read key BlackBoardType.Bool false
    member _.GetInt(key: string) = read key BlackBoardType.Int 0
    member _.GetFloat(key: string) = read key BlackBoardType.Float 0.0f
    member _.GetString(key: string) = read key BlackBoardType.String String.Empty
    member _.GetVector2(key: string) = read key BlackBoardType.Vector2 Float2.Zero
    member _.GetVector3(key: string) = read key BlackBoardType.Vector3 Float3.Zero
    member _.GetEntity(key: string) = read key BlackBoardType.Entity Unchecked.defaultof<Entity>
