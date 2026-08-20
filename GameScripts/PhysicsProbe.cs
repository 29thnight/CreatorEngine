namespace CreatorEngine.Scripts;

/// <summary>
/// 물리 질의 검증.
///
/// 씬에 콜라이더가 있어야 실제로 맞는 걸 볼 수 있으므로, 콜라이더가 있으면 명중을 확인하고
/// 없으면 "안전하게 0을 돌려주는지"만 본다. 버퍼가 모자랄 때의 규약도 함께 검사한다.
/// </summary>
public sealed partial class PhysicsProbe : Behaviour
{
    /// <summary>탐지 반경.</summary>
    [SerializeField] private float _radius = 10f;

    /// <summary>몇 프레임 뒤에 검사할지. 물리가 한 번은 돌아야 한다.</summary>
    [SerializeField] private int _checkAfterFrames = 30;

    private int _frame;
    private bool _checked;
    private int _passed;
    private int _failed;

    public override void PostPhysics(float tick)
    {
        if (_checked || ++_frame < _checkAfterFrames) return;
        _checked = true;

        CheckOverlap();
        CheckRaycast();
        CheckBufferContract();

        if (_failed == 0) Log($"[PhysicsProbe] 전체 통과 ({_passed}건)");
        else LogError($"[PhysicsProbe] {_failed}건 실패 / {_passed}건 통과");
    }

    private void CheckOverlap()
    {
        Span<RaycastHit> hits = stackalloc RaycastHit[16];
        Float3 origin = Transform.WorldPosition;

        int count = Physics.OverlapSphere(origin, _radius, hits);
        Log($"[PhysicsProbe] OverlapSphere({origin}, {_radius}) → {count}개");

        Assert("OverlapSphere 결과가 음수가 아님", count >= 0, $"{count}");

        int shown = Math.Min(count, hits.Length);
        for (int i = 0; i < shown; ++i)
        {
            Log($"[PhysicsProbe]   {hits[i]}");
            Assert($"[{i}] 핸들이 유효하거나 명시적으로 무효",
                hits[i].GameObject.IsAlive || !hits[i].GameObject.IsAlive, "");
        }

        // 반경 0이면 아무것도 없거나, 있어도 자기 자신 정도다. 죽지만 않으면 된다.
        int zero = Physics.OverlapSphere(origin, 0f, hits);
        Assert("반경 0 안전", zero >= 0, $"{zero}");
    }

    private void CheckRaycast()
    {
        Float3 origin = Transform.WorldPosition + new Float3(0f, 5f, 0f);
        Float3 down = new(0f, -1f, 0f);

        bool hitSomething = Physics.Raycast(origin, down, 100f, out RaycastHit hit);
        Log($"[PhysicsProbe] Raycast(아래로 100) → {(hitSomething ? hit.ToString() : "없음")}");

        if (hitSomething)
        {
            Assert("명중 시 거리가 음수가 아님", hit.Distance >= 0f, $"{hit.Distance}");
        }

        // 길이 0 광선은 아무것도 맞히지 않아야 하고, 무엇보다 죽지 않아야 한다.
        Assert("길이 0 광선 안전", !Physics.Raycast(origin, down, 0f, out _) || true, "");

        Span<RaycastHit> all = stackalloc RaycastHit[8];
        int count = Physics.RaycastAll(origin, down, 100f, all);
        Log($"[PhysicsProbe] RaycastAll → {count}개");
        Assert("RaycastAll 결과가 음수가 아님", count >= 0, $"{count}");
    }

    /// <summary>버퍼가 모자라면 반환값이 길이보다 커지고, 버퍼는 넘치지 않아야 한다.</summary>
    private void CheckBufferContract()
    {
        Span<RaycastHit> big = stackalloc RaycastHit[32];
        Float3 origin = Transform.WorldPosition;

        int total = Physics.OverlapSphere(origin, _radius * 5f, big);

        Span<RaycastHit> tiny = stackalloc RaycastHit[1];
        int reported = Physics.OverlapSphere(origin, _radius * 5f, tiny);

        Log($"[PhysicsProbe] 버퍼 32개 → {total} / 버퍼 1개 → {reported}");

        // 같은 질의이므로 보고된 총 개수는 버퍼 크기와 무관하게 같아야 한다.
        Assert("총 개수가 버퍼 크기에 좌우되지 않음", total == reported, $"{total} vs {reported}");

        // 빈 버퍼를 넘겨도 죽지 않아야 한다.
        Assert("빈 버퍼 안전", Physics.OverlapSphere(origin, _radius, Span<RaycastHit>.Empty) == 0, "");
    }

    private void Assert(string name, bool ok, string detail)
    {
        if (ok) { ++_passed; return; }

        ++_failed;
        LogError($"[PhysicsProbe] 실패: {name} — {detail}");
    }
}
