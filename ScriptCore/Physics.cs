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

public static class Physics
{
    public const uint AllLayers = uint.MaxValue;
    public const int MaxQueryCapacity = 256;

    // The anchor selects a scene by a live Entity handle, rather than global active-scene state.
    // Direction must be unit length. Only Written entries are valid; empty spans support capacity discovery.
    public static PhysicsQueryResult Raycast(Entity sceneAnchor, Float3 origin, Float3 direction, float distance,
                                            Span<PhysicsHit> results, uint layers = AllLayers, bool includeSensors = false)
        => Native.PhysicsQuery(sceneAnchor.Handle, 0, origin, direction, distance, layers, includeSensors, results);

    public static PhysicsQueryResult OverlapSphere(Entity sceneAnchor, Float3 position, float radius,
                                                   Span<PhysicsHit> results, uint layers = AllLayers, bool includeSensors = false)
        => Native.PhysicsQuery(sceneAnchor.Handle, 1, position, default, radius, layers, includeSensors, results);
}
