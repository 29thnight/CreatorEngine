namespace CreatorEngine

open System
open System.Globalization
open System.Reflection
open System.Runtime.CompilerServices
open CreatorEngine

/// CLI-only reflection. Nothing here is cached across collectible generations.
module internal Callable =
    let private convert (text: string) (target: Type) =
        if target = typeof<string> then
            Some(box text)
        elif target = typeof<bool> then
            match Boolean.TryParse(text) with
            | true, value -> Some(box value)
            | _ when text = "1" -> Some(box true)
            | _ when text = "0" -> Some(box false)
            | _ -> None
        elif target = typeof<int> then
            match Int32.TryParse(text, NumberStyles.Integer, CultureInfo.InvariantCulture) with
            | true, value -> Some(box value)
            | _ -> None
        elif target = typeof<int64> then
            match Int64.TryParse(text, NumberStyles.Integer, CultureInfo.InvariantCulture) with
            | true, value -> Some(box value)
            | _ -> None
        elif target = typeof<single> then
            match Single.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture) with
            | true, value -> Some(box value)
            | _ -> None
        elif target = typeof<double> then
            match Double.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture) with
            | true, value -> Some(box value)
            | _ -> None
        else
            None

    let private format (value: obj) =
        match value with
        | null -> "null"
        | :? bool as value -> if value then "true" else "false"
        | :? single as value -> value.ToString("R", CultureInfo.InvariantCulture)
        | :? double as value -> value.ToString("R", CultureInfo.InvariantCulture)
        | :? IFormattable as value -> value.ToString(null, CultureInfo.InvariantCulture)
        | value -> value.ToString()

    let private resolveType (assemblies: Assembly array) name =
        let exact = ResizeArray<Type>()
        let shortNames = ResizeArray<Type>()

        for assembly in assemblies do
            let types =
                try
                    assembly.GetTypes()
                with :? ReflectionTypeLoadException as error ->
                    error.Types

            for candidate in types do
                if not (isNull candidate) then
                    if candidate.FullName = name then
                        exact.Add(candidate)
                    elif candidate.Name = name then
                        shortNames.Add(candidate)

        let matches = if exact.Count > 0 then exact else shortNames

        if matches.Count = 0 then
            Error(InvokeOutcome.TypeNotFound, $"Type not found: {name}")
        elif matches.Count > 1 then
            Error(InvokeOutcome.TypeAmbiguous, $"Ambiguous type name: {name}; use its full name.")
        else
            Ok(matches[0])

    [<MethodImpl(MethodImplOptions.NoInlining)>]
    let invoke (assemblies: Assembly array) (typeName: string) (methodName: string) (arguments: string array) =
        let result, payload =
            if assemblies.Length = 0 then
                InvokeOutcome.NoAssembly, "No script assembly is loaded."
            else
                match resolveType assemblies typeName with
                | Error error -> error
                | Ok scriptType ->
                    let named =
                        scriptType.GetMethods(BindingFlags.Public ||| BindingFlags.NonPublic ||| BindingFlags.Static)
                        |> Array.filter (fun method -> method.Name = methodName)

                    let byArity =
                        named
                        |> Array.filter (fun method -> method.GetParameters().Length = arguments.Length)

                    if named.Length = 0 then
                        InvokeOutcome.MethodNotFound, $"Static method not found: {methodName}"
                    elif byArity.Length = 0 then
                        InvokeOutcome.ArgumentMismatch, "Argument count does not match."
                    elif byArity.Length > 1 then
                        InvokeOutcome.MethodAmbiguous, "Multiple overloads have the same argument count."
                    else
                        let method = byArity[0]

                        if not (method.IsDefined(typeof<EngineCallableAttribute>, false)) then
                            InvokeOutcome.NotMarked, $"{methodName} requires [EngineCallable]."
                        else
                            let parameters = method.GetParameters()
                            let converted = Array.zeroCreate<obj> parameters.Length
                            let mutable mismatch = -1

                            for index in 0 .. parameters.Length - 1 do
                                match convert arguments[index] parameters[index].ParameterType with
                                | Some value -> converted[index] <- value
                                | None ->
                                    if mismatch < 0 then
                                        mismatch <- index

                            if mismatch >= 0 then
                                InvokeOutcome.ArgumentMismatch,
                                $"Argument {mismatch} cannot be converted to {parameters[mismatch].ParameterType.Name}."
                            else
                                try
                                    let returned = method.Invoke(null, converted)

                                    let text =
                                        if method.ReturnType = typeof<Void> then
                                            ""
                                        else
                                            format returned

                                    InvokeOutcome.Ok, method.ReturnType.Name + "\u001f" + text
                                with
                                | :? TargetInvocationException as error ->
                                    let cause =
                                        if isNull error.InnerException then
                                            error :> exn
                                        else
                                            error.InnerException

                                    InvokeOutcome.Threw, $"{cause.GetType().Name}: {cause.Message}"
                                | error ->
                                    InvokeOutcome.Threw, $"Invocation failed: {error.GetType().Name}: {error.Message}"

        struct (result, payload)
