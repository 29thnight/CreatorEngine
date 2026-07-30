using System.Runtime.InteropServices;

namespace CreatorEngine;

/// <summary>
/// 회전을 나타내는 쿼터니언. 네이티브 <c>Mathf::Quaternion</c>(= Vector4)과 배치가 같다.
///
/// 생성·보간은 전부 순수 계산이라 관리 영역에서 끝낸다 — 경계를 넘을 이유가 없다.
/// 제공하는 함수는 기존 게임 스크립트가 실제로 쓰는 것들로 골랐다
/// (CreateFromAxisAngle 15회 · Identity 11 · Slerp 7 · CreateFromYawPitchRoll 7 · LookRotation 5).
/// 엔진과 같은 왼손 좌표계·행벡터 규약을 따른다.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public struct Quaternion(float x, float y, float z, float w)
{
    public float X = x;
    public float Y = y;
    public float Z = z;
    public float W = w;

    public static Quaternion Identity => new(0f, 0f, 0f, 1f);

    /// <summary>축과 각(라디안)으로 만든다.</summary>
    public static Quaternion CreateFromAxisAngle(Float3 axis, float angle)
    {
        Float3 n = axis.Normalized;
        float half = angle * 0.5f;
        float s = MathF.Sin(half);
        return new Quaternion(n.X * s, n.Y * s, n.Z * s, MathF.Cos(half));
    }

    /// <summary>요·피치·롤(라디안). DirectXMath와 같은 Z→X→Y 순서다.</summary>
    public static Quaternion CreateFromYawPitchRoll(float yaw, float pitch, float roll)
    {
        float hy = yaw * 0.5f, hp = pitch * 0.5f, hr = roll * 0.5f;
        float sy = MathF.Sin(hy), cy = MathF.Cos(hy);
        float sp = MathF.Sin(hp), cp = MathF.Cos(hp);
        float sr = MathF.Sin(hr), cr = MathF.Cos(hr);

        return new Quaternion(
            cy * sp * cr + sy * cp * sr,
            sy * cp * cr - cy * sp * sr,
            cy * cp * sr - sy * sp * cr,
            cy * cp * cr + sy * sp * sr);
    }

    /// <summary>주어진 방향을 바라보는 회전. forward가 0에 가까우면 단위 회전을 돌려준다.</summary>
    public static Quaternion LookRotation(Float3 forward, Float3 up)
    {
        Float3 f = forward.Normalized;
        if (f.LengthSquared < 1e-12f) return Identity;

        Float3 r = Float3.Cross(up, f);
        if (r.LengthSquared < 1e-12f)
        {
            // forward와 up이 나란하면 기준축을 하나 바꿔 잡는다.
            r = Float3.Cross(MathF.Abs(f.Y) > 0.99f ? new Float3(0f, 0f, 1f) : new Float3(0f, 1f, 0f), f);
        }
        r = r.Normalized;
        Float3 u = Float3.Cross(f, r);

        // 회전 행렬 → 쿼터니언. 수치 안정성을 위해 대각합이 가장 큰 축을 기준으로 푼다.
        float trace = r.X + u.Y + f.Z;
        if (trace > 0f)
        {
            float s = MathF.Sqrt(trace + 1f) * 2f;
            return new Quaternion((u.Z - f.Y) / s, (f.X - r.Z) / s, (r.Y - u.X) / s, s * 0.25f);
        }
        if (r.X > u.Y && r.X > f.Z)
        {
            float s = MathF.Sqrt(1f + r.X - u.Y - f.Z) * 2f;
            return new Quaternion(s * 0.25f, (u.X + r.Y) / s, (f.X + r.Z) / s, (u.Z - f.Y) / s);
        }
        if (u.Y > f.Z)
        {
            float s = MathF.Sqrt(1f + u.Y - r.X - f.Z) * 2f;
            return new Quaternion((u.X + r.Y) / s, s * 0.25f, (f.Y + u.Z) / s, (f.X - r.Z) / s);
        }
        {
            float s = MathF.Sqrt(1f + f.Z - r.X - u.Y) * 2f;
            return new Quaternion((f.X + r.Z) / s, (f.Y + u.Z) / s, s * 0.25f, (r.Y - u.X) / s);
        }
    }

    /// <summary>바라보는 방향만 주면 위쪽은 +Y로 잡는다.</summary>
    public static Quaternion LookRotation(Float3 forward) => LookRotation(forward, new Float3(0f, 1f, 0f));

    /// <summary>구면 선형 보간. 최단 경로로 돈다.</summary>
    public static Quaternion Slerp(Quaternion a, Quaternion b, float t)
    {
        float dot = a.X * b.X + a.Y * b.Y + a.Z * b.Z + a.W * b.W;

        // 반대쪽 반구면 부호를 뒤집어 먼 길로 돌지 않게 한다.
        if (dot < 0f)
        {
            b = new Quaternion(-b.X, -b.Y, -b.Z, -b.W);
            dot = -dot;
        }

        // 거의 같은 방향이면 각이 0에 수렴해 나눗셈이 불안정해진다 — 선형 보간으로 대체한다.
        if (dot > 0.9995f)
        {
            return new Quaternion(
                a.X + (b.X - a.X) * t,
                a.Y + (b.Y - a.Y) * t,
                a.Z + (b.Z - a.Z) * t,
                a.W + (b.W - a.W) * t).Normalized;
        }

        float theta = MathF.Acos(dot);
        float sinTheta = MathF.Sin(theta);
        float wa = MathF.Sin((1f - t) * theta) / sinTheta;
        float wb = MathF.Sin(t * theta) / sinTheta;

        return new Quaternion(
            a.X * wa + b.X * wb,
            a.Y * wa + b.Y * wb,
            a.Z * wa + b.Z * wb,
            a.W * wa + b.W * wb);
    }

    /// <summary>켤레. 단위 쿼터니언에서는 역회전과 같다.</summary>
    public readonly Quaternion Conjugate => new(-X, -Y, -Z, W);

    public readonly Quaternion Normalized
    {
        get
        {
            float lenSq = X * X + Y * Y + Z * Z + W * W;
            if (lenSq < 1e-12f) return Identity;

            float inv = 1f / MathF.Sqrt(lenSq);
            return new Quaternion(X * inv, Y * inv, Z * inv, W * inv);
        }
    }

    /// <summary>회전 합성. <c>a * b</c>는 b를 적용한 뒤 a를 적용한다(엔진 AddRotation과 같은 순서).</summary>
    public static Quaternion operator *(Quaternion a, Quaternion b) => new(
        a.W * b.X + a.X * b.W + a.Y * b.Z - a.Z * b.Y,
        a.W * b.Y - a.X * b.Z + a.Y * b.W + a.Z * b.X,
        a.W * b.Z + a.X * b.Y - a.Y * b.X + a.Z * b.W,
        a.W * b.W - a.X * b.X - a.Y * b.Y - a.Z * b.Z);

    /// <summary>벡터를 이 회전으로 돌린다.</summary>
    public static Float3 operator *(Quaternion q, Float3 v)
    {
        Float3 axis = new(q.X, q.Y, q.Z);
        Float3 t = Float3.Cross(axis, v) * 2f;
        return v + t * q.W + Float3.Cross(axis, t);
    }

    public override readonly string ToString() => $"({X:0.###}, {Y:0.###}, {Z:0.###}, {W:0.###})";
}
