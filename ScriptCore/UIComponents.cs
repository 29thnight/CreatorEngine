namespace CreatorEngine;

/// <summary>
/// 네이티브 <c>RectTransformComponent</c>의 스크립트 쪽 얼굴.
///
/// UI 요소의 화면 배치를 담당한다. 실측에서 <c>SetAnchoredPosition</c>이 39회로
/// UI 관련 호출 중 가장 많다 — 대상을 따라다니는 표식·체력바가 매 프레임 부른다.
/// </summary>
public sealed class RectTransformComponent : NativeComponent
{
    /// <summary>부모 기준 화면 좌표(픽셀).</summary>
    public Float2 AnchoredPosition
    {
        get => Native.RectGetAnchoredPosition(OwnerHandle);
        set => Native.RectSetAnchoredPosition(OwnerHandle, value);
    }

    public Float2 SizeDelta
    {
        get => Native.RectGetSizeDelta(OwnerHandle);
        set => Native.RectSetSizeDelta(OwnerHandle, value);
    }

    /// <summary>회전·크기의 기준점. (0,0)이 좌상단, (0.5,0.5)가 중앙이다.</summary>
    public Float2 Pivot
    {
        get => Native.RectGetPivot(OwnerHandle);
        set => Native.RectSetPivot(OwnerHandle, value);
    }

    /// <summary>계산된 최종 사각형 (X=x, Y=y, Z=width, W=height 순으로 담겨 온다).</summary>
    public (float X, float Y, float Width, float Height) WorldRect
    {
        get
        {
            Color4 raw = Native.RectGetWorldRect(OwnerHandle);
            return (raw.R, raw.G, raw.B, raw.A);
        }
    }

    /// <summary>남의 UI를 옮길 때. 프로퍼티 대입은 임시 복사본이라 막혀 있다.</summary>

    public void SetAnchoredPosition(Float2 position) => Native.RectSetAnchoredPosition(OwnerHandle, position);
}

/// <summary>
/// 화면에 그려지는 UI 요소의 공통 기반.
///
/// 그리기 순서는 네이티브 <c>UIComponent</c>에 있어 이미지와 글자가 함께 쓴다.
/// 네이티브 쪽은 정확한 타입을 모른 채 기반 타입으로 찾아 처리한다.
/// </summary>
public abstract class UIComponent : NativeComponent
{
    /// <summary>같은 캔버스 안에서의 그리기 순서. 클수록 위에 그려진다.</summary>
    public int Order
    {
        get => Native.UiGetOrder(OwnerHandle);
        set => Native.UiSetOrder(OwnerHandle, value);
    }

    /// <summary>
    /// 패드/키 내비게이션이 지금 이 UI를 가리키고 있는지.
    /// 게임 메뉴 버튼의 표준 형태가 이 값 + 입력 조합이다(실측 5회):
    /// <code>if (image.IsSelected &amp;&amp; Input.GetButtonDown(0, GamepadButton.A)) { ... }</code>
    /// </summary>
    public bool IsSelected => Native.UiIsSelected(OwnerHandle);

    /// <summary>잠그면 내비게이션이 이 UI에서 다른 곳으로 이동하지 않는다(팝업 고정 등).</summary>
    public bool NavLock
    {
        get => Native.UiIsNavLocked(OwnerHandle);
        set => Native.UiSetNavLock(OwnerHandle, value);
    }
}

/// <summary>UI 내비게이션(패드/키 메뉴 이동) 전역 상태.</summary>
public static class UINavigation
{
    /// <summary>현재 선택된 UI 오브젝트. 없으면 <c>IsAlive</c>가 false다.</summary>
    public static GameObject Selected
    {
        get => new(Native.UiNavGetSelected());
        set => Native.UiNavSetSelected(value.Handle);
    }
}

/// <summary>
/// 네이티브 <c>ImageComponent</c>의 스크립트 쪽 얼굴.
/// </summary>
public sealed class ImageComponent : UIComponent
{
    /// <summary>
    /// 미리 등록된 텍스처 목록에서 인덱스로 고른다.
    /// 범위를 벗어나면 엔진이 무시하므로 <see cref="TextureCount"/>로 먼저 확인할 수 있다.
    /// </summary>
    public void SetTexture(int index) => Native.ImageSetTexture(OwnerHandle, index);

    public int TextureCount => Native.ImageGetTextureCount(OwnerHandle);

    /// <summary>곱해지는 색. 알파를 낮춰 페이드하는 용도가 대부분이다(실측 46회).</summary>
    public Color4 Color
    {
        get => Native.ImageGetColor(OwnerHandle);
        set => Native.ImageSetColor(OwnerHandle, value);
    }

