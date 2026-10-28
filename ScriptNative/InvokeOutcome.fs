namespace CreatorEngine

type internal InvokeOutcome =
    | Ok = 0
    | NoAssembly = -1
    | TypeNotFound = -2
    | TypeAmbiguous = -3
    | MethodNotFound = -4
    | MethodAmbiguous = -5
    | NotMarked = -6
    | ArgumentMismatch = -7
    | Threw = -8
    | Internal = -9
