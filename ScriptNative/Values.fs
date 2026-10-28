namespace CreatorEngine

open System
open System.Runtime.InteropServices
open System.Runtime.CompilerServices
open CreatorEngine.Native.reflgen_creator_script_native

[<AttributeUsage(AttributeTargets.Field, AllowMultiple = false)>]
type SerializeFieldAttribute() =
    inherit Attribute()
    member val DisplayName: string = null with get, set

[<AttributeUsage(AttributeTargets.Method, Inherited = false, AllowMultiple = false)>]
type EngineCallableAttribute() =
    inherit Attribute()

type FieldType =
    | Unknown = 0
    | Float = 1
    | Int32 = 2
    | Bool = 3
    | Float3 = 4
    | String = 5
    | Object = 6
    | Float2 = 7
    | AssetLink = 8

[<Struct; StructLayout(LayoutKind.Sequential)>]
type Color4 =
    val mutable R: single
    val mutable G: single
    val mutable B: single
    val mutable A: single
    new (r, g, b, a) = { R = r; G = g; B = b; A = a }
    static member White = Color4(1.0f, 1.0f, 1.0f, 1.0f)
    static member Clear = Color4(1.0f, 1.0f, 1.0f, 0.0f)
    member this.WithAlpha(alpha) = Color4(this.R, this.G, this.B, alpha)
    override this.ToString() = String.Format("({0:0.##}, {1:0.##}, {2:0.##}, {3:0.##})", this.R, this.G, this.B, this.A)
    member internal this.ToNative() =
        let mutable value = Unchecked.defaultof<ce_script_binding_Float4>
        value.x <- this.R
        value.y <- this.G
        value.z <- this.B
        value.w <- this.A
        value
    static member internal FromNative(value: ce_script_binding_Float4) = Color4(value.x, value.y, value.z, value.w)

[<Struct; StructLayout(LayoutKind.Sequential)>]
type Float2 =
    val mutable X: single
    val mutable Y: single
    new (x, y) = { X = x; Y = y }
    static member Zero = Float2(0.0f, 0.0f)
    static member One = Float2(1.0f, 1.0f)
    static member (+) (a: Float2, b: Float2) = Float2(a.X + b.X, a.Y + b.Y)
    static member (-) (a: Float2, b: Float2) = Float2(a.X - b.X, a.Y - b.Y)
    static member (*) (a: Float2, value: single) = Float2(a.X * value, a.Y * value)
    member this.Length = MathF.Sqrt(this.X * this.X + this.Y * this.Y)
    override this.ToString() = String.Format("({0:0.###}, {1:0.###})", this.X, this.Y)
    member internal this.ToNative() =
        let mutable value = Unchecked.defaultof<ce_script_binding_Float2>
        value.x <- this.X
        value.y <- this.Y
        value
    static member internal FromNative(value: ce_script_binding_Float2) = Float2(value.x, value.y)

[<Struct; StructLayout(LayoutKind.Sequential); CustomEquality; NoComparison>]
type ObjectHandle =
    val Index: uint32
    val Generation: uint32
    new (index, generation) = { Index = index; Generation = generation }
    static member Invalid = Unchecked.defaultof<ObjectHandle>
    member this.IsValid = this.Generation <> 0u
    [<MethodImpl(MethodImplOptions.AggressiveInlining)>]
    member internal this.ToNative() =
        let mutable value = Unchecked.defaultof<ScriptObjectHandle>
        value.index <- this.Index
        value.generation <- this.Generation
        value
    static member internal FromNative(value: ScriptObjectHandle) = ObjectHandle(value.index, value.generation)
    override this.Equals(other) =
        match other with
        | :? ObjectHandle as handle -> this.Index = handle.Index && this.Generation = handle.Generation
        | _ -> false
    override this.GetHashCode() = HashCode.Combine(this.Index, this.Generation)
    override this.ToString() = if this.IsValid then $"#{this.Index}:{this.Generation}" else "#invalid"
    interface IEquatable<ObjectHandle> with
        member this.Equals(other) = this.Index = other.Index && this.Generation = other.Generation