    /// <summary>0~1로 잘라 보여 준다. 게이지·쿨다운 표시에 쓴다.</summary>
    public float ClipPercent
    {
        get => Native.ImageGetClipPercent(OwnerHandle);
        set => Native.ImageSetClipPercent(OwnerHandle, value);
    }

    /// <summary>현재 표시 중인 텍스처 번호(실측 5회 — 상태 아이콘 판별에 쓰인다).</summary>
    public int TextureIndex => Native.ImageGetTextureIndex(OwnerHandle);

    /// <summary>
    /// RectTransform의 크기를 현재 텍스처의 픽셀 크기로 맞춘다(uGUI의 SetNativeSize).
    ///
    /// 예전에는 엔진이 Awake마다 이것을 자동으로 해서, 스크립트가 SizeDelta를 정해도
    /// 다음 Awake에 지워졌다. 이제는 부를 때만 동작하므로 크기를 스크립트가 쥔다.
    /// 텍스처가 아직 로드되지 않았으면(크기 0) 아무것도 하지 않는다.
    /// </summary>
    public void SetNativeSize() => Native.ImageSetNativeSize(OwnerHandle);

    /// <summary>회전(라디안). 로딩 스피너 같은 회전 연출에 쓴다(실측 4회).</summary>
    public float Rotation
    {
        get => Native.ImageGetRotation(OwnerHandle);
        set => Native.ImageSetRotation(OwnerHandle, value);
    }
}

/// <summary>
/// 네이티브 <c>UIButton</c>의 스크립트 쪽 얼굴.
///
/// 클릭 판정(마우스 히트 테스트)은 엔진이 하고, 결과만 폴링으로 받는다 —
/// 콜백 델리게이트를 경계 너머로 넘기지 않는 것이 틱당 1회 규약과 맞고,
/// 클릭은 프레임당 최대 1회라 래치 하나로 유실 없이 전달된다.
/// <code>
/// public override void Update(float tick)
/// {
///     if (_button.WasClicked()) OpenMenu();
/// }
/// </code>
/// </summary>
public sealed class UIButton : UIComponent
{
    /// <summary>지난 폴링 이후 클릭됐는지. 읽으면 플래그가 내려간다(소모형).</summary>
    public bool WasClicked() => Native.ButtonConsumeClicked(OwnerHandle);
}

/// <summary>
/// 네이티브 <c>TextComponent</c>의 스크립트 쪽 얼굴.
///
/// 실측에서 <c>SetMessage</c> 16회 · <c>SetAlpha</c> 6회로, 대사·수치 표시와
/// 페이드 연출이 대부분이다.
/// </summary>
public sealed class TextComponent : UIComponent
{
    /// <summary>표시할 문자열.</summary>
    public string Message
    {
        get => Native.TextGetMessage(OwnerHandle);
        set => Native.TextSetMessage(OwnerHandle, value);
    }

    public Color4 Color
    {
        get => Native.TextGetColor(OwnerHandle);
        set => Native.TextSetColor(OwnerHandle, value);
    }

    /// <summary>
    /// 투명도만 따로 다룬다. 페이드가 흔한데 색 전체를 읽고-고쳐-쓰면 경계를 두 번 넘는다.
    /// </summary>
    public float Alpha
    {
        get => Native.TextGetAlpha(OwnerHandle);
        set => Native.TextSetAlpha(OwnerHandle, value);
    }

    public float FontSize
    {
        get => Native.TextGetFontSize(OwnerHandle);
        set => Native.TextSetFontSize(OwnerHandle, value);
    }

    /// <summary>RectTransform 영역 안에서의 상대 위치.</summary>
    public Float2 RelativePosition
    {
        get => Native.TextGetRelativePosition(OwnerHandle);
        set => Native.TextSetRelativePosition(OwnerHandle, value);
    }
}

/// <summary>
/// 네이티브 <c>Canvas</c>의 스크립트 쪽 얼굴. UI 요소를 묶는 단위다.
/// </summary>
public sealed class Canvas : NativeComponent
{
    /// <summary>캔버스끼리의 그리기 순서. 클수록 위에 그려진다.</summary>
    public int Order
    {
        get => Native.CanvasGetOrder(OwnerHandle);
        set => Native.CanvasSetOrder(OwnerHandle, value);
    }

    public string Name
    {
        get => Native.CanvasGetName(OwnerHandle);
        set => Native.CanvasSetName(OwnerHandle, value);
    }
}

