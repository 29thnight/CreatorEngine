namespace CreatorEngine.Native

open System
open System.Reflection.Emit
open System.Runtime.CompilerServices
open System.Runtime.InteropServices
open CreatorEngine.Native.reflgen_creator_script_native

type internal EntityAliveCall = delegate of ScriptObjectHandle -> int

module internal FastNativeCalls =
    let entityAlive (entry: ScriptApi_Entity_IsAlive_Delegate) =
        if isNull entry then null, false
        elif not RuntimeFeature.IsDynamicCodeSupported then
            EntityAliveCall(fun handle -> entry.Invoke(handle)), false
        else
            try
                let address = Marshal.GetFunctionPointerForDelegate(entry)
                let method = DynamicMethod("CreatorScript_EntityIsAlive", typeof<int>, [| typeof<ScriptObjectHandle> |], typeof<EntityAliveCall>.Module, true)
                let il = method.GetILGenerator()
                il.Emit(OpCodes.Ldarg_0)
                il.Emit(OpCodes.Ldc_I8, address.ToInt64())
                il.Emit(OpCodes.Conv_I)
                il.EmitCalli(OpCodes.Calli, CallingConvention.StdCall, typeof<int>, [| typeof<ScriptObjectHandle> |])
                il.Emit(OpCodes.Ret)
                let call = method.CreateDelegate(typeof<EntityAliveCall>) :?> EntityAliveCall
                call.Invoke(Unchecked.defaultof<ScriptObjectHandle>) |> ignore
                call, true
            with _ ->
                EntityAliveCall(fun handle -> entry.Invoke(handle)), false
