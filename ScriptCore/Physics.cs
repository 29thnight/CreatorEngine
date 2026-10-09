using System.Runtime.InteropServices;

namespace CreatorEngine;

public enum PhysicsError
{
    None = 0, InvalidArgument = 1, UnsupportedGeometry = 2, StaleHandle = 3, WrongScene = 4,
    WrongPhase = 5, BackendInitialization = 6, CookingFailed = 7, CapacityExceeded = 8,
    OutOfMemory = 9, LateCommand = 10, DuplicateCommand = 11
}

[StructLayout(LayoutKind.Sequential)]
public struct PhysicsHit
{
    internal ObjectHandle Object;
    public ulong ComponentId;
    public uint ShapeId;
    public uint Face;
    public ulong LayerId;
    public Float3 Point;
    public Float3 Normal;
    public float Distance;
    internal int Location;

    public readonly Entity Entity => new(Object);
    public readonly bool HasLocation => Location != 0;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativePhysicsQueryResult
{
    public int Written, RequiredCapacity, Truncated;
}

public readonly record struct PhysicsQueryResult(PhysicsError Error, int Written, int RequiredCapacity, bool Truncated)
{
    public bool Succeeded => Error == PhysicsError.None;
}

[StructLayout(LayoutKind.Sequential)]
public readonly struct PhysicsQueryRequest
{
    internal readonly int Kind;
    public readonly Float3 Origin, Direction;
    public readonly float DistanceOrRadius;
    public readonly uint Layers;
    internal readonly int Sensors;
    public readonly int Offset, Capacity;

    private PhysicsQueryRequest(int kind, Float3 origin, Float3 direction, float distanceOrRadius,
                                int offset, int capacity, uint layers, bool sensors)
        => (Kind, Origin, Direction, DistanceOrRadius, Offset, Capacity, Layers, Sensors)
            = (kind, origin, direction, distanceOrRadius, offset, capacity, layers, sensors ? 1 : 0);

    public static PhysicsQueryRequest Raycast(Float3 origin, Float3 direction, float distance, int offset, int capacity,
                                             uint layers = Physics.AllLayers, bool includeSensors = false)
        => new(0, origin, direction, distance, offset, capacity, layers, includeSensors);

    public static PhysicsQueryRequest OverlapSphere(Float3 origin, float radius, int offset, int capacity,
                                                   uint layers = Physics.AllLayers, bool includeSensors = false)
        => new(1, origin, default, radius, offset, capacity, layers, includeSensors);
}

[StructLayout(LayoutKind.Sequential)]
public readonly struct PhysicsBatchResult
{
    public readonly PhysicsError Error;
    public readonly int Written, RequiredCapacity;
    private readonly int _truncated;
    public bool Truncated => _truncated != 0;
    public bool Succeeded => Error == PhysicsError.None;
}

public static class Physics
{
    /// <summary>Register during OnBeginSimulation. Conditions are indexed once; Read returns only matching contacts.</summary>
    public static ContactStream ObserveContacts(Entity owner, ShapeRole selfRole, ShapeRole otherRole,
        ContactPhases phases = ContactPhases.Begin, int capacity = 256,
        ContactGrouping grouping = ContactGrouping.ShapePairs)
    {
        var subscriber = ContactRouter.Current ?? throw new InvalidOperationException("ObserveContacts requires OnBeginSimulation.");
        return ContactRouter.Observe(subscriber, owner.Handle, selfRole, otherRole, phases, capacity, grouping);
    }

    /// <summary>Explicit script-authored role binding, scoped to the current simulation. No collision layer changes.</summary>
    public static void BindContactRole(PhysicsBodyComponent body, uint shapeId, ShapeRole role)
    {
        var subscriber = ContactRouter.Current ?? throw new InvalidOperationException("Role binding requires OnBeginSimulation.");
        if (role.Id == Guid.Empty || body.GetShapeCount(out var count) != PhysicsError.None)
            throw new ArgumentException("Invalid body or role.");
        bool found = false;
        for (int i = 0; i < count; ++i)
            if (body.GetShape(i, out var shape) == PhysicsError.None && shape.ShapeId == shapeId)
            {
                if (shape.ContactRole.Id != Guid.Empty)
                    throw new InvalidOperationException("Authored contact role cannot be rebound by a script.");

                found = true;
                break;
            }
        if (!found) throw new ArgumentException("Shape does not belong to the body.");
        ContactRouter.BindRole(body.OwnerHandle, body.ComponentId, shapeId, role, subscriber.Scope);
    }

    public const uint AllLayers = uint.MaxValue;
    public const int MaxQueryCapacity = 256;
    public const int MaxBatchRequests = 64;
    public const int MaxBatchHitCapacity = 4096;

    // One owner read window. Hit ranges must be disjoint; zero capacity supports discovery.
    // Outputs are unchanged on batch-level failure. Individual errors appear in results.
    public static PhysicsError QueryBatch(Entity sceneAnchor, ReadOnlySpan<PhysicsQueryRequest> requests,
                                         Span<PhysicsHit> hits, Span<PhysicsBatchResult> results)
        => Native.PhysicsQueryBatch(sceneAnchor.Handle, requests, hits, results);

    // The anchor selects a scene by a live Entity handle, rather than global active-scene state.
    // Direction must be unit length. Only Written entries are valid; empty spans support capacity discovery.
    public static PhysicsQueryResult Raycast(Entity sceneAnchor, Float3 origin, Float3 direction, float distance,
                                            Span<PhysicsHit> results, uint layers = AllLayers, bool includeSensors = false)
        => Native.PhysicsQuery(sceneAnchor.Handle, 0, origin, direction, distance, layers, includeSensors, results);

    public static PhysicsQueryResult OverlapSphere(Entity sceneAnchor, Float3 position, float radius,
                                                   Span<PhysicsHit> results, uint layers = AllLayers, bool includeSensors = false)
        => Native.PhysicsQuery(sceneAnchor.Handle, 1, position, default, radius, layers, includeSensors, results);
}
