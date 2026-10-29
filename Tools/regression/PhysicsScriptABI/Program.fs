open System
open System.IO
open System.Reflection
open System.Runtime.InteropServices
open System.Text.Json
open CreatorEngine
open CreatorEngine.Native.reflgen_creator_script_native

let mutable checks = 0
let check value name =
    if not value then invalidOp name
    checks <- checks + 1

check (IntPtr.Size = 8) "x64 required"
check (Marshal.SizeOf<PhysicsBodyState>() = 60) "body state layout"
check (Marshal.SizeOf<PhysicsShapeState>() = 88) "shape state layout"
check (Marshal.SizeOf<PhysicsHit>() = 64) "hit layout"
for name, offset in ["ComponentId", 8; "LayerId", 24; "Point", 32] do
    check (Marshal.OffsetOf<PhysicsHit>(name).ToInt32() = offset) ("hit " + name + " offset")
check (int PhysicsBodyKind.Dynamic = 2 && int PhysicsBodyKind.Kinematic = 1) "body kind ABI"
check (int PhysicsForceMode.Impulse = 1 && int PhysicsForceMode.Acceleration = 2) "force mode ABI"
check (int PhysicsError.DuplicateCommand = 11) "error ABI"
check (Marshal.SizeOf<CharacterMovementState>() = 88) "character layout"
check (Marshal.OffsetOf<CharacterMovementState>("Tick").ToInt32() = 56) "character tick offset"
check (uint32 CharacterCollisionFlags.Simulating = 8u && uint32 CharacterCollisionFlags.Below = 4u) "character flags ABI"
check (ScriptApiVersion = 49) "current host ABI version includes callbacks"
let arguments = Environment.GetCommandLineArgs()
if arguments.Length <> 2 then invalidArg "args" "Pass the generated native ABI manifest path."
let document = JsonDocument.Parse(File.ReadAllText(arguments[1]))
let table = document.RootElement.GetProperty("tables")[0]
let fields = table.GetProperty("fields").EnumerateArray() |> Seq.map (fun field -> field.GetProperty("name").GetString(), field) |> dict
let offset name = fields[name].GetProperty("offset").GetInt32()
check (table.GetProperty("version").GetInt32() = ScriptApiVersion && table.GetProperty("size").GetInt32() = ScriptApiSize) "generated API constants and native manifest"
check (offset "version" = 0 && offset "structSize" = 4) "API table header layout"
for previous, current in [
    "Asset_ListRoots", "Asset_RequestTyped"; "Asset_RequestTyped", "Asset_TryAcquireTyped"
    "Asset_TryAcquireTyped", "Body_Remove"; "Body_Remove", "Body_ShapeRole"; "Body_ShapeRole", "Camera_NotifyCameraCut"
    "Camera_NotifyCameraCut", "Simulation_Status"; "Simulation_Status", "Simulation_Submit"; "Simulation_Submit", "Simulation_Read"
    "Simulation_Read", "ExtraComponent_Exists"; "ExtraComponent_Exists", "ExtraComponent_GetEnabled"; "ExtraComponent_GetEnabled", "ExtraComponent_SetEnabled"
    "Decal_SetFloat", "NativeReflection_Execute"; "NativeReflection_Execute", "NativeReflection_Read"; "NativeReflection_Read", "NativeReflection_Release"
    "NativeReflection_Release", "NativeCallback_BeginGeneration"; "NativeCallback_BeginGeneration", "NativeCallback_Register"
    "NativeCallback_Register", "NativeCallback_Disable"; "NativeCallback_Disable", "NativeCallback_TakeRetired"; "NativeCallback_TakeRetired", "NativeCallback_EndGeneration"] do
    check (offset current = offset previous + IntPtr.Size) (current + " append-only slot")
check (ScriptApiSize = offset "NativeCallback_EndGeneration" + IntPtr.Size) "callback tail ends table"
for name, count, result in ["Body_Remove", 2, "int"; "Body_ShapeRole", 4, "int"; "Camera_NotifyCameraCut", 1, "void"] do
    let signature = fields[name].GetProperty("type").GetProperty("pointee")
    check (signature.GetProperty("parameters").GetArrayLength() = count && signature.GetProperty("result").GetProperty("canonical").GetString() = result) (name + " signature")
