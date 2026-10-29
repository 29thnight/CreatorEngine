namespace CreatorEngine

open System.Runtime.InteropServices
open CreatorEngine.Native

module internal NativeLog =
    let writeAt level (message: string) (file: string) line (memberName: string) =
        match NativeBootstrap.TryGetForLogging() with
        | Some api when not (isNull api.PrintLog) ->
            let pointer = Marshal.StringToCoTaskMemUTF8(if isNull message then "" else message)
            let mutable filePointer = 0n
            let mutable memberPointer = 0n
            try
                filePointer <- Marshal.StringToCoTaskMemUTF8(if isNull file then "" else file)
                memberPointer <- Marshal.StringToCoTaskMemUTF8(if isNull memberName then "" else memberName)
                api.PrintLog.Invoke(level, pointer, filePointer, line, memberPointer)
            finally
                Marshal.FreeCoTaskMem(memberPointer)
                Marshal.FreeCoTaskMem(filePointer)
                Marshal.FreeCoTaskMem(pointer)
        | _ -> ()
    let write level message = writeAt level message "" 0 ""
