namespace CreatorEngine;

/// <summary>
/// 네이티브 <c>SoundComponent</c>(FMOD 래핑)의 스크립트 쪽 얼굴.
///
/// 사운드 호출이 실측 435회로 프리팹 다음가는 비중이라 T2에서 먼저 열었다.
/// 다만 <c>SFXPoolManager</c> 같은 풀 관리는 엔진이 아니라 게임 코드이므로 여기 두지 않는다 —
/// 설계 문서 11.4대로 그쪽은 C#으로 통째 옮기고, 경계는 이 래퍼까지만이다.
/// </summary>
public sealed class SoundComponent : NativeComponent
{
    public void Play() => Native.SoundPlay(OwnerHandle);
    public void Stop() => Native.SoundStop(OwnerHandle);
    public void Pause(bool pause) => Native.SoundPause(OwnerHandle, pause);
    public bool IsPlaying() => Native.SoundIsPlaying(OwnerHandle);

    /// <summary>겹쳐 재생한다. 발소리·타격음처럼 짧고 잦은 소리에 쓴다.</summary>
    public void PlayOneShot() => Native.SoundPlayOneShot(OwnerHandle);

    /// <summary>재생할 클립 키(SoundManager에 등록된 이름).</summary>
    public string ClipKey
    {
        get => Native.SoundGetClipKey(OwnerHandle);
        set => Native.SoundSetClipKey(OwnerHandle, value);
    }

    /// <summary>
    /// 재생 음량. 네이티브가 FMOD 채널에 이 값을 <c>Play</c> 시점에만 넘기므로,
    /// 재생 중에 바꿔도 다음 <c>Play</c>부터 적용된다(엔진 인스펙터와 같은 동작).
    /// </summary>
    public float Volume
    {
        get => Native.SoundGetVolume(OwnerHandle);
        set => Native.SoundSetVolume(OwnerHandle, value);
    }

    /// <summary><see cref="Volume"/>과 같이 다음 재생부터 반영된다.</summary>
    public float Pitch
    {
        get => Native.SoundGetPitch(OwnerHandle);
        set => Native.SoundSetPitch(OwnerHandle, value);
    }
}
