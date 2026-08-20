namespace CreatorEngine.Scripts;

/// <summary>
/// RectTransform 레이아웃의 해상도 추종 검증 (PHASE 7-1).
///
/// 7-1 이전에는 세 가설이 모두 "반응 없음"으로 확인됐다(고정 해상도 의존 실증).
/// 이제는 <b>반대로</b> 단정한다 — 화면 크기가 바뀌면 캔버스와 자식이 따라와야 한다.
///
/// 화면 크기 변경은 CLI <c>window.resize</c>가 수행하고, 이 프로브는 변경 전후를
/// 관찰해 판정한다. 스스로 리사이즈하지 않는 이유는 실제 엔진 리사이즈 경로
/// (g_ClientRect 갱신 → 다음 프레임 레이아웃)를 그대로 태우기 위해서다.
/// </summary>
public sealed partial class RectLayoutProbe : Behaviour
{
    /// <summary>검사할 캔버스 오브젝트 이름.</summary>
    [SerializeField] private string _canvasName = "Canvas";

    /// <summary>기준선을 잡을 프레임.</summary>
    [SerializeField] private int _baselineFrame = 30;

    /// <summary>리사이즈 후 판정할 프레임(그 사이에 CLI가 window.resize를 수행한다).</summary>
    [SerializeField] private int _verifyFrame = 150;

    private int _frame;
    private bool _baselineTaken;
    private bool _verified;

    private RectTransformComponent? _canvasRect;
    private RectTransformComponent? _childRect;
    private string _childName = "";

    private Float2 _screenBefore;
    private (float X, float Y, float Width, float Height) _canvasBefore;
    private (float X, float Y, float Width, float Height) _childBefore;

    private int _passed;
    private int _failed;

    public override void PostPhysics(float tick)
    {
        ++_frame;

        if (!_baselineTaken && _frame >= _baselineFrame) { _baselineTaken = true; TakeBaseline(); }
        else if (_baselineTaken && !_verified && _frame >= _verifyFrame) { _verified = true; Verify(); }
    }

    private void TakeBaseline()
    {
        Entity canvas = Entity.Find(_canvasName);
        if (!canvas.IsAlive)
        {
            LogError($"[RectLayoutProbe] 캔버스 '{_canvasName}' 없음");
            _verified = true;
            return;
        }

        _canvasRect = canvas.GetComponent<RectTransformComponent>();
        if (_canvasRect is null)
        {
            LogError("[RectLayoutProbe] 캔버스에 RectTransform 없음");
            _verified = true;
            return;
        }

        _screenBefore = Camera.ScreenSize;
        _canvasBefore = _canvasRect.WorldRect;

        Log($"[RectLayoutProbe] 기준선 — 화면 {_screenBefore} · 캔버스 worldRect " +
            $"({_canvasBefore.X}, {_canvasBefore.Y}, {_canvasBefore.Width}, {_canvasBefore.Height})");

        // 캔버스 rect가 화면 크기와 일치하는지 (7-1의 직접 목표)
        Assert("캔버스 크기 = 화면 크기",
            MathF.Abs(_canvasBefore.Width - _screenBefore.X) < 1f &&
            MathF.Abs(_canvasBefore.Height - _screenBefore.Y) < 1f,
            $"캔버스 {_canvasBefore.Width}x{_canvasBefore.Height} vs 화면 {_screenBefore}");

        // 원점 규약: -size/2 (중앙 앵커 자식이 좌상단 0-원점 공간에 놓이게 하는 상쇄)
        Assert("캔버스 원점 = -size/2",
            MathF.Abs(_canvasBefore.X + _canvasBefore.Width * 0.5f) < 1f &&
            MathF.Abs(_canvasBefore.Y + _canvasBefore.Height * 0.5f) < 1f,
            $"원점 ({_canvasBefore.X}, {_canvasBefore.Y})");

        foreach (Entity child in canvas.Children)
        {
            if (child.GetComponent<RectTransformComponent>() is { } rect)
            {
                _childRect = rect;
                _childName = child.Name;
                _childBefore = rect.WorldRect;
                Log($"[RectLayoutProbe] 자식 '{_childName}' worldRect " +
                    $"({_childBefore.X}, {_childBefore.Y}, {_childBefore.Width}, {_childBefore.Height})");
                break;
            }
        }
    }

    private void Verify()
    {
        if (_canvasRect is null) return;

        Float2 screenAfter = Camera.ScreenSize;
        var canvasAfter = _canvasRect.WorldRect;

        Log($"[RectLayoutProbe] 검증 — 화면 {screenAfter} · 캔버스 worldRect " +
            $"({canvasAfter.X}, {canvasAfter.Y}, {canvasAfter.Width}, {canvasAfter.Height})");

        bool screenChanged =
            MathF.Abs(screenAfter.X - _screenBefore.X) > 1f ||
            MathF.Abs(screenAfter.Y - _screenBefore.Y) > 1f;

        if (!screenChanged)
        {
            LogWarning("[RectLayoutProbe] 화면 크기가 바뀌지 않음 — 추종 검사 건너뜀 " +
                "(window.resize가 실행됐는지 확인할 것)");
        }
        else
        {
            // 7-1의 핵심: 캔버스가 새 화면 크기를 따라왔는가
            Assert("캔버스가 새 화면 크기를 추종",
                MathF.Abs(canvasAfter.Width - screenAfter.X) < 1f &&
                MathF.Abs(canvasAfter.Height - screenAfter.Y) < 1f,
                $"캔버스 {canvasAfter.Width}x{canvasAfter.Height} vs 화면 {screenAfter}");

            Assert("변경 후에도 원점 = -size/2",
                MathF.Abs(canvasAfter.X + canvasAfter.Width * 0.5f) < 1f,
                $"원점 X {canvasAfter.X}, 크기 {canvasAfter.Width}");

            if (_childRect is not null)
            {
                var childAfter = _childRect.WorldRect;
                Log($"[RectLayoutProbe] 자식 '{_childName}' " +
                    $"({_childBefore.X}, {_childBefore.Y}) → ({childAfter.X}, {childAfter.Y})");

                // 중앙 앵커 자식은 좌상단 0-원점 공간에 머문다 — 화면이 줄어도
                // 고정 픽셀 오프셋이므로 위치 자체는 유지되는 것이 정상(uGUI 점 앵커와 동일).
                // 여기서 확인할 것은 "전파 사슬이 실제로 돌았는가"이므로, 자식 rect가
                // 계산 가능한 유효 값인지를 본다.
                Assert("자식 rect가 유효",
                    !float.IsNaN(childAfter.X) && !float.IsNaN(childAfter.Y), $"{childAfter}");
            }
        }

        if (_failed == 0) Log($"[RectLayoutProbe] 전체 통과 ({_passed}건)");
        else LogError($"[RectLayoutProbe] {_failed}건 실패 / {_passed}건 통과");
    }

    private void Assert(string name, bool ok, string detail)
    {
        if (ok) { ++_passed; return; }

        ++_failed;
        LogError($"[RectLayoutProbe] 실패: {name} — {detail}");
    }
}
