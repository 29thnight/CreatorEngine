namespace CreatorEngine.Scripts;

/// <summary>
/// 기존 C++ 스크립트 <c>Dynamic_CPP/Assets/Script/TutorialUI.cpp</c>의 C# 이관본.
///
/// 월드에 있는 대상을 따라다니는 화면 표식이다. 매 프레임 대상의 월드 좌표를
/// 화면 좌표로 바꿔 UI 위치에 반영하고, 대상이 사라지면 자기 오브젝트를 지운다.
///
/// 원본과 달라진 점:
///  · 뷰·투영 행렬을 직접 곱하던 20줄이 <see cref="Camera.WorldToScreenPoint"/> 한 줄이 됐다.
///    같은 코드가 게임 스크립트 11개 파일에 복제돼 있었다.
///  · <c>weak_ptr&lt;Entity&gt;</c>이 필요 없다 — 세대 핸들이 같은 일을 한다.
///  · 원본 Start의 컴포넌트 조회에 버그가 있었다(아래 주석 참고). 의도대로 고쳐 옮겼다.
/// </summary>
public sealed partial class TutorialUI : Behaviour
{
    /// <summary>화면 좌표에 더할 보정. 대상 머리 위에 띄우려고 보통 Y를 음수로 둔다.</summary>
    [SerializeField] private Float2 _screenOffset = new(0f, -50f);

    /// <summary>표시할 텍스처 번호. 0 공격 · 1 상호작용 · 2 필요없음 · 3 종료 · 4 대화.</summary>
    [SerializeField] private int _type;

    /// <summary>따라다닐 대상. 인스펙터나 스포너가 물려 준다.</summary>
    [SerializeField] private Entity _target;

    private RectTransformComponent? _rect;
    private ImageComponent? _image;

    public override void OnBeginSimulation()
    {
        // 원본은 두 번째 조회 조건도 m_rect를 보고 있어서(if (nullptr == m_rect) m_image = ...),
        // RectTransform이 있는 정상적인 경우에 m_image가 끝내 null로 남았다.
        // 그러면 LateUpdate 첫 줄에서 곧바로 자기 자신을 파괴한다 — 의도한 동작일 리 없어 고쳤다.
        _rect ??= GetComponent<RectTransformComponent>();
        _image ??= GetComponent<ImageComponent>();

        if (_image is not null) _image.SetTexture(_type);
    }

    public override void PostPhysics(float tick)
    {
        // 대상이 사라졌거나 UI 구성이 갖춰지지 않았으면 표식을 남겨 둘 이유가 없다.
        if (!_target.IsAlive || _rect is null || _image is null)
        {
            Entity.Destroy();
            return;
        }

        // 씬 전환 직후에는 카메라가 아직 없을 수 있다. 다음 프레임에 다시 시도한다.
        if (!Camera.Exists) return;

        Float3 screen = Camera.WorldToScreenPoint(_target.Transform.WorldPosition);

        // Z가 0 이하면 대상이 카메라 뒤다 — 이때 X·Y는 의미가 없어 그대로 두고 넘긴다.
        if (screen.Z <= 0f) return;

        // WorldToScreenPoint는 좌상단 원점 화면 좌표다. AnchoredPosition은 부모
        // 앵커 기준 로컬 오프셋이므로 둘을 직접 대입하지 않는다.
        _rect.ScreenPosition = new Float2(screen.X + _screenOffset.X, screen.Y + _screenOffset.Y);
    }

    /// <summary>따라다닐 대상을 지정한다. 원본 <c>SetTarget</c>에 해당한다.</summary>
    public void SetTarget(Entity target) => _target = target;

    /// <summary>표시할 표식 종류를 바꾼다. 원본 <c>SetType</c>에 해당한다.</summary>
    public void SetType(int type)
    {
        _type = type;
        _image?.SetTexture(type);
    }
}
