namespace CreatorEngine

open System.Runtime.CompilerServices
open System.Runtime.InteropServices

module Debug =
    type LogLevel = | Debug = 0 | Info = 1 | Warning = 2 | Error = 3 | Critical = 4

[<AbstractClass; Sealed>]
type Debug private () =
    static member PrintLog(level: Debug.LogLevel, message: string, [<CallerFilePath; Optional; DefaultParameterValue("")>] file: string, [<CallerLineNumber; Optional; DefaultParameterValue(0)>] line: int, [<CallerMemberName; Optional; DefaultParameterValue("")>] memberName: string) = NativeLog.writeAt (int level) message file line memberName
    static member Log(message: string, [<CallerFilePath; Optional; DefaultParameterValue("")>] file: string, [<CallerLineNumber; Optional; DefaultParameterValue(0)>] line: int, [<CallerMemberName; Optional; DefaultParameterValue("")>] memberName: string) = NativeLog.writeAt 1 message file line memberName
    static member LogWarning(message: string, [<CallerFilePath; Optional; DefaultParameterValue("")>] file: string, [<CallerLineNumber; Optional; DefaultParameterValue(0)>] line: int, [<CallerMemberName; Optional; DefaultParameterValue("")>] memberName: string) = NativeLog.writeAt 2 message file line memberName
    static member LogError(message: string, [<CallerFilePath; Optional; DefaultParameterValue("")>] file: string, [<CallerLineNumber; Optional; DefaultParameterValue(0)>] line: int, [<CallerMemberName; Optional; DefaultParameterValue("")>] memberName: string) = NativeLog.writeAt 3 message file line memberName
