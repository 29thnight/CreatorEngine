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

/// <summary>Scope-owned, owner-thread contact buffer. Read does not search or allocate.
/// The borrowed span expires when PostPhysics completes. Catch-up ticks retain their tick IDs.</summary>
public sealed class ContactStream : IDisposable
{
    internal readonly Component Subscriber;
    internal readonly ObjectHandle Owner;
    internal readonly ShapeRole SelfRole, OtherRole;
    internal readonly ContactPhases Phases;
    private readonly int _thread = Environment.CurrentManagedThreadId;
    private readonly Contact[] _buffer;
    private readonly record struct SensorPair(ObjectHandle First, ulong FirstComponent, uint FirstShape, uint FirstGeneration, uint FirstSlot,
        ObjectHandle Second, ulong SecondComponent, uint SecondShape, uint SecondGeneration, uint SecondSlot);
    private readonly HashSet<SensorPair> _sensorPairs;
    private readonly ContactGrouping _grouping;
    private readonly record struct TargetState(int Pairs, ulong LastTick);
    private readonly Dictionary<ObjectHandle, TargetState> _targets;
    private int _count;
    internal bool Disposed, Touched;
    public bool Overflowed { get; private set; }
    public int RequiredCapacity { get; private set; }

    internal ContactStream(Component subscriber, ObjectHandle owner, ShapeRole self, ShapeRole other,
        ContactPhases phases, int capacity, ContactGrouping grouping = ContactGrouping.ShapePairs)
    {
        Subscriber = subscriber;
        Owner = owner;
        SelfRole = self;
        OtherRole = other;
        Phases = phases;
        _buffer = new Contact[capacity];
        _sensorPairs = new(capacity);
        _grouping = grouping;
        _targets = new(capacity);
    }

    public ReadOnlySpan<Contact> Read()
    {
        RequireThread();
        ObjectDisposedException.ThrowIf(Disposed, this);
        if (Overflowed) throw new InvalidOperationException("Contact stream capacity exceeded; partial gameplay results are unavailable.");
        return _buffer.AsSpan(0, _count);
    }

    internal void Append(in Contact contact)
    {
        ++RequiredCapacity;
        if (_count == _buffer.Length) { Overflowed = true; return; }
        _buffer[_count++] = contact;
    }

    internal void Clear() { _count = RequiredCapacity = 0; Overflowed = Touched = false; }

    internal void ResetObservation()
    {
        _sensorPairs.Clear();
        _targets.Clear();
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
                    Overflowed = true;
                    ++RequiredCapacity;
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
        Clear();
    }
}

internal static class ContactRouter
{
    [ThreadStatic]
    internal static Component? Current;
    private readonly record struct EndpointKey(ObjectHandle Owner, ulong Component, uint Shape);
    private readonly record struct RouteKey(ObjectHandle Owner, ShapeRole Self, ShapeRole Other, ContactPhases Phase);
    private static readonly Dictionary<EndpointKey, ShapeRole> Roles = new();
    private static readonly Dictionary<RouteKey, List<ContactStream>> Routes = new();
    private static readonly List<ContactStream> Touched = new();
    private static readonly Dictionary<Component, List<ContactStream>> Subscribers = new(ReferenceEqualityComparer.Instance);

    internal static void Suspend(Component subscriber)
    {
        if (!Subscribers.TryGetValue(subscriber, out var streams)) return;

        foreach (var stream in streams)
        {
            Touched.Remove(stream);
            stream.ResetObservation();
        }
    }

    internal static void BindRole(ObjectHandle owner, ulong component, uint shape, ShapeRole role, SimulationScope scope)
    {
        var key = new EndpointKey(owner, component, shape);
        if (!Roles.TryAdd(key, role)) throw new InvalidOperationException("Shape already has a contact role binding.");
        scope.RegisterCleanup(() => Roles.Remove(key));
    }

    internal static ContactStream Observe(Component subscriber, ObjectHandle owner, ShapeRole self, ShapeRole other,
        ContactPhases phases, int capacity, ContactGrouping grouping = ContactGrouping.ShapePairs)
    {
        if (!owner.IsValid || self.Id == Guid.Empty || other.Id == Guid.Empty || phases == 0 ||
            (phases & ~ContactPhases.All) != 0 || capacity <= 0 || capacity > 65536 ||
            !Enum.IsDefined(grouping))
            throw new ArgumentException("Invalid contact subscription.");
        var stream = new ContactStream(subscriber, owner, self, other, phases, capacity, grouping);
        foreach (var phase in new[] { ContactPhases.Begin, ContactPhases.Persist, ContactPhases.End })
        {
            // Track sensor pair transitions even for Begin-only/End-only subscriptions.
            var key = new RouteKey(owner, self, other, phase);
            if (!Routes.TryGetValue(key, out var list)) Routes.Add(key, list = new());
            list.Add(stream);
        }
        if (!Subscribers.TryGetValue(subscriber, out var owned)) Subscribers.Add(subscriber, owned = new());
        owned.Add(stream);

        subscriber.Scope.RegisterCleanup(stream.Dispose);
        return stream;
    }

    internal static void Remove(ContactStream stream)
    {
        Touched.Remove(stream);
        if (Subscribers.TryGetValue(stream.Subscriber, out var owned))
        {
            owned.Remove(stream);
            if (owned.Count == 0) Subscribers.Remove(stream.Subscriber);
        }

        foreach (var phase in new[] { ContactPhases.Begin, ContactPhases.Persist, ContactPhases.End })
        {
            var key = new RouteKey(stream.Owner, stream.SelfRole, stream.OtherRole, phase);
            if (!Routes.TryGetValue(key, out var list)) continue;
            list.Remove(stream);
            if (list.Count == 0) Routes.Remove(key);
        }
    }

    internal static void Route(in NativeContact value)
    {
        var phase = value.Kind switch { 0 or 3 => ContactPhases.Begin, 1 or 5 => ContactPhases.Persist,
            2 or 4 => ContactPhases.End, _ => throw new ArgumentException("Unknown contact phase.") };
        var first = ResolveRole(value.First);
        var second = ResolveRole(value.Second);
        if (first.Id == Guid.Empty || second.Id == Guid.Empty) return;
        Deliver(new(value.First.Owner, first, second, phase), value, false, phase);
        Deliver(new(value.Second.Owner, second, first, phase), value, true, phase);
    }

    private static ShapeRole ResolveRole(in ContactEndpoint endpoint)
    {
        if (endpoint.Role != Guid.Empty) return new(endpoint.Role);
        return Roles.TryGetValue(new(endpoint.Owner, endpoint.Component, endpoint.Shape), out var role) ? role : default;
    }

    private static void Deliver(RouteKey key, in NativeContact value, bool reverse, ContactPhases phase)
    {
        if (!Routes.TryGetValue(key, out var streams)) return;
        foreach (var stream in streams)
        {
            if (stream.Disposed || !stream.Subscriber.Enabled || !stream.Subscriber.BeginSucceeded) continue;
            var observed = stream.ObservePhase(value, phase, reverse);
            if (observed == 0 && !stream.Overflowed) continue;
            if (!stream.Touched) { Touched.Add(stream); stream.Touched = true; }
            if (observed != 0) stream.Append(new Contact(value, reverse, observed));
        }
    }

    internal static void EndFrame()
    {
        foreach (var stream in Touched) stream.Clear();
        Touched.Clear();
    }
}
