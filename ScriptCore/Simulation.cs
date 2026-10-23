using System.Runtime.InteropServices;

namespace CreatorEngine;

public enum SimulationResult
{
    Ok,
    Inactive,
    StaleEpoch,
    InvalidArgument,
    Late,
    TooFar,
    Full,
    NoSnapshot,
    SnapshotGap,
    BufferTooSmall,
    WrongThread,
    Busy,
    Failed
}

[StructLayout(LayoutKind.Sequential)]
public readonly struct SimulationStatus
{
    public readonly ulong Epoch;
    public readonly ulong Committed;
    public readonly ulong Executing;

    public readonly uint Queued;
    public readonly uint Active;

    public ulong NextTick => (Executing > Committed ? Executing : Committed) + 1;
}

[StructLayout(LayoutKind.Sequential)]
public readonly struct SimulationSnapshot
{
    public readonly ulong Epoch;
    public readonly ulong Tick;

    public readonly uint Applied;
    public readonly uint Rejected;
    public readonly uint Bytes;
    public readonly uint Schema;
}

/// <summary>
/// Game-thread value boundary. This is a local ABI, not a network serializer.
/// Commands execute before OnTickSimulate. Snapshots contain only successful post-physics commits.
/// Capacity: 256 commands, 64 bytes/command, 128 future ticks, 8 snapshots, 4096 bytes/snapshot.
/// Copy payloads immediately; SnapshotGap reports overwrite and BufferTooSmall leaves the cursor unchanged.
/// Async producers marshal validated values to the game thread using ordinary .NET facilities.
/// </summary>
public static class Simulation
{
    public static SimulationResult SetWorldPosition(
        ulong epoch,
        ulong tick,
        Entity owner,
        Float3 position,
        out ulong sequence)
        => SetWorldPosition(epoch, tick, owner.Handle, position, out sequence);

    public static SimulationResult GetStatus(out SimulationStatus status)
        => Native.SimulationStatus(out status);

    public static SimulationResult Submit(
        ulong epoch,
        ulong tick,
        uint kind,
        ReadOnlySpan<byte> payload,
        out ulong sequence)
        => Native.SimulationSubmit(epoch, tick, kind, payload, out sequence);

    public static SimulationResult ReadSnapshot(
        ulong epoch,
        ulong afterTick,
        Span<byte> payload,
        out SimulationSnapshot snapshot)
        => Native.SimulationRead(epoch, afterTick, payload, out snapshot);

    // Built-in local command/schema 1: ObjectHandle (8) + world xyz (12).
    // Transport adapters must resolve their own wire IDs, never send this handle over the wire.
    public static SimulationResult SetWorldPosition(
        ulong epoch,
        ulong tick,
        ObjectHandle owner,
        Float3 position,
        out ulong sequence)
    {
        Span<byte> payload = stackalloc byte[20];
        MemoryMarshal.Write(payload, in owner);
        MemoryMarshal.Write(payload[8..], in position);

        return Submit(epoch, tick, 1, payload, out sequence);
    }
}
