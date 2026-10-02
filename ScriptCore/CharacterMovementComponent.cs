using System.Runtime.InteropServices;

namespace CreatorEngine;

[Flags]
public enum CharacterCollisionFlags : uint
{
    None = 0, Sides = 1, Above = 2, Below = 4, Simulating = 8, Forced = 16, JumpQueued = 32
}

[StructLayout(LayoutKind.Sequential)]
public struct CharacterMovementState
{
    public Float3 Position, FootPosition, ActualDisplacement, DesiredVelocity;
    public float FallVelocity;
    public CharacterCollisionFlags Flags;
    public ulong Tick;
    public Float3 MovementVelocity;
    public double ForcedRemaining;

    public readonly bool Sides => (Flags & CharacterCollisionFlags.Sides) != 0;
    public readonly bool Above => (Flags & CharacterCollisionFlags.Above) != 0;
    public readonly bool Below => (Flags & CharacterCollisionFlags.Below) != 0;
    public readonly bool Forced => (Flags & CharacterCollisionFlags.Forced) != 0;
    public readonly bool JumpQueued => (Flags & CharacterCollisionFlags.JumpQueued) != 0;
    public readonly bool Simulating => (Flags & CharacterCollisionFlags.Simulating) != 0;
}

public sealed class CharacterMovementComponent : NativeComponent
{
    // Capture identity once; a removed/replaced component never retargets this wrapper.
    internal ulong NativeInstance { get; init; }
    public ulong ComponentId => NativeInstance;

    public override bool Enabled
    {
        get => throw new NotSupportedException("Use Entity.SetEnabled to control native character activation.");
        set => throw new NotSupportedException("Use Entity.SetEnabled to control native character activation.");
    }

    public static CharacterMovementComponent? Find(Entity owner, ulong componentId)
    {
        if (componentId == 0 || Native.CharacterRead(owner.Handle, componentId, out _) != PhysicsError.None)
            return null;

        return new CharacterMovementComponent { OwnerHandle = owner.Handle, NativeInstance = componentId };
    }

    // Collision flags describe the last completed fixed movement, not a persistent ground probe.
    public PhysicsError ReadState(out CharacterMovementState state)
        => Native.CharacterRead(OwnerHandle, NativeInstance, out state);

    // World velocity in metres/second, retained and integrated by the Scene's fixed step.
    public PhysicsError SetDesiredVelocity(Float3 velocity)
        => Native.CharacterVelocity(OwnerHandle, NativeInstance, velocity);

    public PhysicsError Jump() => Native.CharacterJump(OwnerHandle, NativeInstance);

    public PhysicsError ForceVelocity(Float3 velocity, double seconds)
        => Native.CharacterForce(OwnerHandle, NativeInstance, velocity, seconds);

    public PhysicsError CancelForcedVelocity() => Native.CharacterCancelForce(OwnerHandle, NativeInstance);

    // Clears fall/movement velocity, forced movement, queued jump and old flags; desired input is retained.
    public PhysicsError Teleport(Float3 position)
        => Native.CharacterTeleport(OwnerHandle, NativeInstance, position);
}
