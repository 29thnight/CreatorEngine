namespace CreatorEngine

[<AllowNullLiteral; Sealed>]
type PlayerInputComponent internal (owner) = inherit ExtraNativeComponent(owner, 4)
