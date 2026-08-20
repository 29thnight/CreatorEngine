namespace CreatorEngine.Scripts;

/// <summary>RigidBody · 콜라이더 바인딩 검증.</summary>
public sealed partial class RigidBodyProbe : Behaviour
{
    [SerializeField] private int _checkAfterFrames = 30;

    private int _frame;
    private bool _checked;
    private int _passed;
    private int _failed;

    public override void PostPhysics(float tick)
    {
        if (_checked || ++_frame < _checkAfterFrames) return;
        _checked = true;

        CheckRigidBody();
        CheckSphereCollider();
        CheckWrongKind();

        if (_failed == 0) Log($"[RigidBodyProbe] 전체 통과 ({_passed}건)");
        else LogError($"[RigidBodyProbe] {_failed}건 실패 / {_passed}건 통과");
    }

    private void CheckRigidBody()
    {
        var rigid = GetComponent<RigidBodyComponent>();
        if (rigid is null) { LogWarning("[RigidBodyProbe] RigidBodyComponent 없음 — 건너뜀"); return; }

        Log($"[RigidBodyProbe] Rigid — mass={rigid.Mass} gravity={rigid.UseGravity} " +
            $"kinematic={rigid.IsKinematic} trigger={rigid.IsTrigger} 콜라이더={rigid.ColliderEnabled}");

        // 속도 왕복
        rigid.LinearVelocity = new Float3(1f, 2f, 3f);
        Assert("LinearVelocity 왕복", Near(rigid.LinearVelocity, new Float3(1f, 2f, 3f)), $"{rigid.LinearVelocity}");

        rigid.AddLinearVelocity(new Float3(1f, 0f, 0f));
        Assert("AddLinearVelocity 누적", Near(rigid.LinearVelocity, new Float3(2f, 2f, 3f)), $"{rigid.LinearVelocity}");

        rigid.LinearVelocity = Float3.Zero;

        rigid.AngularVelocity = new Float3(0f, 5f, 0f);
        Assert("AngularVelocity 왕복", Near(rigid.AngularVelocity, new Float3(0f, 5f, 0f)), $"{rigid.AngularVelocity}");
        rigid.AngularVelocity = Float3.Zero;

        // 불리언 상태 왕복
        bool gravity = rigid.UseGravity;
        rigid.UseGravity = !gravity;
        Assert("UseGravity 왕복", rigid.UseGravity == !gravity, $"{rigid.UseGravity}");
        rigid.UseGravity = gravity;

        bool trigger = rigid.IsTrigger;
        rigid.IsTrigger = !trigger;
        Assert("IsTrigger 왕복", rigid.IsTrigger == !trigger, $"{rigid.IsTrigger}");
        rigid.IsTrigger = trigger;

        bool colliderEnabled = rigid.ColliderEnabled;
        rigid.ColliderEnabled = !colliderEnabled;
        Assert("ColliderEnabled 왕복", rigid.ColliderEnabled == !colliderEnabled, $"{rigid.ColliderEnabled}");
        rigid.ColliderEnabled = colliderEnabled;

        // 질량 왕복
        float mass = rigid.Mass;
        rigid.Mass = 42f;
        Assert("Mass 왕복", MathF.Abs(rigid.Mass - 42f) < 1e-3f, $"{rigid.Mass}");
        rigid.Mass = mass;

        // 반환값 없는 호출들 — 죽지 않는지만 본다.
        // 어느 것이 문제인지 가리려고 한 단계씩 로그를 남긴다.
        Log("[RigidBodyProbe] step: AddForce"); rigid.AddForce(new Float3(0f, 10f, 0f), ForceMode.Impulse);
        Log("[RigidBodyProbe] step: SetLockAngular"); rigid.SetLockAngular(true, true, true);
        Log("[RigidBodyProbe] step: SetLockLinear"); rigid.SetLockLinear(false, false, false);
        Log("[RigidBodyProbe] step: SetLinearDamping"); rigid.SetLinearDamping(0.1f);
        Log("[RigidBodyProbe] step: SetScale"); rigid.SetScale(Float3.One);
        Log("[RigidBodyProbe] step: 완료");
        Assert("힘·잠금·감쇠 호출", true, "");
    }

    private void CheckSphereCollider()
    {
        var sphere = GetComponent<SphereColliderComponent>();
        if (sphere is null) { LogWarning("[RigidBodyProbe] SphereCollider 없음 — 건너뜀"); return; }

        Log($"[RigidBodyProbe] Sphere — r={sphere.Radius} offset={sphere.PositionOffset} " +
            $"반발={sphere.Restitution} 정지마찰={sphere.StaticFriction} 운동마찰={sphere.DynamicFriction}");

        float radius = sphere.Radius;
        sphere.Radius = 2.5f;
        Assert("Radius 왕복", MathF.Abs(sphere.Radius - 2.5f) < 1e-3f, $"{sphere.Radius}");
        sphere.Radius = radius;

        sphere.PositionOffset = new Float3(0f, 1f, 0f);
        Assert("PositionOffset 왕복", Near(sphere.PositionOffset, new Float3(0f, 1f, 0f)), $"{sphere.PositionOffset}");
        sphere.PositionOffset = Float3.Zero;

        sphere.Restitution = 0.6f;
        Assert("Restitution 왕복", MathF.Abs(sphere.Restitution - 0.6f) < 1e-3f, $"{sphere.Restitution}");

        sphere.DynamicFriction = 0.3f;
        Assert("DynamicFriction 왕복", MathF.Abs(sphere.DynamicFriction - 0.3f) < 1e-3f, $"{sphere.DynamicFriction}");
    }

    /// <summary>없는 종류를 물어도 죽지 않고 기본값이어야 한다(구에 높이를 묻는 등).</summary>
    private void CheckWrongKind()
    {
        Assert("붙지 않은 Box는 null", GetComponent<BoxColliderComponent>() is null, "찾아졌습니다");
        Assert("붙지 않은 Capsule은 null", GetComponent<CapsuleColliderComponent>() is null, "찾아졌습니다");
    }

    private void Assert(string name, bool ok, string detail)
    {
        if (ok) { ++_passed; return; }

        ++_failed;
        LogError($"[RigidBodyProbe] 실패: {name} — {detail}");
    }

    private static bool Near(Float3 a, Float3 b) => (a - b).Length < 1e-3f;
}
