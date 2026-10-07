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
public sealed partial class LightComponent : NativeComponent
{
    // 속성과 Native 호출은 LightComponent.h 의 명시적 선언에서 생성된다.
}
