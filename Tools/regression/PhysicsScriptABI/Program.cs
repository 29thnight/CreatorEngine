using System.Reflection;
using System.Runtime.InteropServices;
using CreatorEngine;

int checks = 0;
void Check(bool value, string name)
{
    if (!value) throw new InvalidOperationException(name);
    ++checks;
}

Check(IntPtr.Size == 8, "x64 required");
Check(Marshal.SizeOf<PhysicsBodyState>() == 60, "body state layout");
Check(Marshal.SizeOf<PhysicsShapeState>() == 72, "shape state layout");
Check(Marshal.SizeOf<PhysicsHit>() == 64, "hit layout");
Check(Marshal.OffsetOf<PhysicsHit>(nameof(PhysicsHit.ComponentId)).ToInt32() == 8, "component offset");
Check(Marshal.OffsetOf<PhysicsHit>(nameof(PhysicsHit.LayerId)).ToInt32() == 24, "layer offset");
Check(Marshal.OffsetOf<PhysicsHit>(nameof(PhysicsHit.Point)).ToInt32() == 32, "point offset");
Check((int)PhysicsBodyKind.Dynamic == 2 && (int)PhysicsBodyKind.Kinematic == 1, "body kind ABI");
Check((int)PhysicsForceMode.Impulse == 1 && (int)PhysicsForceMode.Acceleration == 2, "force mode ABI");
Check((int)PhysicsError.DuplicateCommand == 11, "error ABI");

Check(Marshal.SizeOf<CharacterMovementState>() == 88, "character layout");
Check(Marshal.OffsetOf<CharacterMovementState>(nameof(CharacterMovementState.Tick)).ToInt32() == 56, "character tick offset");
Check((uint)CharacterCollisionFlags.Simulating == 8 && (uint)CharacterCollisionFlags.Below == 4, "character flags ABI");

var assembly = typeof(Physics).Assembly;
var native = assembly.GetType("CreatorEngine.Native", true)!;
var version = native.GetField("ExpectedVersion", BindingFlags.Static | BindingFlags.NonPublic | BindingFlags.Public)!;
Check((int)version.GetRawConstantValue()! == 31, "ABI version");
var summary = assembly.GetType("CreatorEngine.NativePhysicsQueryResult", true)!;
Check(Marshal.SizeOf(summary) == 12, "query summary layout");

// Unbound calls must reset outputs and report wrong phase, including capacity discovery.
var result = Physics.Raycast(default, default, new Float3(0, -1, 0), 10, Span<PhysicsHit>.Empty);
Check(result.Error == PhysicsError.WrongPhase && result.Written == 0 && result.RequiredCapacity == 0 && !result.Truncated,
    "unbound query failure contract");
Check(PhysicsBodyComponent.Find(default, 1) is null, "unbound body identity lookup");
var body = new PhysicsBodyComponent();
Check(body.ReadState(out var state) == PhysicsError.WrongPhase && state.Mass == 0, "unbound state reset");
Check(body.SetVelocity(default) == PhysicsError.WrongPhase, "unbound mutation failure");
try { ((Component)body).Enabled = false; throw new InvalidOperationException("native enable silently accepted"); }
catch (NotSupportedException) { ++checks; }
Check(CharacterMovementComponent.Find(default, 1) is null, "unbound character identity lookup");
var character = new CharacterMovementComponent();
Check(character.ReadState(out var movement) == PhysicsError.WrongPhase && movement.Tick == 0 && !movement.Simulating,
    "unbound character state reset");
Check(character.SetDesiredVelocity(new Float3(1, 0, 0)) == PhysicsError.WrongPhase, "unbound character velocity");
Check(character.Teleport(default) == PhysicsError.WrongPhase, "unbound character teleport");
try { ((Component)character).Enabled = false; throw new InvalidOperationException("native character enable silently accepted"); }
catch (NotSupportedException) { ++checks; }
Check(Marshal.OffsetOf<CharacterMovementState>(nameof(CharacterMovementState.ForcedRemaining)).ToInt32() == 80, "force remaining offset");
Check(character.Jump() == PhysicsError.WrongPhase, "unbound jump");
Check(character.ForceVelocity(default, 1) == PhysicsError.WrongPhase, "unbound force");
Check(character.CancelForcedVelocity() == PhysicsError.WrongPhase, "unbound cancel");
Console.WriteLine($"PHYSICS_SCRIPT_ABI_OK checks={checks} version=31");
