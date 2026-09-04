namespace CreatorEngine.Scripts;

/// <summary>
/// S2 대표 케이스 — 물리 콜백.
///
/// 충돌·트리거를 받아 로그로 남긴다. Stay는 매 프레임 쏟아지므로 횟수만 세고,
/// Enter/Exit만 그때그때 기록한다.
/// </summary>
public sealed partial class CollisionProbe : Component
{
    [SerializeField] private bool _logStay;

    private int _stayCount;

    public override void OnTriggerEnter(in Collision collision)
    {
        Log($"[CollisionProbe] TriggerEnter — 상대 {Describe(collision)}");
    }

    public override void OnTriggerStay(in Collision collision)
    {
        ++_stayCount;
        if (_logStay) Log($"[CollisionProbe] TriggerStay #{_stayCount}");
    }

    public override void OnTriggerExit(in Collision collision)
    {
        Log($"[CollisionProbe] TriggerExit — 상대 {Describe(collision)} (Stay {_stayCount}회)");
        _stayCount = 0;
    }

    public override void OnCollisionEnter(in Collision collision)
    {
        Log($"[CollisionProbe] CollisionEnter — 상대 {Describe(collision)}, 접점 {collision.ContactCount}개 {collision.Contact}");
    }

    public override void OnCollisionExit(in Collision collision)
    {
        Log($"[CollisionProbe] CollisionExit — 상대 {Describe(collision)}");
    }

    /// <summary>상대가 이미 파괴되었을 수 있다. 세대 핸들이 그것을 알려준다.</summary>
    private static string Describe(in Collision collision)
        => collision.Other.IsAlive ? collision.Other.Name : "(사라진 오브젝트)";
}
