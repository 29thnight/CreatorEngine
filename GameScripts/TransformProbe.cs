namespace CreatorEngine.Scripts;

/// <summary>
/// Transform 회전·스케일·방향축 바인딩 검증.
///
/// 왕복(쓴 값 == 읽은 값)과 합성(45°+45° == 90°)을 스크립트가 스스로 판정한다.
/// 좌표계 규약에 의존하는 값(Forward 방향 등)은 단정하지 않고 로그로만 남긴다 —
/// 엔진이 왼손 좌표계인지 여기서 다시 못 박으면 규약이 바뀔 때 거짓 실패가 난다.
/// </summary>
public sealed partial class TransformProbe : Component
{
    private const float Epsilon = 1e-3f;

    private int _passed;
    private int _failed;

    public override void OnInitialized()
    {
        Log($"[TransformProbe] 시작 — pos={Transform.LocalPosition} rot={Transform.LocalRotation} scale={Transform.LocalScale}");

        CheckScale();
        CheckRotation();
        CheckCompose();
        CheckTranslate();
        CheckWorld();
        ReportAxes();

        if (_failed == 0) Log($"[TransformProbe] 전체 통과 ({_passed}건)");
        else LogError($"[TransformProbe] {_failed}건 실패 / {_passed}건 통과");
    }

    private void CheckScale()
    {
        Transform.LocalScale = new Float3(2f, 3f, 4f);
        Assert("LocalScale 왕복", Near(Transform.LocalScale, new Float3(2f, 3f, 4f)), $"{Transform.LocalScale}");

        Transform.LocalScale = Float3.One;
    }

    private void CheckRotation()
    {
        Quaternion yaw90 = Quaternion.CreateFromAxisAngle(Float3.Up, MathF.PI * 0.5f);

        Transform.LocalRotation = yaw90;
        Assert("LocalRotation 왕복", Near(Transform.LocalRotation, yaw90), $"{Transform.LocalRotation} vs {yaw90}");

        Transform.LocalRotation = Quaternion.Identity;
    }

    private void CheckCompose()
    {
        // 45°를 두 번 더하면 90°가 되어야 한다 — AddLocalRotation의 합성 순서 검증.
        Quaternion yaw45 = Quaternion.CreateFromAxisAngle(Float3.Up, MathF.PI * 0.25f);
        Quaternion yaw90 = Quaternion.CreateFromAxisAngle(Float3.Up, MathF.PI * 0.5f);

        Transform.LocalRotation = Quaternion.Identity;
        Transform.Rotate(yaw45);
        Transform.Rotate(yaw45);

        Assert("Rotate 합성 45+45=90", Near(Transform.LocalRotation, yaw90), $"{Transform.LocalRotation} vs {yaw90}");

        Transform.LocalRotation = Quaternion.Identity;
    }

    private void CheckTranslate()
    {
        Float3 start = Transform.LocalPosition;
        Transform.Translate(new Float3(1f, 2f, 3f));

        Assert("Translate 누적", Near(Transform.LocalPosition, start + new Float3(1f, 2f, 3f)),
            $"{start} -> {Transform.LocalPosition}");

        Transform.LocalPosition = start;
    }

    private void CheckWorld()
    {
        // 부모가 없으면 월드 = 로컬이다. 부모가 있으면 환산을 거치므로 값이 달라질 수 있어,
        // 여기서는 "쓴 뒤 읽으면 같은 월드 좌표"만 본다.
        Float3 start = Transform.WorldPosition;
        Float3 target = start + new Float3(5f, 0f, 0f);

        Transform.WorldPosition = target;
        Assert("WorldPosition 왕복", Near(Transform.WorldPosition, target), $"{Transform.WorldPosition} vs {target}");

        Transform.WorldPosition = start;

        Quaternion startRotation = Transform.WorldRotation;
        Quaternion yaw90 = Quaternion.CreateFromAxisAngle(Float3.Up, MathF.PI * 0.5f);

        Transform.WorldRotation = yaw90;
        Assert("WorldRotation 왕복", Near(Transform.WorldRotation, yaw90), $"{Transform.WorldRotation} vs {yaw90}");

        Transform.WorldRotation = startRotation;

        Float3 startScale = Transform.WorldScale;
        Float3 targetScale = startScale * 2f;

        Transform.WorldScale = targetScale;
        Assert("WorldScale 왕복", Near(Transform.WorldScale, targetScale), $"{Transform.WorldScale} vs {targetScale}");

        Transform.WorldScale = startScale;
    }

    /// <summary>좌표계 규약 확인용 — 단정하지 않고 값만 남긴다.</summary>
    private void ReportAxes()
    {
        Transform.LocalRotation = Quaternion.Identity;
        Log($"[TransformProbe] 무회전 축 — forward={Transform.Forward} right={Transform.Right} up={Transform.Up}");

        Transform.LocalRotation = Quaternion.CreateFromAxisAngle(Float3.Up, MathF.PI * 0.5f);
        Log($"[TransformProbe] Yaw 90° 축 — forward={Transform.Forward} right={Transform.Right} up={Transform.Up}");

        Transform.LocalRotation = Quaternion.Identity;
    }

    private void Assert(string name, bool ok, string detail)
    {
        if (ok) { ++_passed; return; }

        ++_failed;
        LogError($"[TransformProbe] 실패: {name} — {detail}");
    }

    private static bool Near(Float3 a, Float3 b) => (a - b).Length < Epsilon;

    /// <summary>q와 -q는 같은 회전이므로 부호를 맞춰 비교한다.</summary>
    private static bool Near(Quaternion a, Quaternion b)
    {
        float dot = a.X * b.X + a.Y * b.Y + a.Z * b.Z + a.W * b.W;
        return MathF.Abs(MathF.Abs(dot) - 1f) < Epsilon;
    }
}
