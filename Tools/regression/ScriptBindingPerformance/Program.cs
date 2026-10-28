extern alias legacy;
extern alias direct;

using System.Diagnostics;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OldEntity = legacy::CreatorEngine.Entity;
using OldHandle = legacy::CreatorEngine.ObjectHandle;
using OldNative = legacy::CreatorEngine.Native;
using OldTable = legacy::CreatorEngine.ScriptApiTable;
using NewEntity = direct::CreatorEngine.Entity;
using NewHandle = direct::CreatorEngine.ObjectHandle;
using NewBootstrap = direct::CreatorEngine.Native.NativeBootstrap;
using OldSimulation = legacy::CreatorEngine.Simulation;
using NewSimulation = direct::CreatorEngine.Simulation;
using OldCharacter = legacy::CreatorEngine.CharacterMovementComponent;
using NewCharacter = direct::CreatorEngine.CharacterMovementComponent;

unsafe class Program
{
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static (long elapsed, long allocated, int count) MeasureCharacter(bool direct, OldCharacter oldCharacter, NewCharacter newCharacter, int iterations)
    {
        var allocated = GC.GetAllocatedBytesForCurrentThread();
        var started = Stopwatch.GetTimestamp();
        var count = 0;
        var state = default(direct::CreatorEngine.CharacterMovementState);
        var newVelocity = new direct::CreatorEngine.Float3(2, 0, 0);
        var oldVelocity = new legacy::CreatorEngine.Float3(2, 0, 0);
        for (var i = 0; i < iterations; ++i)
        {
            if (direct)
            {
                if ((int)newCharacter.ReadState(ref state) == 0 && (int)newCharacter.SetDesiredVelocity(newVelocity) == 0)
                {
                    ++count;
                }
            }
            else
            {
                if ((int)oldCharacter.ReadState(out _) == 0 && (int)oldCharacter.SetDesiredVelocity(oldVelocity) == 0)
                {
                    ++count;
                }
            }
        }
        return (Stopwatch.GetTimestamp() - started, GC.GetAllocatedBytesForCurrentThread() - allocated, count);
    }
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static (long elapsed, long allocated, int count) MeasureStatus(bool direct, int iterations)
    {
        var allocated = GC.GetAllocatedBytesForCurrentThread();
        var started = Stopwatch.GetTimestamp();
        var count = 0;
        var status = default(direct::CreatorEngine.SimulationStatus);
        for (var i = 0; i < iterations; ++i)
        {
            if (direct)
            {
                if ((int)NewSimulation.GetStatus(ref status) == 0)
                {
                    ++count;
                }
            }
            else
            {
                if ((int)OldSimulation.GetStatus(out _) == 0)
                {
                    ++count;
                }
            }
        }
        return (Stopwatch.GetTimestamp() - started, GC.GetAllocatedBytesForCurrentThread() - allocated, count);
    }
    [DllImport("ScriptNativeProvider.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern nint ProbeHeadlessTable();

    [DllImport("ScriptNativeProvider.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern ulong reflgen_creator_script_native_abi_fingerprint();

    [UnmanagedFunctionPointer(CallingConvention.StdCall)]
    private delegate int AliveDelegate(OldHandle handle);

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static (long elapsed, long allocated, int count) MeasureOld(OldEntity entity, int iterations)
    {
        var allocated = GC.GetAllocatedBytesForCurrentThread();
        var started = Stopwatch.GetTimestamp();
        var count = 0;
        for (var i = 0; i < iterations; ++i)
        {
            if (entity.IsAlive)
            {
                ++count;
            }
        }
        return (Stopwatch.GetTimestamp() - started, GC.GetAllocatedBytesForCurrentThread() - allocated, count);
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static (long elapsed, long allocated, int count) MeasureNew(NewEntity entity, int iterations)
    {
        var allocated = GC.GetAllocatedBytesForCurrentThread();
        var started = Stopwatch.GetTimestamp();
        var count = 0;
        for (var i = 0; i < iterations; ++i)
        {
            if (entity.IsAlive)
            {
                ++count;
            }
        }
        return (Stopwatch.GetTimestamp() - started, GC.GetAllocatedBytesForCurrentThread() - allocated, count);
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static (long elapsed, long allocated, int count) MeasureDelegate(AliveDelegate alive, OldHandle handle, int iterations)
    {
        var allocated = GC.GetAllocatedBytesForCurrentThread();
        var started = Stopwatch.GetTimestamp();
        var count = 0;
        for (var i = 0; i < iterations; ++i)
        {
            if (alive(handle) != 0)
            {
                ++count;
            }
        }
        return (Stopwatch.GetTimestamp() - started, GC.GetAllocatedBytesForCurrentThread() - allocated, count);
    }

    private static int Main()
    {
        var table = ProbeHeadlessTable();
        if (!OldNative.Bind((OldTable*)table))
        {
            throw new InvalidOperationException("Legacy table binding failed.");
        }
        delegate* unmanaged<nint, nuint, ulong, int, int> bind = &NewBootstrap.Initialize;
        delegate* unmanaged<int> unbind = &NewBootstrap.Shutdown;
        if (bind(table, (nuint)sizeof(OldTable), reflgen_creator_script_native_abi_fingerprint(), 1) != 0)
        {
            throw new InvalidOperationException("F# table binding failed.");
        }
        var oldEntity = new OldEntity(new OldHandle(7, 3));
        var newEntity = new NewEntity(new NewHandle(7, 3));
        var oldCharacter = OldCharacter.Find(oldEntity, 42) ?? throw new InvalidOperationException("Legacy character fixture unavailable.");
        var newCharacter = new NewCharacter(new NewHandle(7, 3), 42);
        var nativeDelegate = Marshal.GetDelegateForFunctionPointer<AliveDelegate>((nint)((OldTable*)table)->Entity_IsAlive);
        const int iterations = 1_000_000;
        MeasureOld(oldEntity, iterations);
        MeasureNew(newEntity, iterations);
        Console.WriteLine("order,backend,iterations,timestamp_ticks,allocated_bytes,successful_calls");
        for (var round = 0; round < 4; ++round)
        {
            var sequence = round % 2 == 0 ? new[] { false, true, true, false } : new[] { true, false, false, true };
            foreach (var direct in sequence)
            {
                var result = direct ? MeasureNew(newEntity, iterations) : MeasureOld(oldEntity, iterations);
                if (result.count != iterations || result.allocated != 0)
                {
                    throw new InvalidOperationException("Call results or allocation count diverged.");
                }
                Console.WriteLine($"{round},{(direct ? "fsharp" : "csharp")},{iterations},{result.elapsed},{result.allocated},{result.count}");
            }
        }
        MeasureDelegate(nativeDelegate, new OldHandle(7, 3), iterations);
        for (var round = 0; round < 4; ++round)
        {
            var result = MeasureDelegate(nativeDelegate, new OldHandle(7, 3), iterations);
            if (result.count != iterations || result.allocated != 0)
            {
                throw new InvalidOperationException("Delegate control diverged.");
            }
            Console.WriteLine($"{round},delegate-control,{iterations},{result.elapsed},{result.allocated},{result.count}");
        }
        MeasureStatus(false, iterations);
        MeasureStatus(true, iterations);
        for (var round = 0; round < 4; ++round)
        {
            var sequence = round % 2 == 0 ? new[] { false, true, true, false } : new[] { true, false, false, true };
            foreach (var direct in sequence)
            {
                var result = MeasureStatus(direct, iterations);
                if (result.count != iterations || result.allocated != 0)
                {
                    throw new InvalidOperationException("Simulation status results or allocation count diverged.");
                }
                Console.WriteLine($"{round},{(direct ? "status-fsharp" : "status-csharp")},{iterations},{result.elapsed},{result.allocated},{result.count}");
            }
        }
        MeasureCharacter(false, oldCharacter, newCharacter, iterations);
        MeasureCharacter(true, oldCharacter, newCharacter, iterations);
        for (var round = 0; round < 4; ++round)
        {
            var sequence = round % 2 == 0 ? new[] { false, true, true, false } : new[] { true, false, false, true };
            foreach (var direct in sequence)
            {
                var result = MeasureCharacter(direct, oldCharacter, newCharacter, iterations);
                if (result.count != iterations || result.allocated != 0)
                {
                    throw new InvalidOperationException("Character results or allocation count diverged.");
                }
                Console.WriteLine($"{round},{(direct ? "character-fsharp" : "character-csharp")},{iterations},{result.elapsed},{result.allocated},{result.count}");
            }
        }
        Console.WriteLine($"frequency,{Stopwatch.Frequency}");
        unbind();
        OldNative.Unbind();
        return 0;
    }
}
