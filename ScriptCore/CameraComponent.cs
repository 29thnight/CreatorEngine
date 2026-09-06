namespace CreatorEngine;

/// <summary>
/// 네이티브 <c>CameraComponent</c>의 스크립트 쪽 얼굴. 저작 자산에 20개가 있다.
///
/// ── 여기에 위치·방향이 없는 이유 ──
///
/// 네이티브 <c>ResolveCamera()</c>가 눈 위치와 forward/up/right를 매번 소유
/// 오브젝트의 Transform에서 유도한다. 카메라를 움직이려면
/// <c>Entity.Transform</c>을 옮기는 것이 정본이고, 여기에 좌표를 또 두면
/// 두 개의 진실이 생긴다.
///
/// ── dirty를 발행하지 않는 이유 ──
///
/// <see cref="LightComponent"/>와 달리 카메라는 렌더 프록시를 쓰지 않는다
/// (Scene.cpp의 프록시 Kind 열거에 Camera가 없다). 매 프레임
/// <c>CaptureFrameSnapshot</c>으로 읽히므로 값을 바꾸면 다음 프레임에 그대로 반영된다.
///
/// 주 카메라 기준의 화면 질의는 <see cref="Camera"/>에 있다.
/// </summary>
public sealed class CameraComponent : NativeComponent
{
    /// <summary>수직 시야각(도). 원근 투영에만 쓴다.</summary>
    public float FieldOfView
    {
        get => Native.CameraGetFov(OwnerHandle);
        set => Native.CameraSetFov(OwnerHandle, value);
    }

    /// <summary>근평면. 너무 작게 잡으면 깊이 정밀도가 무너진다.</summary>
    public float NearPlane
    {
        get => Native.CameraGetNearPlane(OwnerHandle);
        set => Native.CameraSetNearPlane(OwnerHandle, value);
    }

    /// <summary>원평면.</summary>
    public float FarPlane
    {
        get => Native.CameraGetFarPlane(OwnerHandle);
        set => Native.CameraSetFarPlane(OwnerHandle, value);
    }

    /// <summary>
    /// 주 카메라 후보로 표시돼 있는가.
    ///
    /// 이것을 켠다고 곧바로 주 카메라가 되지는 않는다 — 엔진은 켜져 있고
    /// 이 표시가 붙은 것들 중 인스턴스 ID가 가장 작은 것을 고른다. 그래서
    /// 여럿이 동시에 켜져 있어도 결과는 정해져 있고, 이 setter는 남의
    /// 표시를 내리지 않는다(모르는 사이에 다른 오브젝트의 저작값이 바뀌면 안 된다).
    ///
    /// 실제로 지금 골라진 것을 알고 싶으면 <see cref="Camera.Main"/>과 비교하라.
    /// </summary>
    public bool IsPrimary
    {
        get => Native.CameraIsPrimary(OwnerHandle);
        set => Native.CameraSetPrimary(OwnerHandle, value);
    }
}
