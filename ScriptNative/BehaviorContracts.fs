namespace CreatorEngine

open System
open System.Collections.Generic
open System.Runtime.InteropServices
open System.Runtime.CompilerServices

type BTNodeKind =
    | None = 0
    | Action = 1
    | Condition = 2
    | ConditionDecorator = 3

type internal AniEventKind =
    | Enter = 0
    | Update = 1
    | Exit = 2

[<Struct; StructLayout(LayoutKind.Sequential)>]
type internal AniEvent =
    val mutable InstanceId: int
    val mutable Kind: int
    val mutable DeltaTime: single
    val mutable Owner: ObjectHandle

[<Struct; StructLayout(LayoutKind.Sequential)>]
type internal AITick =
    val mutable InstanceId: int
    val mutable DeltaTime: single

[<Struct; StructLayout(LayoutKind.Sequential)>]
type BTStats =
    val mutable TreeCount: int
    val mutable NodeTypeCount: int
    val mutable TickCount: int64
    val mutable SkippedCount: int64

type NodeStatus =
    | Success = 0
    | Failure = 1
    | Aborted = 2
    | Running = 3

type BehaviorNodeType =
    | Composite = 0
    | Decorator = 1
    | Sequence = 2
    | Selector = 3
    | WeightedSelector = 4
    | Inverter = 5
    | ConditionDecorator = 6
    | Condition = 7
    | Parallel = 8
    | Action = 9

type ParallelPolicy =
    | RequiredAll = 0
    | RequiredOne = 1

[<AllowNullLiteral; AbstractClass>]
type BTNode() =
    member val Name = String.Empty with get, internal set
    member val Entity = Unchecked.defaultof<Entity> with get, internal set
    abstract member Tick: single * BlackBoard -> NodeStatus
    static member Log(message: string, [<CallerFilePath; Optional; DefaultParameterValue("")>] file: string, [<CallerLineNumber; Optional; DefaultParameterValue(0)>] line: int, [<CallerMemberName; Optional; DefaultParameterValue("")>] memberName: string) = NativeLog.writeAt 1 message file line memberName
    static member LogWarning(message: string, [<CallerFilePath; Optional; DefaultParameterValue("")>] file: string, [<CallerLineNumber; Optional; DefaultParameterValue(0)>] line: int, [<CallerMemberName; Optional; DefaultParameterValue("")>] memberName: string) = NativeLog.writeAt 2 message file line memberName
    static member LogError(message: string, [<CallerFilePath; Optional; DefaultParameterValue("")>] file: string, [<CallerLineNumber; Optional; DefaultParameterValue(0)>] line: int, [<CallerMemberName; Optional; DefaultParameterValue("")>] memberName: string) = NativeLog.writeAt 3 message file line memberName

[<AllowNullLiteral; AbstractClass>]
type CompositeNode() =
    inherit BTNode()
    let children = List<BTNode>()
    member _.Children = children :> IReadOnlyList<BTNode>
    member internal _.AddChild(child: BTNode) = children.Add(child)
    member _.ChildList = children

[<AllowNullLiteral; AbstractClass>]
type DecoratorNode() =
    inherit BTNode()
    member val Child: BTNode = null with get, internal set

[<AllowNullLiteral; AbstractClass>]
type ActionNode() =
    inherit BTNode()

[<AllowNullLiteral; AbstractClass>]
type ConditionNode() =
    inherit BTNode()
    abstract member ConditionCheck: single * BlackBoard -> bool
    override this.Tick(dt, board) =
        if this.ConditionCheck(dt, board) then NodeStatus.Success else NodeStatus.Failure

[<AllowNullLiteral; AbstractClass>]
type ConditionDecoratorNode() =
    inherit DecoratorNode()
    abstract member ConditionCheck: single * BlackBoard -> bool
    override this.Tick(dt, board) =
        if isNull this.Child || not (this.ConditionCheck(dt, board)) then
            NodeStatus.Failure
        else
            this.Child.Tick(dt, board)