[<Struct; StructLayout(LayoutKind.Sequential)>]
type Float3 =
    val mutable X: single
    val mutable Y: single
    val mutable Z: single
    new (x, y, z) = { X = x; Y = y; Z = z }
    static member Zero = Float3(0.0f, 0.0f, 0.0f)
    static member One = Float3(1.0f, 1.0f, 1.0f)
    static member Up = Float3(0.0f, 1.0f, 0.0f)
    static member Right = Float3(1.0f, 0.0f, 0.0f)
    static member Forward = Float3(0.0f, 0.0f, 1.0f)
    static member (+) (a: Float3, b: Float3) = Float3(a.X + b.X, a.Y + b.Y, a.Z + b.Z)
    static member (-) (a: Float3, b: Float3) = Float3(a.X - b.X, a.Y - b.Y, a.Z - b.Z)
    static member (~-) (a: Float3) = Float3(-a.X, -a.Y, -a.Z)
    static member (*) (a: Float3, value: single) = Float3(a.X * value, a.Y * value, a.Z * value)
    static member (*) (value: single, a: Float3) = a * value
    static member (/) (a: Float3, value: single) = Float3(a.X / value, a.Y / value, a.Z / value)
    member this.LengthSquared = this.X * this.X + this.Y * this.Y + this.Z * this.Z
    member this.Length = MathF.Sqrt(this.LengthSquared)
    member this.Normalized =
        let squared = this.LengthSquared
        if squared < 1e-12f then this else this * (1.0f / MathF.Sqrt(squared))
    static member Dot(a: Float3, b: Float3) = a.X * b.X + a.Y * b.Y + a.Z * b.Z
    static member Cross(a: Float3, b: Float3) =
        Float3(a.Y * b.Z - a.Z * b.Y, a.Z * b.X - a.X * b.Z, a.X * b.Y - a.Y * b.X)
    static member Distance(a: Float3, b: Float3) = (a - b).Length
    static member Lerp(a: Float3, b: Float3, t: single) = a + (b - a) * t
    override this.ToString() = String.Format("({0:0.###}, {1:0.###}, {2:0.###})", this.X, this.Y, this.Z)
    member internal this.ToNative() =
        let mutable value = Unchecked.defaultof<ce_script_vector3>
        value.x <- this.X
        value.y <- this.Y
        value.z <- this.Z
        value
    static member internal FromNative(value: ce_script_vector3) = Float3(value.x, value.y, value.z)

