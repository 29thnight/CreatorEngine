using CreatorEngine;
using System.Runtime.InteropServices;

// Source fixture only. Execution and trimmed NativeAOT publication are pending.
int checks = 0;
void Check(bool value, string name)
{
    ++checks;
    if (!value)
    {
        throw new Exception(name);
    }
}
void Reject(Action action, string name)
{
    bool rejected = false;
    try
    {
        action();
    }
    catch
    {
        rejected = true;
    }
    Check(rejected, name);
}

InputID graph = new(11, 22);
InputID jumpID = new(33, 44);
var jump = InputSignal.Button(graph, jumpID, 77);
InputFrame Frame(ulong sequence, ulong semanticHash = 80, ulong interfaceHash = 77, uint reason = 0)
{
    NativeInputFrameHeader header = new()
    {
        ABIVersion = 1, SchemaVersion = 1, CompilerVersion = 1,
        Session = Native.Session, Graph = graph, SemanticHash = semanticHash, InterfaceHash = interfaceHash,
        DefinitionGeneration = 1, Sequence = sequence, Begin = (long)sequence, End = (long)sequence + 1,
        StateCount = 1, EventCount = 4
    };
    var states = new[] { new NativeInputState { Signal = jumpID, Value = new() { Type = 0 }, Flags = 6 } };
    var events = new NativeInputEvent[4];
    for (int i = 0; i < events.Length; ++i)
    {
        events[i] = new() { Signal = jumpID, Value = new() { Type = 0, X = i % 2 == 0 ? 1 : 0 },
            Phase = i % 2 == 0 ? 1u : 2u, Reason = reason, Sequence = (ulong)i + sequence * 10, SourceTime = i, EffectiveTime = i };
    }
    return new(header, states, events);
}

Check(Marshal.SizeOf<InputID>() == 16 && Marshal.SizeOf<InputSessionHandle>() == 16, "stable IDs/session layout");
Check(Marshal.SizeOf<NativeInputValue>() == 16 && Marshal.SizeOf<NativeInputState>() == 40, "value/state layout");
Check(Marshal.SizeOf<NativeInputEvent>() == 96 && Marshal.SizeOf<NativeInputFrameHeader>() == 120, "batch layout");
Check(Marshal.SizeOf<NativeInputRequest>() == 72, "request layout");
Check(Marshal.SizeOf<NativeInputDevice>() == 48, "device snapshot layout");
Check(Marshal.OffsetOf<NativeInputEvent>(nameof(NativeInputEvent.Sequence)).ToInt32() == 48, "event sequence offset");
Check(Marshal.OffsetOf<NativeInputFrameHeader>(nameof(NativeInputFrameHeader.Sequence)).ToInt32() == 80,
    "frame sequence offset");
var first = Frame(1);
var state = first.Read(jump);
Check(!state.Value && state.Pressed && state.Released, "same tick press/release preserves flags and final held");
Check(first.Events.Length == 4 && first.ReadEvent(0, jump).Value && !first.ReadEvent(1, jump).Value,
    "ordered typed events are not coalesced");
Check(Frame(2, semanticHash: 999).Read(jump).Pressed, "rebind semantic change preserves generated interface");
Reject(() => Frame(2, interfaceHash: 78).Read(jump), "stale accessor rejected");
Reject(() => first.Read(InputSignal.Float(graph, jumpID, 77)), "wrong typed accessor rejected");
Check(Frame(2, reason: 13).Events[0].Reason == InputCancelReason.InteractionTimeout, "timeout reason ABI mirror");
Reject(() => Frame(2, reason: 14), "unknown reason fails closed");

var component = new Component();
var session = InputSession.For(component);
int calls = 0;
InputSubscription? self = null;
InputSubscription? next = null;
self = session.Subscribe(component, jump, value =>
{
    ++calls;
    self!.Dispose();
    next = session.Subscribe(component, jump, later => { ++calls; });
});
InputRouter.Publish(first);
Check(calls == 1, "unsubscribe immediate and new subscription waits for next batch");
InputRouter.Publish(first);
Check(calls == 1, "duplicate batch ignored");
InputRouter.Publish(Frame(2));
Check(calls == 5, "next frame receives every event");
next!.Dispose();
int surviving = 0;
var throwing = session.Subscribe(component, jump, value => { throw new Exception("fixture"); });
var survivor = session.Subscribe(component, jump, value => { ++surviving; });
InputRouter.Publish(Frame(3));
Check(surviving == 4 && Native.Exceptions == 4, "subscriber exception isolated for each event");
component.Scope.Cancel();
InputRouter.Publish(Frame(4));
Check(surviving == 4, "scope end suppresses callbacks");
var disabled = session.Subscribe(component, jump, value => { ++surviving; });
InputRouter.RemoveOwner(component);
component.Enabled = false;
InputRouter.Publish(Frame(5));
component.Enabled = true;
InputRouter.Publish(Frame(6));
Check(surviving == 4, "disable invalidates subscription across re-enable");
var dead = session.Subscribe(component, jump, value => { ++surviving; });
Native.Alive = false;
InputRouter.Publish(Frame(7));
Check(surviving == 4, "destroyed owner receives no callback");
Native.Alive = true;
InputRouter.Invalidate(Native.Session);
Native.Session = new(1, 2);
Reject(() => session.GetFrame(), "stale session generation rejected");
var replacement = InputSession.For(component);
var reloadToken = replacement.Subscribe(component, jump, value => { ++surviving; });
InputRouter.Reset();
Reject(() => replacement.GetFrame(), "script reload invalidates managed session generation");
reloadToken.Dispose();
Check(first.Read(jump).Pressed, "owned immutable snapshot survives session/reload");
Native.IsGameThread = false;
Reject(() => InputSession.For(component), "off-thread consumer rejected");
Native.IsGameThread = true;
Console.WriteLine($"InputGraph script source fixture checks: {checks}");
