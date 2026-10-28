namespace CreatorEngine;

/// <summary>A native component resolved through its owner's generation-checked handle.</summary>
public abstract class ExtraNativeComponent : NativeComponent
{
    private protected abstract int Kind { get; }
    public bool IsPresent => Native.HasExtraComponent(OwnerHandle, Kind);
    public override bool Enabled
    {
        get => Native.ExtraComponentGetEnabled(OwnerHandle, Kind);
        set => Native.ExtraComponentSetEnabled(OwnerHandle, Kind, value);
    }
}

public sealed class BehaviorTreeComponent : ExtraNativeComponent { private protected override int Kind => 0; }
public sealed class BoneComponent : ExtraNativeComponent { private protected override int Kind => 1; }
public sealed class DecalComponent : ExtraNativeComponent { private protected override int Kind => 2; }
public sealed class FoliageComponent : ExtraNativeComponent { private protected override int Kind => 3; }
public sealed class PlayerInputComponent : ExtraNativeComponent { private protected override int Kind => 4; }
public sealed class SceneRenderProfileComponent : ExtraNativeComponent { private protected override int Kind => 5; }
public sealed class SpriteRenderer : ExtraNativeComponent { private protected override int Kind => 6; }
public sealed class SpriteSheetComponent : ExtraNativeComponent { private protected override int Kind => 7; }
public sealed class StateMachineComponent : ExtraNativeComponent { private protected override int Kind => 8; }
public sealed class TerrainComponent : ExtraNativeComponent { private protected override int Kind => 9; }