[<AllowNullLiteral; AbstractClass>]
type AniBehavior() =
    member val Entity = Unchecked.defaultof<Entity> with get, internal set
    member val Transform: Transform = null with get, internal set
    member this.IsAlive = this.Entity.IsAlive
    abstract member Enter: unit -> unit
    default _.Enter() = ()
    abstract member Update: single -> unit
    default _.Update(_) = ()
    abstract member Exit: unit -> unit
    default _.Exit() = ()
    member this.GetComponent<'T when 'T :> Component>() = this.Entity.GetComponent<'T>()
    member this.GetComponentInParent<'T when 'T :> Component>() = this.Entity.GetComponentInParent<'T>()
    member this.GetComponentInParent<'T when 'T :> Component>(includeSelf: bool) = this.Entity.GetComponentInParent<'T>(includeSelf)
    member this.GetComponentInChildren<'T when 'T :> Component>() = this.Entity.GetComponentInChildren<'T>()
    member this.GetComponentInChildren<'T when 'T :> Component>(includeSelf: bool) = this.Entity.GetComponentInChildren<'T>(includeSelf)
    static member FrameCount = Component.FrameCount
    static member Log(message: string, [<CallerFilePath; Optional; DefaultParameterValue("")>] file: string, [<CallerLineNumber; Optional; DefaultParameterValue(0)>] line: int, [<CallerMemberName; Optional; DefaultParameterValue("")>] memberName: string) = NativeLog.writeAt 1 message file line memberName
    static member LogWarning(message: string, [<CallerFilePath; Optional; DefaultParameterValue("")>] file: string, [<CallerLineNumber; Optional; DefaultParameterValue(0)>] line: int, [<CallerMemberName; Optional; DefaultParameterValue("")>] memberName: string) = NativeLog.writeAt 2 message file line memberName
    static member LogError(message: string, [<CallerFilePath; Optional; DefaultParameterValue("")>] file: string, [<CallerLineNumber; Optional; DefaultParameterValue(0)>] line: int, [<CallerMemberName; Optional; DefaultParameterValue("")>] memberName: string) = NativeLog.writeAt 3 message file line memberName

[<Sealed>]
type SequenceNode() =
    inherit CompositeNode()
    let mutable current = 0
    override this.Tick(dt, board) =
        let mutable result = NodeStatus.Success
        let mutable finished = false
        while current < this.ChildList.Count && not finished do
            let status = this.ChildList[current].Tick(dt, board)
            if status = NodeStatus.Running then
                result <- NodeStatus.Running
                finished <- true
            elif status = NodeStatus.Failure then
                current <- 0
                result <- NodeStatus.Failure
                finished <- true
            else
                current <- current + 1
        if not finished then current <- 0
        result

[<Sealed>]
type SelectorNode() =
    inherit CompositeNode()
    let mutable current = 0
    override this.Tick(dt, board) =
        let mutable result = NodeStatus.Failure
        let mutable finished = false
        while current < this.ChildList.Count && not finished do
            let status = this.ChildList[current].Tick(dt, board)
            if status = NodeStatus.Running then
                result <- NodeStatus.Running
                finished <- true
            elif status = NodeStatus.Success then
                current <- 0
                result <- NodeStatus.Success
                finished <- true
            else
                current <- current + 1
        if not finished then current <- 0
        result

[<Sealed>]
type WeightedSelectorNode() =
    inherit CompositeNode()
    let weights = List<single>()
    let mutable running = -1
    member internal _.SetWeights(values: IEnumerable<single>) = weights.Clear(); weights.AddRange(values)
    override this.Tick(dt, board) =
        if running >= 0 && running < this.ChildList.Count then
            let status = this.ChildList[running].Tick(dt, board)
            if status <> NodeStatus.Running then running <- -1
            status
        elif this.ChildList.Count = 0 then NodeStatus.Failure
        else
            let mutable total = 0.0f
            for index = 0 to weights.Count - 1 do total <- total + weights[index]
            if total <= 0.0f then NodeStatus.Failure
            else
                let point = Random.Shared.NextSingle() * total
                let mutable sum = 0.0f
                let mutable index = 0
                let mutable found = false
                let mutable result = NodeStatus.Failure
                while index < this.ChildList.Count && index < weights.Count && not found do
                    sum <- sum + weights[index]
                    if point <= sum then
                        result <- this.ChildList[index].Tick(dt, board)
                        if result = NodeStatus.Running then running <- index
                        found <- true
                    index <- index + 1
                result

[<Sealed>]
type ParallelNode() =
    inherit CompositeNode()
    member val Policy = ParallelPolicy.RequiredAll with get, internal set
    override this.Tick(dt, board) =
        let mutable anyRunning = false
        let mutable index = 0
        let mutable result = NodeStatus.Failure
        let mutable finished = false
        while index < this.ChildList.Count && not finished do
            let status = this.ChildList[index].Tick(dt, board)
            if status = NodeStatus.Running then anyRunning <- true
            if status = NodeStatus.Failure && this.Policy = ParallelPolicy.RequiredAll then
                result <- NodeStatus.Failure
                finished <- true
            elif status = NodeStatus.Success && this.Policy = ParallelPolicy.RequiredOne then
                result <- NodeStatus.Success
                finished <- true
            index <- index + 1
        if finished then result
        elif anyRunning then NodeStatus.Running
        elif this.Policy = ParallelPolicy.RequiredAll then NodeStatus.Success
        else NodeStatus.Failure

[<Sealed>]
type InverterNode() =
    inherit DecoratorNode()
    override this.Tick(dt, board) =
        if isNull this.Child then NodeStatus.Failure
        else
            match this.Child.Tick(dt, board) with
            | NodeStatus.Success -> NodeStatus.Failure
            | NodeStatus.Failure -> NodeStatus.Success
            | status -> status
