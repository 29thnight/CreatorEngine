using System.Runtime.InteropServices;

namespace CreatorEngine;

/// <summary>
/// 물리 질의 결과 하나. 네이티브 <c>ScriptHitResult</c>와 배치가 같다.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public struct RaycastHit
{
    internal ObjectHandle _object;

    /// <summary>충돌 대상이 속한 레이어 비트.</summary>
    public uint Layer;

    /// <summary>접촉 지점. Overlap 질의에서는 채워지지 않는다.</summary>
    public Float3 Point;

    /// <summary>접촉면 법선. Overlap 질의에서는 채워지지 않는다.</summary>
    public Float3 Normal;

    /// <summary>시작점에서의 거리. Overlap 질의에서는 -1이다.</summary>
    public float Distance;

    /// <summary>맞은 오브젝트. 이미 파괴되었으면 <c>IsAlive</c>가 false다.</summary>
    public readonly Entity Entity => new(_object);

    public readonly override string ToString()
        => $"{(Entity.IsAlive ? Entity.Name : "(사라짐)")} @ {Point} d={Distance:0.###}";
}

/// <summary>
/// 물리 질의. 타격 판정·탐지의 뼈대다(실측 Raycast 19 · SphereOverlap 16).
///
/// 결과 개수가 정해지지 않으므로 <see cref="Span{T}"/>에 채워 받는다. 호출자가 버퍼를
/// 쥐고 있어 매 프레임 불러도 할당이 없다 — 탐지 로직은 대개 Update에서 돈다.
/// <code>
/// Span&lt;RaycastHit&gt; hits = stackalloc RaycastHit[16];
/// int count = Physics.OverlapSphere(Transform.WorldPosition, 5f, hits);
/// for (int i = 0; i &lt; Math.Min(count, hits.Length); ++i) Damage(hits[i].Entity);
/// </code>
/// </summary>
public static class Physics
{
    /// <summary>모든 레이어를 대상으로 하는 기본 마스크.</summary>
    public const uint AllLayers = ~0u;

    /// <summary>
    /// 가장 가까운 충돌 하나를 찾는다. 맞으면 true.
    /// </summary>
    public static bool Raycast(Float3 origin, Float3 direction, float maxDistance,
        out RaycastHit hit, uint layerMask = AllLayers)
        => Native.PhysicsRaycast(origin, direction, maxDistance, layerMask, out hit);

    /// <summary>
    /// 광선에 걸리는 것을 전부 찾는다.
    /// </summary>
    /// <returns>
    /// 실제로 맞은 개수. <b><paramref name="results"/> 길이보다 클 수 있다</b> —
    /// 그때는 앞의 길이만큼만 채워지므로, 잘렸는지 알아야 하면 반환값과 길이를 비교할 것.
    /// </returns>
    public static int RaycastAll(Float3 origin, Float3 direction, float maxDistance,
        Span<RaycastHit> results, uint layerMask = AllLayers)
        => Native.PhysicsRaycastAll(origin, direction, maxDistance, layerMask, results);

    /// <summary>
    /// 구 범위 안에 겹치는 것을 전부 찾는다. 폭발·범위 공격·주변 탐지에 쓴다.
    /// </summary>
    /// <returns><see cref="RaycastAll"/>과 같은 규약 — 잘릴 수 있으니 길이와 비교할 것.</returns>
    public static int OverlapSphere(Float3 position, float radius,
        Span<RaycastHit> results, uint layerMask = AllLayers)
        => Native.PhysicsOverlapSphere(position, radius, layerMask, results);
}
