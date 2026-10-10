using System.Runtime.InteropServices;

namespace CreatorEngine
{
    [StructLayout(LayoutKind.Sequential)]
    public readonly record struct InputID(ulong High, ulong Low)
    {
        public bool IsValid => High != 0 || Low != 0;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal readonly record struct InputSessionHandle(ulong ID, ulong Generation)
    {
        internal bool IsValid => ID != 0 && Generation != 0;
    }

    public enum InputDomain : uint { Game, UI }
    public enum InputValueType : uint { Button, Float, Vector2 }
    public enum InputDeviceKind : uint { Keyboard, Mouse, Gamepad }
    public enum InputEventPhase : uint { Started, Performed, Completed, Canceled }
    public enum InputCancelReason : uint
    {
        None, LayerBlocked, UIOwnership, FocusLost, Paused, DeviceDisconnected, DeviceReassigned,
        DefinitionChanged, Rebound, HistoryGap, ClockDiscontinuity, SessionShutdown, ScriptReload, InteractionTimeout
    }
    public enum InputSourceKind : uint
    {
        Key, MouseButton, MouseDelta, PointerPosition, MouseWheel, GamepadButton, GamepadAxis
    }
    public enum InputCoordinateSpace : uint { None, ClientPixels, RelativeCounts, Normalized }

    /// <summary>Copied device identity. Assignment requests recheck both epochs;
    /// a disconnected/reassigned device never retargets this snapshot silently.</summary>
    public readonly struct InputDevice
    {
        private readonly NativeInputDevice _value;
        internal InputDevice(in NativeInputDevice value) { _value = value; }
        public ulong ID => _value.ID;
        public ulong Epoch => _value.Epoch;
        public ulong AssignmentEpoch => _value.AssignmentEpoch;
        public ulong AssignedUser => _value.User;
        public InputDeviceKind Kind => (InputDeviceKind)_value.Kind;
        public int ControllerIndex => _value.ControllerIndex;
    }

    public static class InputDevices
    {
        /// <summary>Returns a bounded, owned snapshot of connected devices. Refresh
        /// after a rejected assignment to obtain the latest connection generation.</summary>
        public static InputDevice[] GetConnected() => Native.InputListDevices();
    }

    /// <summary>A descriptor emitted from the compiled LX output. It does not author a graph.</summary>
    public readonly struct InputSignal<T> where T : unmanaged
    {
        public InputID Graph { get; }
        public InputID ID { get; }
        public ulong InterfaceHash { get; }
        public uint SchemaVersion { get; }
        public uint ABIVersion { get; }
        public InputValueType Type { get; }
        private readonly Func<NativeInputValue, T>? _decode;

        internal InputSignal(InputID graph, InputID id, ulong hash, uint schema, uint abi,
            InputValueType type, Func<NativeInputValue, T> decode)
        {
            Graph = graph;
            ID = id;
            InterfaceHash = hash;
            SchemaVersion = schema;
            ABIVersion = abi;
            Type = type;
            _decode = decode;
        }

        internal T Decode(in NativeInputValue value)
        {
            if (_decode is null || value.Type != (uint)Type)
            {
                throw new InvalidOperationException("Input signal value type does not match its generated descriptor.");
            }
            return _decode(value);
        }
    }

    /// <summary>Factories used by generated accessors. No reflection or runtime name lookup.</summary>
    public static class InputSignal
    {
        public static InputSignal<bool> Button(InputID graph, InputID signal, ulong interfaceHash,
            uint schemaVersion = 1, uint abiVersion = 1)
            => new(graph, signal, interfaceHash, schemaVersion, abiVersion, InputValueType.Button, static v => v.X != 0);

        public static InputSignal<float> Float(InputID graph, InputID signal, ulong interfaceHash,
            uint schemaVersion = 1, uint abiVersion = 1)
            => new(graph, signal, interfaceHash, schemaVersion, abiVersion, InputValueType.Float, static v => v.X);

        public static InputSignal<Float2> Vector2(InputID graph, InputID signal, ulong interfaceHash,
            uint schemaVersion = 1, uint abiVersion = 1)
            => new(graph, signal, interfaceHash, schemaVersion, abiVersion, InputValueType.Vector2, static v => new(v.X, v.Y));
    }

    public readonly record struct InputSignalState<T>(T Value, bool Held, bool Pressed, bool Released) where T : unmanaged;

    public readonly struct SignalEvent
    {
        private readonly NativeInputEvent _value;
        internal SignalEvent(in NativeInputEvent value) { _value = value; }
        public InputID Signal => _value.Signal;
        public InputValueType Type => (InputValueType)_value.Value.Type;
        public InputEventPhase Phase => (InputEventPhase)_value.Phase;
        public InputCancelReason Reason => (InputCancelReason)_value.Reason;
        public long SourceTime => _value.SourceTime;
        public long EffectiveTime => _value.EffectiveTime;
        public ulong Sequence => _value.Sequence;
        public ulong RoutingEpoch => _value.RoutingEpoch;
        public ulong DeviceEpoch => _value.DeviceEpoch;
        public ulong User => _value.User;
        public bool Late => (_value.Flags & 1) != 0;
        internal NativeInputValue Value => _value.Value;
    }

    public readonly struct SignalEvent<T> where T : unmanaged
    {
        public SignalEvent Event { get; }
        public T Value { get; }
        public InputEventPhase Phase => Event.Phase;
        public InputCancelReason Reason => Event.Reason;
        internal SignalEvent(SignalEvent value, InputSignal<T> signal)
        {
            Event = value;
            Value = signal.Decode(value.Value);
        }
    }

    /// <summary>Immutable owned copy of one sealed native frame. Safe to retain across ticks,
    /// session destruction and reload. No unmanaged pointer escapes the copy boundary.</summary>
    public sealed class InputFrame
    {
        internal readonly NativeInputFrameHeader Header;
        private readonly NativeInputState[] _states;
        private readonly SignalEvent[] _events;
        public InputID Graph => Header.Graph;
        public ulong SemanticHash => Header.SemanticHash;
        public ulong InterfaceHash => Header.InterfaceHash;
        public ulong DefinitionGeneration => Header.DefinitionGeneration;
        public ulong Sequence => Header.Sequence;
        public ulong User => Header.User;
        public InputDomain Domain => (InputDomain)Header.Domain;
        public long BeginTime => Header.Begin;
        public long EndTime => Header.End;
        public bool HasHistoryGap => (Header.Flags & 1) != 0;
        public ReadOnlySpan<SignalEvent> Events => _events;

        internal InputFrame(in NativeInputFrameHeader header, NativeInputState[] states, NativeInputEvent[] events)
        {
            ValidateHeader(header);
            if (states.Length != header.StateCount || events.Length != header.EventCount)
            {
                throw new InvalidOperationException("Input frame arrays do not match the sealed header.");
            }
            Header = header;
            _states = states;
            _events = new SignalEvent[events.Length];
            for (int i = 0; i < events.Length; ++i)
            {
                if (events[i].Value.Type > 2 || events[i].Phase > 3 || events[i].Reason > 13)
                {
                    throw new InvalidOperationException("Malformed input event batch.");
                }
                _events[i] = new(events[i]);
            }
        }

        internal static void ValidateHeader(in NativeInputFrameHeader header)
        {
            if (!BitConverter.IsLittleEndian || header.ABIVersion != 1 || header.SchemaVersion != 1 ||
                header.CompilerVersion != 1 || !header.Session.IsValid || !header.Graph.IsValid || header.Domain > 1 ||
                header.StateCount > 65536 || header.EventCount > 262144 || header.End < header.Begin)
            {
                throw new InvalidOperationException("Unsupported or malformed InputGraph frame ABI.");
            }
        }

        internal void Validate<T>(InputSignal<T> signal) where T : unmanaged
        {
            if (!signal.ID.IsValid || signal.Graph != Graph || signal.InterfaceHash != InterfaceHash ||
                signal.SchemaVersion != Header.SchemaVersion || signal.ABIVersion != Header.ABIVersion)
            {
                throw new InvalidOperationException("Generated InputGraph accessor is stale or belongs to another graph.");
            }
        }

        public InputSignalState<T> Read<T>(InputSignal<T> signal) where T : unmanaged
        {
            Validate(signal);
            foreach (ref readonly var state in _states.AsSpan())
            {
                if (state.Signal == signal.ID)
                {
                    return new(signal.Decode(state.Value), (state.Flags & 1) != 0,
                        (state.Flags & 2) != 0, (state.Flags & 4) != 0);
                }
            }
            throw new KeyNotFoundException("Input signal is not present in this frame domain.");
        }

        public SignalEvent<T> ReadEvent<T>(int index, InputSignal<T> signal) where T : unmanaged
        {
            Validate(signal);
            var value = _events[index];
            if (value.Signal != signal.ID)
            {
                throw new ArgumentException("Event belongs to another signal.", nameof(signal));
            }
            return new(value, signal);
        }
    }

    /// <summary>A generation-checked reference to the entity's native InputSessionComponent.
    /// Control requests are applied at the next evaluation boundary, never during dispatch.</summary>
    public sealed class InputSession
    {
        internal readonly InputSessionHandle Handle;
        internal readonly uint Epoch;
        public InputDomain Domain { get; }
        private readonly ObjectHandle _owner;

        private InputSession(ObjectHandle owner, InputSessionHandle handle, InputDomain domain)
        {
            _owner = owner;
            Handle = handle;
            Domain = domain;
            Epoch = InputRouter.Epoch;
        }

        public static InputSession For(Component component, InputDomain domain = InputDomain.Game)
        {
            if (!TryFor(component, out var session, domain))
            {
                throw new InvalidOperationException("Entity has no ready InputSessionComponent. Use TryFor while its graph loads.");
            }
            return session!;
        }

        public static bool TryFor(Component component, out InputSession? session, InputDomain domain = InputDomain.Game)
        {
            ArgumentNullException.ThrowIfNull(component);
            InputRouter.RequireThread();
            if ((uint)domain > 1)
            {
                throw new ArgumentOutOfRangeException(nameof(domain));
            }
            var handle = Native.InputFindSession(component.Entity.Handle);
            session = handle.IsValid ? new(component.Entity.Handle, handle, domain) : null;
            return session is not null;
        }

        internal void RequireAlive()
        {
            InputRouter.RequireThread();
            if (Epoch != InputRouter.Epoch || Native.InputFindSession(_owner) != Handle)
            {
                throw new ObjectDisposedException(nameof(InputSession), "Session, entity or script generation changed.");
            }
        }

        public InputFrame GetFrame()
        {
            RequireAlive();
            return InputRouter.GetFrame(Handle, Domain);
        }

        public InputSubscription Subscribe<T>(Component subscriber, InputSignal<T> signal,
            Action<SignalEvent<T>> callback) where T : unmanaged
        {
            RequireAlive();
            return InputRouter.Subscribe(this, subscriber, signal, callback);
        }

        public void SetLayerEnabled(InputID layer, bool enabled)
        {
            RequireAlive();
            Native.InputRequest(Handle, new() { Kind = 0, Target = layer, Enabled = enabled ? 1u : 0u });
        }

        public void AssignDevice(InputDevice device, bool assigned = true)
        {
            AssignDevice(device.ID, device.Epoch, device.AssignmentEpoch, assigned);
        }

        public void AssignDevice(ulong device, ulong deviceEpoch, ulong assignmentEpoch, bool assigned = true)
        {
            RequireAlive();
            Native.InputRequest(Handle, new() { Kind = 1, Device = device, DeviceEpoch = deviceEpoch,
                AssignmentEpoch = assignmentEpoch, Enabled = assigned ? 1u : 0u });
        }

        /// <summary>Device output follows this session's assignment. Reading/replaying a frame never repeats it.</summary>
        public void Vibrate(float seconds, float leftMotor, float rightMotor)
        {
            RequireAlive();
            Native.InputHaptic(Handle, seconds, leftMotor, rightMotor);
        }

        public void Rebind(InputID binding, InputSourceKind source, uint controlCode,
            InputCoordinateSpace space, float scaleX = 1, float scaleY = 0)
        {
            RequireAlive();
            Native.InputRequest(Handle, new() { Kind = 2, Target = binding, SourceKind = (uint)source,
                ControlCode = controlCode, CoordinateSpace = (uint)space, ScaleX = scaleX, ScaleY = scaleY });
        }
    }

    /// <summary>Dispose invalidates immediately, including inside a callback. New subscriptions
    /// created while delivering a batch begin with the next frame.</summary>
    public sealed class InputSubscription : IDisposable
    {
        internal readonly ulong Token;
        internal readonly uint Epoch;
        internal InputSubscription(ulong token, uint epoch) { Token = token; Epoch = epoch; }
        public void Dispose()
        {
            InputRouter.RequireThread();
            InputRouter.Remove(Token, Epoch);
        }
    }

    internal static class InputRouter
    {
        private abstract class Subscription
        {
            internal required Component Owner;
            internal required InputSession Session;
            internal required ulong Token;
            internal abstract void Deliver(InputFrame frame, SignalEvent value);
        }

        private sealed class Subscription<T> : Subscription where T : unmanaged
        {
            internal required InputSignal<T> Signal;
            internal required Action<SignalEvent<T>> Callback;
            internal override void Deliver(InputFrame frame, SignalEvent value)
            {
                if (value.Signal != Signal.ID)
                {
                    return;
                }
                frame.Validate(Signal);
                Callback(new(value, Signal));
            }
        }

        private readonly record struct FrameKey(InputSessionHandle Session, InputDomain Domain);
        private static readonly Dictionary<ulong, Subscription> Subscriptions = new();
        private static readonly Dictionary<FrameKey, InputFrame> Frames = new();
        private static ulong _nextToken;
        internal static uint Epoch { get; private set; } = 1;

        internal static void RequireThread()
        {
            if (!Native.IsGameThread)
            {
                throw new InvalidOperationException("InputSession requires the simulation owner thread.");
            }
        }

        internal static InputSubscription Subscribe<T>(InputSession session, Component owner, InputSignal<T> signal,
            Action<SignalEvent<T>> callback) where T : unmanaged
        {
            ArgumentNullException.ThrowIfNull(owner);
            ArgumentNullException.ThrowIfNull(callback);
            RequireThread();
            if (!signal.ID.IsValid || !signal.Graph.IsValid || signal.SchemaVersion != 1 || signal.ABIVersion != 1)
            {
                throw new ArgumentException("Invalid generated input descriptor.", nameof(signal));
            }
            Native.InputValidateSignal(session.Handle, signal.Graph, signal.ID, signal.InterfaceHash, signal.Type, session.Domain);
            ulong token = checked(++_nextToken);
            var result = new InputSubscription(token, Epoch);
            Subscriptions.Add(token, new Subscription<T> { Owner = owner, Session = session, Token = token,
                Signal = signal, Callback = callback });
            owner.Scope.RegisterCleanup(result.Dispose);
            return result;
        }

        internal static void Remove(ulong token, uint epoch)
        {
            if (epoch == Epoch)
            {
                Subscriptions.Remove(token);
            }
        }

        internal static void Invalidate(InputSessionHandle session)
        {
            RequireThread();
            foreach (var token in Subscriptions.Where(pair => pair.Value.Session.Handle == session)
                .Select(pair => pair.Key).ToArray())
            {
                Subscriptions.Remove(token);
            }
            Frames.Remove(new(session, InputDomain.Game));
            Frames.Remove(new(session, InputDomain.UI));
        }

        internal static void RemoveOwner(Component owner)
        {
            foreach (var token in Subscriptions.Where(pair => ReferenceEquals(pair.Value.Owner, owner))
                .Select(pair => pair.Key).ToArray())
            {
                Subscriptions.Remove(token);
            }
        }

        internal static void Reset()
        {
            Subscriptions.Clear();
            Frames.Clear();
            Epoch = checked(Epoch + 1);
        }

        internal static InputFrame GetFrame(InputSessionHandle session, InputDomain domain)
        {
            RequireThread();
            var key = new FrameKey(session, domain);
            if (Frames.TryGetValue(key, out var frame))
            {
                return frame;
            }
            // This path is only needed before the first managed publication.
            // Do not mark it dispatched: subscribers still receive that boundary.
            return Native.InputCopyFrame(session, domain);
        }

        internal static void Publish(InputFrame frame)
        {
            RequireThread();
            var key = new FrameKey(frame.Header.Session, frame.Domain);
            if (Frames.TryGetValue(key, out var previous) && frame.Sequence <= previous.Sequence)
            {
                return;
            }
            Frames[key] = frame;
            // Snapshot token IDs once for the whole batch, never callback references.
            // Removal is checked again before every event; insertion is next-frame only.
            ulong[] tokens = Subscriptions.Keys.ToArray();
            uint epoch = Epoch;
            foreach (var value in frame.Events)
            {
                foreach (ulong token in tokens)
                {
                    if (epoch != Epoch)
                    {
                        return;
                    }
                    if (!Subscriptions.TryGetValue(token, out var subscription) ||
                        subscription.Session.Handle != frame.Header.Session || subscription.Session.Domain != frame.Domain)
                    {
                        continue;
                    }
                    var owner = subscription.Owner;
                    if (!owner.Enabled || !owner.BeginSucceeded || !owner.Entity.IsAlive)
                    {
                        continue;
                    }
                    try
                    {
                        subscription.Deliver(frame, value);
                    }
                    catch (Exception error)
                    {
                        try
                        {
                            Native.Log(3, $"[InputGraph] Subscription {token} failed; batch continues.\n{error}");
                        }
                        catch
                        {
                            // A custom Exception.ToString or a failing logger must
                            // not prevent the remaining valid subscribers running.
                        }
                    }
                }
            }
        }
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct NativeInputValue
    {
        internal uint Type;
        internal float X, Y;
        internal uint Reserved;
    }
    [StructLayout(LayoutKind.Sequential)]
    internal struct NativeInputState
    {
        internal InputID Signal;
        internal NativeInputValue Value;
        internal uint Flags, Reserved;
    }
    [StructLayout(LayoutKind.Sequential)]
    internal struct NativeInputEvent
    {
        internal InputID Signal;
        internal NativeInputValue Value;
        internal long SourceTime, EffectiveTime;
        internal ulong Sequence, RoutingEpoch, DeviceEpoch, User;
        internal uint Phase, Reason, Flags, Reserved;
    }
    [StructLayout(LayoutKind.Sequential)]
    internal struct NativeInputFrameHeader
    {
        internal uint ABIVersion, SchemaVersion, CompilerVersion, Flags;
        internal InputSessionHandle Session;
        internal InputID Graph;
        internal ulong SemanticHash, InterfaceHash, DefinitionGeneration, User, Sequence;
        internal long Begin, End;
        internal uint Domain, StateCount, EventCount, Reserved;
    }
    [StructLayout(LayoutKind.Sequential)]
    internal struct NativeInputDevice
    {
        internal ulong ID, Epoch, AssignmentEpoch, User;
        internal uint Kind;
        internal int ControllerIndex;
        internal uint Connected, Reserved;
    }
    [StructLayout(LayoutKind.Sequential)]
    internal struct NativeInputRequest
    {
        internal uint Kind, Enabled;
        internal InputID Target;
        internal ulong Device, DeviceEpoch, AssignmentEpoch;
        internal uint SourceKind, ControlCode;
        internal float ScaleX, ScaleY;
        internal uint CoordinateSpace, Reason;
    }
}
