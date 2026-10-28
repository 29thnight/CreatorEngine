namespace CreatorEngine

[<AllowNullLiteral; Sealed>]
type StateMachineComponent internal (owner) = inherit ExtraNativeComponent(owner, 8)
