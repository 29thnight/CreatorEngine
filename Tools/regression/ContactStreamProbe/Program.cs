using CreatorEngine;
using System.Runtime.InteropServices;

int checks = 0;
void Check(bool test, string name) { ++checks; if (!test) throw new Exception(name); }
void Reject(Action action, string name) { bool failed = false; try { action(); } catch { failed = true; } Check(failed, name); }
var source = new ObjectHandle(1, 1);
var target = new ObjectHandle(2, 1);
var component = new Component();
ContactRouter.BindRole(source, 11, 1, ShapeRoles.Attack, component.Scope);
ContactRouter.BindRole(target, 22, 2, ShapeRoles.Hurt, component.Scope);
var stream = ContactRouter.Observe(component, source, ShapeRoles.Attack, ShapeRoles.Hurt, ContactPhases.All, 4);
var reverse = ContactRouter.Observe(component, target, ShapeRoles.Hurt, ShapeRoles.Attack, ContactPhases.Begin, 4);
var unrelated = ContactRouter.Observe(component, new(3, 1), ShapeRoles.Attack, ShapeRoles.Hurt, ContactPhases.All, 4);
var value = new NativeContact { First = new() { Owner=source, Component=11, Shape=1, BodyGeneration=3 }, Second = new() { Owner=target, Component=22, Shape=2, BodyGeneration=9 }, Tick=100, Kind=0 };
Check(Marshal.SizeOf<ContactEndpoint>() == 48 && Marshal.SizeOf<NativeContact>() == 128, "native ABI layout");
ContactRouter.Route(value);
Check(stream.Read().Length==1 && reverse.Read().Length==1 && unrelated.Read().IsEmpty,"indexed oriented routing");
Check(stream.Read()[0].OtherEntity.Handle==target && reverse.Read()[0].OtherEntity.Handle==source,"endpoint orientation");
Check(stream.Read()[0].SelfShapeId==1 && stream.Read()[0].OtherShapeId==2,"shape identity");
Check(stream.Read()[0].SelfBodyGeneration==3 && stream.Read()[0].OtherBodyGeneration==9 && reverse.Read()[0].SelfBodyGeneration==9, "oriented body generation identity");
value.Tick=101; value.Kind=1; ContactRouter.Route(value);
value.Tick=102; value.Kind=2; ContactRouter.Route(value);
Check(stream.Read().Length==3 && stream.Read()[2].Tick==102 && stream.Read()[1].Phase==ContactPhases.Persist,"catchup ticks retained");
Check(reverse.Read().Length==1,"phase filter");
ContactRouter.EndFrame();
Check(stream.Read().IsEmpty && stream.RequiredCapacity==0,"no replay on no-step frame");
value.Kind=3; ContactRouter.Route(value);
Check(stream.Read()[0].Sensor && stream.Read()[0].Phase==ContactPhases.Begin,"sensor begin");
ContactRouter.EndFrame();
value.First.Owner=new(1,2); ContactRouter.Route(value);
Check(stream.Read().IsEmpty,"entity generation isolation");
value.First.Owner=source; value.First.Component=99; ContactRouter.Route(value);
Check(stream.Read().IsEmpty,"component replacement isolation");
value.First.Component=11; value.First.Shape=99; ContactRouter.Route(value);
Check(stream.Read().IsEmpty,"shape isolation");
value.First.Shape=1; component.Enabled=false; ContactRouter.Route(value);
Check(stream.Read().IsEmpty,"disabled subscriber");
component.Enabled=true; value.Kind=0;
for(int i=0;i<5;i++) ContactRouter.Route(value);
Check(stream.Overflowed && stream.RequiredCapacity==5,"bounded overflow reported");
Reject(()=>{stream.Read();},"partial gameplay results rejected");
ContactRouter.EndFrame();
for(int i=0;i<10;i++){ContactRouter.Route(value); ContactRouter.EndFrame();}
long before=GC.GetAllocatedBytesForCurrentThread();
for(int i=0;i<10000;i++){ContactRouter.Route(value); var contacts=stream.Read(); if(contacts.Length!=1) throw new Exception("lost contact"); ContactRouter.EndFrame();}
long bytes=GC.GetAllocatedBytesForCurrentThread()-before;
Check(bytes==0,"steady routing/read/clear allocation zero");
component.Scope.Cancel();
Reject(()=>{stream.Read();},"scope disposes stream");
ContactRouter.Route(value);
ContactRouter.EndFrame();
Reject(()=>ContactRouter.Observe(component,source,default,ShapeRoles.Hurt,ContactPhases.All,4),"invalid role");
Check(Marshal.OffsetOf<NativeContact>(nameof(NativeContact.Tick)).ToInt32()==96,"native tick offset");
var authoredComponent = new Component();
var authoredStream = ContactRouter.Observe(authoredComponent, source, ShapeRoles.Attack, ShapeRoles.Hurt, ContactPhases.All, 4);
value.First.Role = ShapeRoles.Attack.Id;
value.Second.Role = ShapeRoles.Hurt.Id;
ContactRouter.Route(value);
Check(authoredStream.Read().Length == 1, "stored roles route without explicit binding");
ContactRouter.EndFrame();
long authoredBefore = GC.GetAllocatedBytesForCurrentThread();
for (int i = 0; i < 10000; ++i)
{
    ContactRouter.Route(value);
    if (authoredStream.Read().Length != 1) throw new Exception("Lost authored contact");
    ContactRouter.EndFrame();
}
Check(GC.GetAllocatedBytesForCurrentThread() == authoredBefore, "stored role steady allocation zero");
value.First.Role = ShapeRoles.Hurt.Id;
ContactRouter.Route(value);
Check(authoredStream.Read().IsEmpty, "changed stored role isolates previous subscription");
ContactRouter.EndFrame();
authoredComponent.Scope.Cancel();
Reject(() => { authoredStream.Read(); }, "stored role stream scope cleanup");
var lateComponent = new Component();
var late = ContactRouter.Observe(lateComponent, source, ShapeRoles.Attack, ShapeRoles.Hurt, ContactPhases.All, 4);
var beginOnly = ContactRouter.Observe(lateComponent, source, ShapeRoles.Attack, ShapeRoles.Hurt, ContactPhases.Begin, 4);
value.First.Role = ShapeRoles.Attack.Id;
value.Kind = 5;
ContactRouter.Route(value);
Check(late.Read().Length == 1 && late.Read()[0].Sensor && late.Read()[0].Phase == ContactPhases.Begin,
    "initial sensor overlap seeds Begin from first Persist");
