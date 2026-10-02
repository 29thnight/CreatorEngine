using System.Runtime.InteropServices;

namespace CreatorEngine;

public enum PhysicsBodyKind { Static = 0, Kinematic = 1, Dynamic = 2 }
public enum PhysicsForceMode { Force = 0, Impulse = 1, Acceleration = 2, VelocityChange = 3 }
public enum PhysicsShapeKind { Box = 0, Sphere = 1, Capsule = 2, Convex = 3, TriangleMesh = 4, Heightfield = 5 }

[StructLayout(LayoutKind.Sequential)]
public struct PhysicsBodyState
{
    public PhysicsBodyKind Kind;
    public float Mass;
    public Float3 Position;
    public Quaternion Rotation;
    public Float3 LinearVelocity;
    public Float3 AngularVelocity;
}

[StructLayout(LayoutKind.Sequential)]
public struct PhysicsShapeState
{
    public uint ShapeId;
    public PhysicsShapeKind Kind;
    internal int SensorValue, QueryValue;
    public ulong LayerOverride;
    public float Radius, HalfHeight;
    public Float3 HalfExtent, LocalPosition;
    public Quaternion LocalRotation;

    public readonly bool Sensor => SensorValue != 0;
    public readonly bool QueryEnabled => QueryValue != 0;
}

public sealed class PhysicsBodyComponent : NativeComponent
{
    // Instance identity is captured at lookup; removal/replacement never silently retargets this wrapper.
    internal ulong NativeInstance { get; init; }
    public ulong ComponentId => NativeInstance;

    // Script lifecycle enablement does not control a native body. Use Entity.SetEnabled for now.
    public override bool Enabled
    {
        get => throw new NotSupportedException("Use Entity.SetEnabled to control native body activation.");
        set => throw new NotSupportedException("Use Entity.SetEnabled to control native body activation.");
    }

    public static PhysicsBodyComponent? Find(Entity owner, ulong componentId)
    {
        if (componentId == 0 || Native.BodyRead(owner.Handle, componentId, out _) != PhysicsError.None)
            return null;

        return new PhysicsBodyComponent { OwnerHandle = owner.Handle, NativeInstance = componentId };
    }


    public PhysicsError ReadState(out PhysicsBodyState state)
        => Native.BodyRead(OwnerHandle, NativeInstance, out state);

    public PhysicsError SetVelocity(Float3 linear, Float3 angular = default)
        => Native.BodyVelocity(OwnerHandle, NativeInstance, linear, angular);

    public PhysicsError ApplyForce(Float3 linear, Float3 angular = default, PhysicsForceMode mode = PhysicsForceMode.Force)
        => Native.BodyForce(OwnerHandle, NativeInstance, linear, angular, mode);

    public PhysicsError GetShapeCount(out int count)
        => Native.BodyShapeCount(OwnerHandle, NativeInstance, out count);

    public PhysicsError GetShape(int index, out PhysicsShapeState state)
        => Native.BodyShapeRead(OwnerHandle, NativeInstance, index, out state);

    // Runtime replacement preserves pose and velocities and can invalidate previously returned SDK handles.
    public PhysicsError SetShapeFlags(uint id, bool sensor, bool queryEnabled)
        => Native.BodyShapeFlags(OwnerHandle, NativeInstance, id, sensor, queryEnabled);
}
