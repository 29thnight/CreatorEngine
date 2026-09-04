namespace CreatorEngine.Scripts;

/// <summary>
/// S2 대표 케이스 — 오브젝트 참조와 확장된 필드 타입을 함께 검증한다.
///
/// 다른 오브젝트를 따라다니되, 그 오브젝트가 사라지면 조용히 멈춘다.
/// 참조 대상이 파괴되었는지는 세대 핸들이 알려주므로(IsAlive) 널 검사만으로 충분하다 —
/// 네이티브 포인터를 들고 있었다면 알 수 없는 상황이다.
/// </summary>
public sealed partial class FollowTarget : Component
{
    [SerializeField] private Entity _target;
    [SerializeField] private Float3 _offset = new(0f, 2f, 0f);
    [SerializeField] private float _smoothing = 5f;
    [SerializeField(DisplayName = "표시 이름")] private string _label = "따라가기";

    private bool _warnedMissing;

    public override void OnBeginSimulation()
    {
        Log($"[FollowTarget] '{_label}' 시작 — 대상 {(_target.IsAlive ? _target.Name : "없음")}");
    }

    public override void PostPhysics(float tick)
    {
        if (!_target.IsAlive)
        {
            // 대상이 없거나 파괴됨. 매 프레임 로그를 쏟지 않도록 한 번만 알린다.
            if (!_warnedMissing)
            {
                _warnedMissing = true;
                LogWarning($"[FollowTarget] '{_label}' 대상이 없어 대기합니다");
            }
            return;
        }

        _warnedMissing = false;

        // 대상에 공간이 없으면(UI 계열) 따라갈 위치 자체가 없다.
        if (_target.Transform is not { } targetTransform) return;

        Float3 goal = targetTransform.LocalPosition + _offset;
        Float3 here = Transform.LocalPosition;

        // 프레임 독립적인 감쇠 — tick이 커져도 목표를 지나치지 않는다.
        float t = 1f - MathF.Exp(-_smoothing * tick);
        Transform.LocalPosition = here + (goal - here) * t;
    }
}