check (Marshal.SizeOf<SimulationStatus>() = 32 && Marshal.SizeOf<SimulationSnapshot>() = 32) "simulation value layouts"
let assembly = typeof<PhysicsHit>.Assembly
check (Marshal.SizeOf(assembly.GetType("CreatorEngine.NativePhysicsQueryResult", true)) = 12) "query summary layout"
check (Marshal.SizeOf<PhysicsQueryRequest>() = 48) "batch request layout"
check (Marshal.OffsetOf<PhysicsQueryRequest>("Offset@").ToInt32() = 40) "batch offset layout"
check (Marshal.SizeOf<PhysicsBatchResult>() = 16) "batch result layout"
let owner = Unchecked.defaultof<Entity>
let zero = Unchecked.defaultof<Float3>
let batchRequests = [| PhysicsQueryRequest.Raycast(zero, Float3(0.0f, -1.0f, 0.0f), 10.0f, 0, 0) |]
let batchResults = Array.zeroCreate<PhysicsBatchResult> 1
check (Physics.QueryBatch(owner, ReadOnlySpan(batchRequests), Span<PhysicsHit>.Empty, Span(batchResults)) = PhysicsError.WrongPhase) "unbound batch phase contract"
let result = Physics.Raycast(owner, zero, Float3(0.0f, -1.0f, 0.0f), 10.0f, Span<PhysicsHit>.Empty)
check (result.Error = PhysicsError.WrongPhase && result.Written = 0 && result.RequiredCapacity = 0 && not result.Truncated) "unbound query failure contract"
check (obj.ReferenceEquals(PhysicsBodyComponent.Find(owner, 1UL), null)) "unbound body identity lookup"
let body = Activator.CreateInstance(typeof<PhysicsBodyComponent>, BindingFlags.Instance ||| BindingFlags.NonPublic, null, [| box Unchecked.defaultof<ObjectHandle>; box 0UL |], null) :?> PhysicsBodyComponent
let mutable state = Unchecked.defaultof<PhysicsBodyState>
check (body.ReadState(&state) = PhysicsError.WrongPhase && state.Mass = 0.0f) "unbound state reset"
check (body.SetShapeRole(1u, ShapeRoles.Attack) = PhysicsError.WrongPhase) "unbound shape role mutation"
check (body.Remove() = PhysicsError.WrongPhase) "unbound body removal failure"
check (body.SetVelocity(zero) = PhysicsError.WrongPhase) "unbound mutation failure"
let bodyEnableRejected =
    try
        body.Enabled <- false
        false
    with :? NotSupportedException -> true
check bodyEnableRejected "native enable rejected"
check (obj.ReferenceEquals(CharacterMovementComponent.Find(owner, 1UL), null)) "unbound character identity lookup"
let character = Activator.CreateInstance(typeof<CharacterMovementComponent>, BindingFlags.Instance ||| BindingFlags.NonPublic, null, [| box Unchecked.defaultof<ObjectHandle>; box 0UL |], null) :?> CharacterMovementComponent
let mutable movement = Unchecked.defaultof<CharacterMovementState>
check (character.ReadState(&movement) = PhysicsError.WrongPhase && movement.Tick = 0UL && not movement.Simulating) "unbound character state reset"
check (character.SetDesiredVelocity(Float3(1.0f, 0.0f, 0.0f)) = PhysicsError.WrongPhase) "unbound character velocity"
let mutable planar = zero
check (CharacterMovementComponent.CreatePlanarVelocity(Float3(0.25f, 9.0f, 0.0f), 4.0f, &planar) = PhysicsError.None && planar.X = 1.0f && planar.Y = 0.0f && planar.Z = 0.0f) "analog input and vertical projection"
for input in [Float3(1.0f, 0.0f, 1.0f); Float3(Single.MaxValue, 0.0f, Single.MaxValue)] do
    check (CharacterMovementComponent.CreatePlanarVelocity(input, 4.0f, &planar) = PhysicsError.None && abs (planar.Length - 4.0f) < 1e-5f) "planar safe speed cap"
check (CharacterMovementComponent.CreatePlanarVelocity(zero, 4.0f, &planar) = PhysicsError.None && planar.LengthSquared = 0.0f) "released input requests stop"
check (CharacterMovementComponent.CreatePlanarVelocity(Float3(1.0f, Single.NaN, 0.0f), 4.0f, &planar) = PhysicsError.InvalidArgument && planar.LengthSquared = 0.0f) "nonfinite input reset"
check (character.SetPlanarInput(zero, -1.0f) = PhysicsError.InvalidArgument) "invalid speed rejected"
check (character.SetPlanarInput(Float3(1.0f, 0.0f, 0.0f), 4.0f) = PhysicsError.WrongPhase) "valid planar input respects native phase"
check (character.Teleport(zero) = PhysicsError.WrongPhase) "unbound character teleport"
let characterEnableRejected =
    try
        character.Enabled <- false
        false
    with :? NotSupportedException -> true
check characterEnableRejected "native character enable rejected"
check (Marshal.OffsetOf<CharacterMovementState>("ForcedRemaining").ToInt32() = 80) "force remaining offset"
check (character.Jump() = PhysicsError.WrongPhase) "unbound jump"
check (character.ForceVelocity(zero, 1.0) = PhysicsError.WrongPhase) "unbound force"
check (character.CancelForcedVelocity() = PhysicsError.WrongPhase) "unbound cancel"
Console.WriteLine($"PHYSICS_SCRIPT_ABI_OK checks={checks} version={ScriptApiVersion}")
let contactType = assembly.GetType("CreatorEngine.NativeContact", true)
check (Marshal.SizeOf(contactType) = 128) "contact ABI size"
check (Marshal.OffsetOf(contactType, "Tick").ToInt32() = 96) "contact ABI tick offset"
Console.WriteLine($"CONTACT_ABI_OK checks={checks}")

document.Dispose()
