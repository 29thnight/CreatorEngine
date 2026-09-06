namespace CreatorEngine;

/// <summary>
/// 현재 카메라에 대한 질의. 컴포넌트 래퍼가 아니라 정적 접근점이다 —
/// 엔진이 <c>CameraManagement</c>에서 마지막 카메라를 들고 있고,
/// 게임 스크립트도 전부 그것을 쓰기 때문이다.
/// </summary>
public static class Camera
{
    /// <summary>쓸 수 있는 카메라가 있는지. 씬 전환 직후에는 없을 수 있다.</summary>
    public static bool Exists => Native.HasCamera();

    /// <summary>화면 크기(픽셀).</summary>
    public static Float2 ScreenSize => Native.CameraGetScreenSize();

    /// <summary>
    /// 월드 좌표를 화면 픽셀로 바꾼다.
    ///
    /// X·Y는 좌상단 기준 화면 좌표, Z는 카메라 앞쪽 거리다.
    /// <b>Z가 0 이하면 대상이 카메라 뒤에 있어 X·Y가 의미 없다</b> — 반드시 먼저 걸러야 한다
    /// (Unity <c>Camera.WorldToScreenPoint</c>와 같은 규약).
    ///
    /// 기존 C++ 스크립트 11개 파일이 뷰·투영 행렬을 직접 곱하고 w로 나누고 NDC를 펴는
    /// 스무 줄짜리를 똑같이 복제하고 있었다. 행렬을 경계 너머로 넘기는 대신 결과만 받는다.
    /// </summary>
    public static Float3 WorldToScreenPoint(Float3 world) => Native.CameraWorldToScreenPoint(world);

    /// <summary>
    /// 지금 주 카메라로 골라진 <see cref="CameraComponent"/>. 없으면 null.
    ///
    /// 엔진은 켜져 있고 primary 표시가 붙은 것들 중 인스턴스 ID가 가장 작은 것을
    /// 고르고, 그런 것이 하나도 없으면 켜져 있는 것 중 가장 작은 것으로 물러선다
    /// (CameraSystem::GetPrimaryCamera). 그래서 <c>IsPrimary</c>가 참인 컴포넌트가
    /// 여럿일 수 있고, 그중 이것 하나만 실제로 쓰인다.
    ///
    /// 매번 경계를 넘어 다시 찾으므로 프레임마다 여러 번 부를 것이면 담아 두라.
    /// </summary>
    public static CameraComponent? Main
    {
        get
        {
            var owner = new Entity(Native.CameraGetPrimaryHandle());
            return owner.IsAlive ? owner.GetComponent<CameraComponent>() : null;
        }
    }
}
