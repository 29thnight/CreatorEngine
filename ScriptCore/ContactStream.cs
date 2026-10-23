using System.Runtime.InteropServices;

namespace CreatorEngine;

[Flags]
public enum ContactPhases { Begin = 1, Persist = 2, End = 4, All = 7 }

/// <summary>SensorTargets aggregates sensor pairs by target entity; solid contacts remain shape-pair events.</summary>
public enum ContactGrouping { ShapePairs, SensorTargets }

public readonly record struct ShapeRole(Guid Id);

public static class ShapeRoles
{
    public static readonly ShapeRole Attack = new(new Guid("67adfded-47c8-4ef9-9c38-d1c3497a7421"));
    public static readonly ShapeRole Hurt = new(new Guid("7d352a65-f9bd-4bd5-83cf-97d1d276f740"));
}

[StructLayout(LayoutKind.Sequential)]
internal struct ContactEndpoint
{
    public ObjectHandle Owner;
    public ulong Component;
    public uint Shape;
    public uint BodyGeneration;
    public uint BodySlot, Reserved;
    public Guid Role;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeContact
{
    public ContactEndpoint First, Second;
    public ulong Tick;
    public int Kind, ContactCount, RequiredContacts;
    public Float3 Point;
}

public readonly struct Contact
{
    private readonly ContactEndpoint _self, _other;
    public Entity SelfEntity => new(_self.Owner);
    public Entity OtherEntity => new(_other.Owner);
    public ulong SelfComponentId => _self.Component;
    public ulong OtherComponentId => _other.Component;
    public uint SelfShapeId => _self.Shape;
    public uint OtherShapeId => _other.Shape;
    public uint SelfBodySlot => _self.BodySlot;
    public uint OtherBodySlot => _other.BodySlot;
    public uint SelfBodyGeneration => _self.BodyGeneration;
    public uint OtherBodyGeneration => _other.BodyGeneration;
    public readonly ulong Tick;
    public readonly ContactPhases Phase;
    public readonly bool Sensor;
    public readonly int ContactCount, RequiredContacts;
    public readonly Float3 Point;

    internal Contact(in NativeContact value, bool reverse, ContactPhases phase)
    {
        _self = reverse ? value.Second : value.First;
        _other = reverse ? value.First : value.Second;
        Tick = value.Tick;
        Phase = phase;
        Sensor = value.Kind >= 3;
        ContactCount = value.ContactCount;
        RequiredContacts = value.RequiredContacts;
        Point = value.Point;
    }
}

/// <summary>Engine-lifetime-owned, owner-thread contact buffer. Read does not search or allocate.
/// The borrowed span expires when PostPhysics completes. Catch-up ticks retain their tick IDs.</summary>
public sealed class ContactStream : IDisposable
{
    internal readonly Component Subscriber;
    internal readonly ObjectHandle Owner;
    internal readonly ShapeRole SelfRole, OtherRole;
    internal readonly ContactPhases Phases;
    private readonly int _thread = Environment.CurrentManagedThreadId;
    private readonly Contact[] _buffer, _tickBuffer;
    private int _tickCount, _tickRequired;
    private bool _tickOverflow;
    private readonly record struct SensorPair(ObjectHandle First, ulong FirstComponent, uint FirstShape, uint FirstGeneration, uint FirstSlot,
        ObjectHandle Second, ulong SecondComponent, uint SecondShape, uint SecondGeneration, uint SecondSlot);
    private readonly HashSet<SensorPair> _sensorPairs;
    private readonly ContactGrouping _grouping;
    private readonly record struct TargetState(int Pairs, ulong LastTick);
    private readonly Dictionary<ObjectHandle, TargetState> _targets;
    private int _count;
    internal bool Disposed, Touched;
    private bool _overflow;
    private int _required;
    public bool Overflowed => ScriptRegistry.InFixedUpdate ? _tickOverflow : _overflow;
    public int RequiredCapacity => ScriptRegistry.InFixedUpdate ? _tickRequired : _required;

    internal ContactStream(Component subscriber, ObjectHandle owner, ShapeRole self, ShapeRole other,
        ContactPhases phases, int capacity, ContactGrouping grouping = ContactGrouping.ShapePairs)
    {
        Subscriber = subscriber;
        Owner = owner;
        SelfRole = self;
        OtherRole = other;
        Phases = phases;
        _buffer = new Contact[capacity];
        _tickBuffer = new Contact[capacity];
        _sensorPairs = new(capacity);
        _grouping = grouping;
        _targets = new(capacity);
    }

    public ReadOnlySpan<Contact> Read()
    {
        RequireThread();
        ObjectDisposedException.ThrowIf(Disposed, this);
        if (Overflowed) throw new InvalidOperationException("Contact stream capacity exceeded; partial gameplay results are unavailable.");
        return ScriptRegistry.InFixedUpdate ? _tickBuffer.AsSpan(0, _tickCount) : _buffer.AsSpan(0, _count);
    }