Check(beginOnly.Read().Length == 1, "Begin-only initial overlap");
ContactRouter.EndFrame();
ContactRouter.Route(value);
Check(late.Read().Length == 1 && late.Read()[0].Phase == ContactPhases.Persist && beginOnly.Read().IsEmpty,
    "sensor Persist and no repeated initial Begin");
ContactRouter.EndFrame();
long sensorBefore = GC.GetAllocatedBytesForCurrentThread();
for (int i = 0; i < 10000; ++i) { ContactRouter.Route(value); ContactRouter.EndFrame(); }
Check(GC.GetAllocatedBytesForCurrentThread() == sensorBefore, "sensor steady allocation zero");
value.Kind = 4;
ContactRouter.Route(value);
Check(late.Read()[0].Phase == ContactPhases.End && beginOnly.Read().IsEmpty, "sensor End phase filter");
ContactRouter.EndFrame();
value.Kind = 5;
ContactRouter.Route(value);
Check(beginOnly.Read().Length == 1, "End removes initial overlap state for re-entry");
ContactRouter.EndFrame();
value.First.BodyGeneration++;
ContactRouter.Route(value);
Check(beginOnly.Read().Length == 1, "sensor body generation isolation");
ContactRouter.EndFrame();
value.First.BodySlot++;
ContactRouter.Route(value);
Check(beginOnly.Read().Length == 1, "sensor same generation different body slot isolation");
ContactRouter.EndFrame();
lateComponent.Scope.Cancel();
var groupedComponent = new Component();
var grouped = ContactRouter.Observe(groupedComponent, source, ShapeRoles.Attack, ShapeRoles.Hurt,
    ContactPhases.All, 4, ContactGrouping.SensorTargets);
