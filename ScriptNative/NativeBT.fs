namespace CreatorEngine

open System
open System.Collections.Generic
open System.Runtime.InteropServices

type private NodeDescription =
    { Id: uint64; ParentId: uint64; Kind: int; Policy: int; IsRoot: bool
      Weight: single; ChildOrder: int; Name: string; ScriptName: string }

module internal NativeBT =
    let private text (address: nativeint) capacity =
        let mutable length = 0
        while length < capacity && Marshal.ReadByte(address, length) <> 0uy do length <- length + 1
        if length = 0 then String.Empty else Marshal.PtrToStringUTF8(address, length)

    let private floatAt address offset = BitConverter.Int32BitsToSingle(Marshal.ReadInt32(address, offset))

    let private describe address =
        { Id = uint64 (Marshal.ReadInt64(address, 0))
          ParentId = uint64 (Marshal.ReadInt64(address, 8))
          Kind = Marshal.ReadInt32(address, 16)
          Policy = Marshal.ReadInt32(address, 20)
          IsRoot = Marshal.ReadInt32(address, 24) <> 0
          Weight = floatAt address 32
          ChildOrder = Marshal.ReadInt32(address, 36)
          Name = text (address + 40n) 64
          ScriptName = text (address + 104n) 64 }

    let private createNode (factories: AuxiliaryFactories) description =
        if not (String.IsNullOrEmpty(description.ScriptName)) then
            let node = factories.CreateNode(description.ScriptName)
            if isNull node then invalidOp $"Unregistered script node '{description.ScriptName}'"
            node
        else
            match description.Name, enum<BehaviorNodeType>(description.Kind) with
            | ("RootSequence" | "Sequence"), _ -> SequenceNode() :> BTNode
            | "Selector", _ -> SelectorNode() :> BTNode
            | "WeightedSelector", _ -> WeightedSelectorNode() :> BTNode
            | "Inverter", _ -> InverterNode() :> BTNode
            | _, BehaviorNodeType.Sequence -> SequenceNode() :> BTNode
            | _, BehaviorNodeType.Selector -> SelectorNode() :> BTNode
            | _, BehaviorNodeType.WeightedSelector -> WeightedSelectorNode() :> BTNode
            | _, BehaviorNodeType.Inverter -> InverterNode() :> BTNode
            | _, BehaviorNodeType.Parallel -> ParallelNode(Policy = enum<ParallelPolicy>(description.Policy)) :> BTNode
            | _, kind -> invalidOp $"Cannot construct node '{description.Name}' of type {kind}"

    let build (factories: AuxiliaryFactories) (owner: Entity) (nodes: nativeint) count =
        if nodes = 0n || count <= 0 then invalidArg "nodes" "Behavior tree has no nodes."
        let descriptions = Array.init count (fun index -> describe (nodes + nativeint (index * 168)))
        let built = Dictionary<uint64, BTNode>(count)
        let children = Dictionary<uint64, ResizeArray<NodeDescription>>()
        let mutable root = 0UL
        for description in descriptions do
            let node = createNode factories description
            node.Name <- description.Name
            node.Entity <- owner
            built[description.Id] <- node
            if description.IsRoot then root <- description.Id
            if description.ParentId <> 0UL then
                let items =
                    match children.TryGetValue(description.ParentId) with
                    | true, existing -> existing
                    | _ ->
                        let created = ResizeArray<NodeDescription>()
                        children.Add(description.ParentId, created)
                        created
                items.Add(description)
        if root = 0UL || not (built.ContainsKey(root)) then invalidOp "Behavior tree root is missing."
        for KeyValue(parentId, items) in children do
            let mutable parent = Unchecked.defaultof<BTNode>
            if not (built.TryGetValue(parentId, &parent)) then invalidOp $"Behavior tree parent {parentId} is missing."
            items.Sort(Comparison<NodeDescription>(fun a b -> compare a.ChildOrder b.ChildOrder))
            match parent with
            | :? CompositeNode as composite ->
                let weights = ResizeArray<single>(items.Count)
                for child in items do
                    composite.AddChild(built[child.Id])
                    weights.Add(child.Weight)
                match composite with
                | :? WeightedSelectorNode as weighted -> weighted.SetWeights(weights)
                | _ -> ()
            | :? DecoratorNode as decorator ->
                if items.Count > 1 then invalidOp $"Decorator '{parent.Name}' has multiple children."
                decorator.Child <- built[items[0].Id]
            | _ -> invalidOp $"Node '{parent.Name}' cannot have children."
        built[root]

    let loadBoard (entries: nativeint) count =
        let board = BlackBoard()
        if entries <> 0n && count > 0 then
            for index = 0 to count - 1 do
                let entry = entries + nativeint (index * 224)
                let key = text (entry + 32n) 64
                if key.Length <> 0 then
                    match enum<BlackBoardType>(Marshal.ReadInt32(entry, 0)) with
                    | BlackBoardType.Bool -> board.SetBool(key, Marshal.ReadInt32(entry, 4) <> 0)
                    | BlackBoardType.Int -> board.SetInt(key, Marshal.ReadInt32(entry, 8))
                    | BlackBoardType.Float -> board.SetFloat(key, floatAt entry 12)
                    | BlackBoardType.Vector2 -> board.SetVector2(key, Float2(floatAt entry 16, floatAt entry 20))
                    | BlackBoardType.Vector3 -> board.SetVector3(key, Float3(floatAt entry 16, floatAt entry 20, floatAt entry 24))
                    | BlackBoardType.String | BlackBoardType.Entity | BlackBoardType.Transform -> board.SetString(key, text (entry + 96n) 128)
                    | kind -> NativeLog.write 2 $"[BT] Unsupported blackboard type {kind} for '{key}'."
        board