    internal void Append(in Contact contact)
    {
        ++_required;
        ++_tickRequired;
        if (_count == _buffer.Length) _overflow = true;
        else _buffer[_count++] = contact;
        if (_tickCount == _tickBuffer.Length) _tickOverflow = true;
        else _tickBuffer[_tickCount++] = contact;
    }

    internal void Clear() { _count = _required = 0; _overflow = Touched = false; }
    internal void ClearTick() { _tickCount = _tickRequired = 0; _tickOverflow = false; }

    internal void ResetObservation()
    {
        _sensorPairs.Clear();
        _targets.Clear();
        ClearTick();
        Clear();
    }

    internal ContactPhases ObservePhase(in NativeContact value, ContactPhases phase, bool reverse)
    {
        if (value.Kind < 3) return (Phases & phase) != 0 ? phase : 0;

        var pair = new SensorPair(value.First.Owner, value.First.Component, value.First.Shape, value.First.BodyGeneration, value.First.BodySlot,
            value.Second.Owner, value.Second.Component, value.Second.Shape, value.Second.BodyGeneration, value.Second.BodySlot);
        var target = reverse ? value.First.Owner : value.Second.Owner;
        bool known = _sensorPairs.Contains(pair);
        bool grouped = _grouping == ContactGrouping.SensorTargets;
        _targets.TryGetValue(target, out var state);

        if (phase == ContactPhases.End)
        {
            _sensorPairs.Remove(pair);
            if (grouped)
            {
                if (!known) return 0;
                if (state.Pairs > 1)
                {
                    _targets[target] = state with { Pairs = state.Pairs - 1 };
                    return 0;
                }

                _targets.Remove(target);
            }
        }
        else
        {
            if (!known)
            {
                if (_sensorPairs.Count == _buffer.Length)
                {
                    _overflow = _tickOverflow = true;
                    ++_required; ++_tickRequired;
                    return 0;
                }

                _sensorPairs.Add(pair);
                if (grouped)
                {
                    bool first = state.Pairs == 0;
                    state = state with { Pairs = state.Pairs + 1 };
                    _targets[target] = state;
                    if (first) phase = ContactPhases.Begin;
                    else phase = ContactPhases.Persist;
                }
                else if (phase == ContactPhases.Persist && (Phases & ContactPhases.Begin) != 0)
                    phase = ContactPhases.Begin;
            }
            else if (grouped) phase = ContactPhases.Persist;

            if (grouped)
            {
                // One delivery per target/tick; retain state across frame clears.
                if (state.LastTick == value.Tick) return 0;
                _targets[target] = state with { LastTick = value.Tick };
                // Persist-only subscriptions must not lose the first observed overlap.
                if (phase == ContactPhases.Begin && (Phases & ContactPhases.Begin) == 0)
                    phase = ContactPhases.Persist;
            }
        }

        return (Phases & phase) != 0 ? phase : 0;
    }

    private void RequireThread()
    {
        if (_thread != Environment.CurrentManagedThreadId) throw new InvalidOperationException("Contact stream requires its simulation owner thread.");
    }

    public void Dispose()
    {
        RequireThread();
        if (Disposed) return;
        ContactRouter.Remove(this);
        Disposed = true;
        _sensorPairs.Clear();
        _targets.Clear();
        ClearTick();
        Clear();
    }
}

// ABI/value-buffer facade; F# owns routing indices and subscription membership.
internal static class ContactRouter
{
    internal static Component? Current
    {
        get => ManagedRuntime.Current.Contacts.Current;
        set => ManagedRuntime.Current.Contacts.Current = value;
    }

    internal static void Suspend(Component subscriber) => ManagedRuntime.Current.Contacts.Suspend(subscriber);
    internal static void BindRole(ObjectHandle owner, ulong body, uint shape, ShapeRole role, ComponentLifetime lifetime)
        => ManagedRuntime.Current.Contacts.BindRole(owner, body, shape, role, lifetime);
    internal static ContactStream Observe(Component subscriber, ObjectHandle owner, ShapeRole self, ShapeRole other,
        ContactPhases phases, int capacity, ContactGrouping grouping = ContactGrouping.ShapePairs)
        => ManagedRuntime.Current.Contacts.Observe(subscriber, owner, self, other, phases, capacity, grouping);
    internal static void Remove(ContactStream stream) => ManagedRuntime.Current.Contacts.Remove(stream);
    internal static void Route(in NativeContact value) => ManagedRuntime.Current.Contacts.Route(value);
    internal static void EndUpdate() => ManagedRuntime.Current.Contacts.EndUpdate();
    internal static void EndFrame() => ManagedRuntime.Current.Contacts.EndFrame();
}