value.Kind = 3;
value.Tick = 200;
value.Second.Shape = 2;
ContactRouter.Route(value);
value.Second.Shape = 3;
ContactRouter.Route(value);
Check(grouped.Read().Length == 1 && grouped.Read()[0].Phase == ContactPhases.Begin,
    "two sensor shapes produce one target Begin");
ContactRouter.EndFrame();
value.Kind = 5;
value.Tick = 201;
ContactRouter.Route(value);
value.Second.Shape = 2;
ContactRouter.Route(value);
Check(grouped.Read().Length == 1 && grouped.Read()[0].Phase == ContactPhases.Persist,
    "target Persist once per fixed tick");
value.Tick = 202;
ContactRouter.Route(value);
Check(grouped.Read().Length == 2 && grouped.Read()[1].Tick == 202, "target catchup ticks preserved");
ContactRouter.EndFrame();
value.Kind = 4;
ContactRouter.Route(value);
Check(grouped.Read().IsEmpty, "partial target exit does not emit End");
value.Second.Shape = 3;
ContactRouter.Route(value);
Check(grouped.Read().Length == 1 && grouped.Read()[0].Phase == ContactPhases.End,
    "last target pair exit emits End");
ContactRouter.EndFrame();
ContactRouter.Route(value);
Check(grouped.Read().IsEmpty, "unknown target End ignored");
value.Kind = 5;
value.Tick = 203;
ContactRouter.Route(value);
Check(grouped.Read()[0].Phase == ContactPhases.Begin, "late grouped target seeds Begin");
ContactRouter.EndFrame();
value.Second.Owner = new(2, 2);
value.Tick = 204;
ContactRouter.Route(value);
Check(grouped.Read()[0].OtherEntity.Handle == value.Second.Owner && grouped.Read()[0].Phase == ContactPhases.Begin,
    "grouped target entity generation isolation");
ContactRouter.EndFrame();
for (int i = 0; i < 10; ++i) { ++value.Tick; ContactRouter.Route(value); ContactRouter.EndFrame(); }
long groupedBefore = GC.GetAllocatedBytesForCurrentThread();
for (int i = 0; i < 10000; ++i)
{
    ++value.Tick;
    ContactRouter.Route(value);
    if (grouped.Read().Length != 1) throw new Exception("Lost grouped contact");
    ContactRouter.EndFrame();
}
Check(GC.GetAllocatedBytesForCurrentThread() == groupedBefore, "grouped steady allocation zero");
ContactRouter.EndFrame();
value.Kind = 4;
ContactRouter.Route(value);
ContactRouter.EndFrame();
value.Kind = 3;
value.Second.Owner = target;
value.Tick++;
for (uint shape = 10; shape < 14; ++shape)
{
    value.Second.Shape = shape;
    ContactRouter.Route(value);
}
Check(grouped.Overflowed, "active pair capacity bounds grouped target state");
Reject(() => { grouped.Read(); }, "grouped overflow rejects partial results");
ContactRouter.EndFrame();
Reject(() => ContactRouter.Observe(groupedComponent, source, ShapeRoles.Attack, ShapeRoles.Hurt,
    ContactPhases.All, 4, (ContactGrouping)99), "invalid grouping rejected");
groupedComponent.Scope.Cancel();
Reject(() => { grouped.Read(); }, "grouped scope cleanup");
var suspendedComponent = new Component();
var suspended = ContactRouter.Observe(suspendedComponent, source, ShapeRoles.Attack, ShapeRoles.Hurt, ContactPhases.All, 4);
var suspendedTargets = ContactRouter.Observe(suspendedComponent, source, ShapeRoles.Attack, ShapeRoles.Hurt,
    ContactPhases.All, 4, ContactGrouping.SensorTargets);
var suspendedBegin = ContactRouter.Observe(suspendedComponent, source, ShapeRoles.Attack, ShapeRoles.Hurt, ContactPhases.Begin, 4);
var independentComponent = new Component();
var independent = ContactRouter.Observe(independentComponent, source, ShapeRoles.Attack, ShapeRoles.Hurt, ContactPhases.All, 4);
value.Kind = 3;
value.Second.Shape = 2;
++value.Tick;
ContactRouter.Route(value);
suspendedComponent.Enabled = false;
Check(suspended.Read().IsEmpty && suspendedTargets.Read().IsEmpty && suspendedBegin.Read().IsEmpty,
    "disable clears pending events before frame end");
