namespace CreatorEngine

type internal AuxiliaryRuntime() =
    let factories = AuxiliaryFactories()
    let trees = Trees(fun () -> factories.NodeCount)
    member _.Factories = factories
    member _.Trees = trees
    interface IAuxiliaryRegistry with
        member _.RegisterAnimation(name, factory) = factories.RegisterAnimation(name, factory)
        member _.RegisterNode(name, kind, factory) = factories.RegisterNode(name, kind, factory)
        member _.ClearAnimations() = factories.ClearAnimations()
        member _.ClearNodes() = factories.ClearNodes()
        member _.ClearTrees() = trees.Clear()