[<Struct; StructLayout(LayoutKind.Sequential)>]
type Quaternion =
    val mutable X: single
    val mutable Y: single
    val mutable Z: single
    val mutable W: single
    new (x, y, z, w) = { X = x; Y = y; Z = z; W = w }
    static member Identity = Quaternion(0.0f, 0.0f, 0.0f, 1.0f)
    static member CreateFromAxisAngle(axis: Float3, angle: single) =
        let normalized = axis.Normalized
        let half = angle * 0.5f
        let sine = MathF.Sin(half)
        Quaternion(normalized.X * sine, normalized.Y * sine, normalized.Z * sine, MathF.Cos(half))
    static member CreateFromYawPitchRoll(yaw: single, pitch: single, roll: single) =
        let sy, cy = MathF.Sin(yaw * 0.5f), MathF.Cos(yaw * 0.5f)
        let sp, cp = MathF.Sin(pitch * 0.5f), MathF.Cos(pitch * 0.5f)
        let sr, cr = MathF.Sin(roll * 0.5f), MathF.Cos(roll * 0.5f)
        Quaternion(cy * sp * cr + sy * cp * sr, sy * cp * cr - cy * sp * sr,
                   cy * cp * sr - sy * sp * cr, cy * cp * cr + sy * sp * sr)
    static member LookRotation(forward: Float3, up: Float3) =
        let f = forward.Normalized
        if f.LengthSquared < 1e-12f then Quaternion.Identity
        else
            let mutable r = Float3.Cross(up, f)
            if r.LengthSquared < 1e-12f then
                r <- Float3.Cross((if MathF.Abs(f.Y) > 0.99f then Float3(0.0f, 0.0f, 1.0f) else Float3.Up), f)
            r <- r.Normalized
            let u = Float3.Cross(f, r)
            let trace = r.X + u.Y + f.Z
            if trace > 0.0f then
                let s = MathF.Sqrt(trace + 1.0f) * 2.0f
                Quaternion((u.Z - f.Y) / s, (f.X - r.Z) / s, (r.Y - u.X) / s, s * 0.25f)
            elif r.X > u.Y && r.X > f.Z then
                let s = MathF.Sqrt(1.0f + r.X - u.Y - f.Z) * 2.0f
                Quaternion(s * 0.25f, (u.X + r.Y) / s, (f.X + r.Z) / s, (u.Z - f.Y) / s)
            elif u.Y > f.Z then
                let s = MathF.Sqrt(1.0f + u.Y - r.X - f.Z) * 2.0f
                Quaternion((u.X + r.Y) / s, s * 0.25f, (f.Y + u.Z) / s, (f.X - r.Z) / s)
            else
                let s = MathF.Sqrt(1.0f + f.Z - r.X - u.Y) * 2.0f
                Quaternion((f.X + r.Z) / s, (f.Y + u.Z) / s, s * 0.25f, (r.Y - u.X) / s)
    static member LookRotation(forward: Float3) = Quaternion.LookRotation(forward, Float3.Up)
    member this.Conjugate = Quaternion(-this.X, -this.Y, -this.Z, this.W)
    member this.Normalized =
        let squared = this.X * this.X + this.Y * this.Y + this.Z * this.Z + this.W * this.W
        if squared < 1e-12f then Quaternion.Identity
        else
            let scale = 1.0f / MathF.Sqrt(squared)
            Quaternion(this.X * scale, this.Y * scale, this.Z * scale, this.W * scale)
    static member Slerp(a: Quaternion, b: Quaternion, t: single) =
        let mutable other = b
        let mutable dot = a.X * b.X + a.Y * b.Y + a.Z * b.Z + a.W * b.W
        if dot < 0.0f then
            other <- Quaternion(-b.X, -b.Y, -b.Z, -b.W)
            dot <- -dot
        if dot > 0.9995f then
            Quaternion(a.X + (other.X - a.X) * t, a.Y + (other.Y - a.Y) * t,
                       a.Z + (other.Z - a.Z) * t, a.W + (other.W - a.W) * t).Normalized
        else
            let theta = MathF.Acos(dot)
            let sine = MathF.Sin(theta)
            let wa = MathF.Sin((1.0f - t) * theta) / sine
            let wb = MathF.Sin(t * theta) / sine
            Quaternion(a.X * wa + other.X * wb, a.Y * wa + other.Y * wb,
                       a.Z * wa + other.Z * wb, a.W * wa + other.W * wb)
    static member (*) (a: Quaternion, b: Quaternion) =
        Quaternion(a.W * b.X + a.X * b.W + a.Y * b.Z - a.Z * b.Y,
                   a.W * b.Y - a.X * b.Z + a.Y * b.W + a.Z * b.X,
                   a.W * b.Z + a.X * b.Y - a.Y * b.X + a.Z * b.W,
                   a.W * b.W - a.X * b.X - a.Y * b.Y - a.Z * b.Z)
    static member (*) (q: Quaternion, value: Float3) =
        let axis = Float3(q.X, q.Y, q.Z)
        let t = Float3.Cross(axis, value) * 2.0f
        value + t * q.W + Float3.Cross(axis, t)
    member internal this.ToNative() =
        let mutable value = Unchecked.defaultof<ce_script_binding_Float4>
        value.x <- this.X; value.y <- this.Y; value.z <- this.Z; value.w <- this.W
        value
    static member internal FromNative(value: ce_script_binding_Float4) = Quaternion(value.x, value.y, value.z, value.w)
    override this.ToString() = String.Format("({0:0.###}, {1:0.###}, {2:0.###}, {3:0.###})", this.X, this.Y, this.Z, this.W)
