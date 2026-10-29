namespace CreatorEngine

open System

type internal LifecyclePhase =
    | OnInitialized = 0
    | OnAddedToScene = 1
    | OnBeginSimulation = 2
    | OnEndSimulation = 3
    | OnRemovingFromScene = 4
    | OnUninitializing = 5

type internal IContactDispatcher =
    abstract Current: Component with get, set
    abstract Suspend: Component -> unit
    abstract BindRole: ObjectHandle * uint64 * uint32 * ShapeRole * ComponentLifetime -> unit
    abstract Observe: Component * ObjectHandle * ShapeRole * ShapeRole * ContactPhases * int * ContactGrouping -> ContactStream
    abstract Remove: ContactStream -> unit
    abstract Route: NativeContact -> unit
    abstract EndUpdate: unit -> unit
    abstract EndFrame: unit -> unit

type internal IAuxiliaryRegistry =
    abstract RegisterAnimation: string * Func<AniBehavior> -> unit
    abstract RegisterNode: string * int * Func<BTNode> -> unit
    abstract ClearAnimations: unit -> unit
    abstract ClearNodes: unit -> unit
    abstract ClearTrees: unit -> unit