Check(independent.Read().Length == 1, "disable isolates subscriber sharing route");
value.Kind = 4;
++value.Tick;
ContactRouter.Route(value);
Check(suspended.Read().IsEmpty, "disabled End not queued");
ContactRouter.EndFrame();
value.Kind = 3;
++value.Tick;
ContactRouter.Route(value);
Check(suspended.Read().IsEmpty, "disabled Begin not queued");
ContactRouter.EndFrame();
suspendedComponent.Enabled = true;
Check(suspended.Read().IsEmpty, "enable does not replay disabled history");
value.Kind = 5;
++value.Tick;
ContactRouter.Route(value);
Check(suspended.Read().Length == 1 && suspended.Read()[0].Phase == ContactPhases.Begin &&
    suspendedTargets.Read().Length == 1 && suspendedTargets.Read()[0].Phase == ContactPhases.Begin &&
    suspendedBegin.Read().Length == 1, "enable seeds current overlap for pair target and Begin-only streams");
ContactRouter.EndFrame();
++value.Tick;
ContactRouter.Route(value);
Check(suspended.Read()[0].Phase == ContactPhases.Persist && suspendedTargets.Read()[0].Phase == ContactPhases.Persist &&
    suspendedBegin.Read().IsEmpty, "reactivated overlap Begin occurs once");
suspendedComponent.Enabled = false;
suspendedComponent.Enabled = true;
++value.Tick;
ContactRouter.Route(value);
Check(suspended.Read().Length == 1 && suspended.Read()[0].Phase == ContactPhases.Begin,
    "same-frame disable enable resets observation");
ContactRouter.EndFrame();
Check(suspended.Read().IsEmpty, "same-frame reset leaves no touched replay");
suspended.Dispose();
suspendedComponent.Enabled = false;
suspendedComponent.Enabled = true;
suspendedComponent.Scope.Cancel();
Reject(() => { suspendedTargets.Read(); }, "disable preserves Scope until actual cancellation");
independentComponent.Scope.Cancel();
ContactRouter.EndFrame();

// DDOL scene boundaries preserve subscription/role ownership while retiring observations.
var transferred = new Component();
ContactRouter.BindRole(source, 11, 1, ShapeRoles.Attack, transferred.Scope);
var acrossScene = ContactRouter.Observe(transferred, source, ShapeRoles.Attack, ShapeRoles.Hurt, ContactPhases.All, 1);
var acrossTargets = ContactRouter.Observe(transferred, source, ShapeRoles.Attack, ShapeRoles.Hurt, ContactPhases.All, 1, ContactGrouping.SensorTargets);
value.First.Owner = source; value.First.Component = 11; value.First.Shape = 1;
value.First.Role = Guid.Empty; value.Second.Role = ShapeRoles.Hurt.Id;
value.Second.Owner = target; value.Kind = 5;
ContactRouter.Route(value);
Check(acrossScene.Read().Length == 1 && acrossTargets.Read().Length == 1, "source scene overlap tracked");
ContactRouter.Suspend(transferred);
Check(acrossScene.Read().IsEmpty && acrossTargets.Read().IsEmpty && !acrossScene.Overflowed,
    "scene detach clears pending pair target buffers");
Check(transferred.Enabled && !acrossScene.Disposed, "DDOL detach preserves subscriber and Scope");
value.Second.Owner = new(2, 2);
ContactRouter.Route(value);
Check(acrossScene.Read().Length == 1 && acrossScene.Read()[0].Phase == ContactPhases.Begin &&
    acrossTargets.Read().Length == 1 && acrossTargets.Read()[0].Phase == ContactPhases.Begin,
    "destination overlap seeds Begin without stale capacity or target state");
