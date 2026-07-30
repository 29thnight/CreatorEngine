using System.Runtime.InteropServices;

namespace CreatorEngine;

/// <summary>
/// 네이티브 객체를 가리키는 세대 핸들.
///
/// 관리 코드는 네이티브 포인터의 유효성을 알 수 없다. 슬롯이 재사용될 때 세대가
/// 올라가므로, 파괴된 객체를 가리키던 핸들은 다음 접근에서 자동으로 무효 판정된다.
/// (설계 문서 03절)
///
/// 네이티브 <c>ScriptObjectHandle</c>과 필드 배치가 같아야 한다 — blittable이라
/// 마샬링 없이 그대로 오간다.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public readonly struct ObjectHandle : IEquatable<ObjectHandle>
{
    public readonly uint Index;
    public readonly uint Generation;

    public ObjectHandle(uint index, uint generation)
    {
        Index = index;
        Generation = generation;
    }

    /// <summary>세대 0은 "비어 있음"으로 예약한다.</summary>
    public static ObjectHandle Invalid => default;

    public bool IsValid => Generation != 0;

    public bool Equals(ObjectHandle other) => Index == other.Index && Generation == other.Generation;
    public override bool Equals(object? obj) => obj is ObjectHandle other && Equals(other);
    public override int GetHashCode() => HashCode.Combine(Index, Generation);
    public override string ToString() => IsValid ? $"#{Index}:{Generation}" : "#invalid";

    public static bool operator ==(ObjectHandle a, ObjectHandle b) => a.Equals(b);
    public static bool operator !=(ObjectHandle a, ObjectHandle b) => !a.Equals(b);
}

/// <summary>
/// 2성분 벡터. 네이티브 <c>Mathf::Vector2</c>와 배치가 같다.
/// UI 좌표(앵커 위치·크기)와 화면 크기에 쓴다.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public struct Float2(float x, float y)
{
    public float X = x;
    public float Y = y;

    public static Float2 Zero => default;
    public static Float2 One => new(1f, 1f);

    public static Float2 operator +(Float2 a, Float2 b) => new(a.X + b.X, a.Y + b.Y);
    public static Float2 operator -(Float2 a, Float2 b) => new(a.X - b.X, a.Y - b.Y);
    public static Float2 operator *(Float2 a, float s) => new(a.X * s, a.Y * s);

    public readonly float Length => MathF.Sqrt(X * X + Y * Y);

    public override string ToString() => $"({X:0.###}, {Y:0.###})";
}

/// <summary>
/// RGBA 색. 네이티브 <c>Mathf::Color4</c>와 배치가 같고 각 성분은 0~1이다.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public struct Color4(float r, float g, float b, float a)
{
    public float R = r;
    public float G = g;
    public float B = b;
    public float A = a;

    public static Color4 White => new(1f, 1f, 1f, 1f);
    public static Color4 Clear => new(1f, 1f, 1f, 0f);

    /// <summary>알파만 바꾼 사본. 페이드에 가장 많이 쓰인다.</summary>
    public readonly Color4 WithAlpha(float alpha) => new(R, G, B, alpha);

    public override string ToString() => $"({R:0.##}, {G:0.##}, {B:0.##}, {A:0.##})";
}

/// <summary>
/// 위치·회전 등에 쓰는 3성분 벡터. 네이티브 <c>Mathf::Vector3</c>와 배치가 같다.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public struct Float3(float x, float y, float z)
{
    public float X = x;
    public float Y = y;
    public float Z = z;

    public static Float3 Zero    => default;
    public static Float3 One     => new(1f, 1f, 1f);
    public static Float3 Up      => new(0f, 1f, 0f);
    public static Float3 Right   => new(1f, 0f, 0f);
    public static Float3 Forward => new(0f, 0f, 1f);

    public static Float3 operator +(Float3 a, Float3 b) => new(a.X + b.X, a.Y + b.Y, a.Z + b.Z);
    public static Float3 operator -(Float3 a, Float3 b) => new(a.X - b.X, a.Y - b.Y, a.Z - b.Z);
    public static Float3 operator -(Float3 a)           => new(-a.X, -a.Y, -a.Z);
    public static Float3 operator *(Float3 a, float s)  => new(a.X * s, a.Y * s, a.Z * s);
    public static Float3 operator *(float s, Float3 a)  => a * s;
    public static Float3 operator /(Float3 a, float s)  => new(a.X / s, a.Y / s, a.Z / s);

    public readonly float LengthSquared => X * X + Y * Y + Z * Z;
    public readonly float Length        => MathF.Sqrt(LengthSquared);

    /// <summary>길이가 0에 가까우면 원본을 그대로 돌려준다 — 0으로 나누지 않기 위해서다.</summary>
    public readonly Float3 Normalized
    {
        get
        {
            float lenSq = LengthSquared;
            if (lenSq < 1e-12f) return this;

            float inv = 1f / MathF.Sqrt(lenSq);
            return new Float3(X * inv, Y * inv, Z * inv);
        }
    }

    public static float Dot(Float3 a, Float3 b) => a.X * b.X + a.Y * b.Y + a.Z * b.Z;

    public static Float3 Cross(Float3 a, Float3 b) => new(
        a.Y * b.Z - a.Z * b.Y,
        a.Z * b.X - a.X * b.Z,
        a.X * b.Y - a.Y * b.X);

    public static float Distance(Float3 a, Float3 b) => (a - b).Length;

    public static Float3 Lerp(Float3 a, Float3 b, float t) => a + (b - a) * t;

    public override string ToString() => $"({X:0.###}, {Y:0.###}, {Z:0.###})";
}
