namespace CreatorEngine;

/// <summary>광원의 종류. 네이티브 <c>LightType</c>(LightProperty.h)과 값이 같아야 한다.</summary>
public enum LightType
{
    Directional = 0,
    Point = 1,
    Spot = 2,
}

/// <summary>
/// 광원의 상태. 네이티브 <c>LightStatus</c>(LightProperty.h)와 값이 같아야 한다.
///
/// <see cref="Component.Enabled"/>와는 다른 축이다 — 이쪽은 그림자를 정적으로
/// 구울지까지 고르는 렌더 쪽 상태다.
/// </summary>
public enum LightStatus
{
    Disabled = 0,
    Enabled = 1,
    StaticShadows = 2,
}

/// <summary>
/// 네이티브 <c>LightComponent</c>의 스크립트 쪽 얼굴.
///
/// 저작 자산에 30개가 있는데 스크립트가 만질 길이 없었다. 값을 바꾸는 일이
/// 곧 연출이라(점멸·페이드·색 전환) 읽기만으로는 쓸모가 거의 없다.
///
/// ── 왜 이 래퍼를 거쳐야 하는가 ──
///
/// 엔진은 광원 값을 <c>LightRenderProxy</c>에 복사해 두고 렌더는 그 프록시만
/// 읽는다. 프록시 갱신은 <c>Scene::CommitRenderProxies</c>가 하는데, 그것은
/// dirty 큐에 실린 것만 훑는다. 그래서 값을 넣는 쪽이 dirty를 발행하지 않으면
/// 화면이 그대로다 — 값을 되읽으면 새 값이 나오므로 성공한 것처럼 보인다.
///
/// 이 래퍼의 setter는 네이티브에서 <c>LightComponent</c>의 writer를 부르고,
/// 그 writer가 <c>PublishRenderProxyDirty</c>를 함께 발행한다.
/// </summary>
public sealed class LightComponent : NativeComponent
{
    /// <summary>광원의 색. 알파는 세기와 별개로 패킹에서 덮어써진다.</summary>
    public Color4 Color
    {
        get => Native.LightGetColor(OwnerHandle);
        set => Native.LightSetColor(OwnerHandle, value);
    }

    /// <summary>세기. 패킹에서 <c>EnhancedLight.color.a</c>로 실린다.</summary>
    public float Intensity
    {
        get => Native.LightGetIntensity(OwnerHandle);
        set => Native.LightSetIntensity(OwnerHandle, value);
    }

    /// <summary>사거리. 방향광에는 의미가 없다.</summary>
    public float Range
    {
        get => Native.LightGetRange(OwnerHandle);
        set => Native.LightSetRange(OwnerHandle, value);
    }

    /// <summary>스포트라이트의 원뿔 각도(도). 패킹이 라디안으로 바꿔 싣는다.</summary>
    public float SpotAngle
    {
        get => Native.LightGetSpotAngle(OwnerHandle);
        set => Native.LightSetSpotAngle(OwnerHandle, value);
    }

    /// <summary>
    /// 광원의 종류. 범위를 벗어난 값은 네이티브가 조용히 무시한다 —
    /// 잘못된 캐스트가 광원을 정의되지 않은 상태로 만들지 않게 한다.
    /// </summary>
    public LightType Type
    {
        get => (LightType)Native.LightGetLightType(OwnerHandle);
        set => Native.LightSetLightType(OwnerHandle, (int)value);
    }

    /// <summary>렌더 쪽 상태. 범위를 벗어난 값은 무시된다.</summary>
    public LightStatus Status
    {
        get => (LightStatus)Native.LightGetLightStatus(OwnerHandle);
        set => Native.LightSetLightStatus(OwnerHandle, (int)value);
    }
}