ContactRouter.EndFrame();
for (int epoch = 3; epoch < 100; ++epoch)
{
    ContactRouter.Suspend(transferred);
    value.Second.Owner = new(2, (uint)epoch);
    ContactRouter.Route(value);
    if (acrossScene.Read().Length != 1 || acrossScene.Read()[0].Phase != ContactPhases.Begin)
        throw new Exception("scene-boundary capacity accumulated");
    ContactRouter.EndFrame();
}
Check(!acrossScene.Overflowed && !acrossTargets.Overflowed, "repeated scene boundaries reuse bounded capacity");
transferred.Scope.Cancel();
Reject(() => { acrossScene.Read(); }, "final destruction disposes retained DDOL stream");
Reject(() => { acrossTargets.Read(); }, "final destruction disposes retained target stream");
ContactRouter.EndFrame();

// Solid manifolds retain shape-pair identity even in a sensor-target subscription.
var solidComponent = new Component();
var solidPairs = ContactRouter.Observe(solidComponent, source, ShapeRoles.Attack, ShapeRoles.Hurt, ContactPhases.All, 8);
var mixedTargets = ContactRouter.Observe(solidComponent, source, ShapeRoles.Attack, ShapeRoles.Hurt,
    ContactPhases.All, 8, ContactGrouping.SensorTargets);
var solidReverse = ContactRouter.Observe(solidComponent, target, ShapeRoles.Hurt, ShapeRoles.Attack, ContactPhases.All, 8);
value.First.Owner = source;
value.First.Role = ShapeRoles.Attack.Id;
value.Second.Owner = target;
value.Second.Role = ShapeRoles.Hurt.Id;
value.Second.Shape = 23;
value.Kind = 0;
value.ContactCount = 2;
value.RequiredContacts = 5;
value.Point = new Float3 { X = 1, Y = 2, Z = 3 };
++value.Tick;
foreach (uint shape in new uint[] { 19, 20 })
{
    value.First.Shape = shape;
    ContactRouter.Route(value);
}
Check(solidPairs.Read().Length == 2 && mixedTargets.Read().Length == 2,
    "same-target solid Begin remains two shape pairs under SensorTargets");
Check(!mixedTargets.Read()[0].Sensor && mixedTargets.Read()[0].SelfShapeId == 19 &&
    mixedTargets.Read()[1].SelfShapeId == 20, "solid shape identity is not a representative target identity");
Check(solidReverse.Read().Length == 2 && solidReverse.Read()[0].OtherShapeId == 19 &&
    solidReverse.Read()[0].SelfShapeId == 23, "solid reverse endpoint orientation");
Check(solidReverse.Read()[0].ContactCount == 2 && solidReverse.Read()[0].RequiredContacts == 5 &&
    solidReverse.Read()[0].Point.X == 1 && solidReverse.Read()[0].Point.Y == 2,
    "solid point snapshot and truncation metadata preserved in reverse routing");
ContactRouter.EndFrame();
value.Kind = 1;
++value.Tick;
foreach (uint shape in new uint[] { 19, 20 })
{
    value.First.Shape = shape;
    ContactRouter.Route(value);
}
Check(mixedTargets.Read().Length == 2 && mixedTargets.Read()[0].Phase == ContactPhases.Persist &&
    mixedTargets.Read()[1].Tick == value.Tick, "solid Persist is not deduplicated per target tick");
ContactRouter.EndFrame();
value.First.Shape = 19;
value.Kind = 2;
ContactRouter.Route(value);
value.First.Shape = 20;
value.Kind = 1;
ContactRouter.Route(value);
Check(mixedTargets.Read().Length == 2 && mixedTargets.Read()[0].Phase == ContactPhases.End &&
    mixedTargets.Read()[1].Phase == ContactPhases.Persist, "partial solid separation preserves End and surviving pair");
ContactRouter.EndFrame();
value.Kind = 2;
ContactRouter.Route(value);
Check(mixedTargets.Read().Length == 1 && mixedTargets.Read()[0].SelfShapeId == 20,
    "last solid separation preserves final shape identity");
ContactRouter.EndFrame();
Check(mixedTargets.Read().IsEmpty, "solid no-step frame does not replay contacts");
solidComponent.Scope.Cancel();
Reject(() => { mixedTargets.Read(); }, "solid stream disposed by Scope");

Console.WriteLine($"CONTACT_STREAM_OK checks={checks} steadyAllocatedBytes={bytes}");
