namespace CreatorEngine;

/// <summary>Scene emitter for a clip, preset, or sound graph. Playback lifetime belongs to the engine.</summary>
public sealed class SoundComponent : NativeComponent
{
    public void Play() => Native.SoundPlay(OwnerHandle);
    public void Stop() => Native.SoundStop(OwnerHandle);
    public void Pause(bool pause) => Native.SoundPause(OwnerHandle, pause);
    public bool IsPlaying() => Native.SoundIsPlaying(OwnerHandle);
    public void PlayOneShot() => Native.SoundPlayOneShot(OwnerHandle);
    public PlaybackHandle PlayInstance() => new(Native.SoundPlayInstance(OwnerHandle, false));
    public PlaybackHandle PlayOneShotInstance() => new(Native.SoundPlayInstance(OwnerHandle, true));

    /// <summary>Canonical clip GUID. Legacy names resolve only when there is one matching clip.</summary>
    public string ClipKey
    {
        get => Native.SoundGetClipKey(OwnerHandle);
        set => Native.SoundSetClipKey(OwnerHandle, value);
    }
    public AudioSource Source
    {
        get => Native.SoundGetSource(OwnerHandle);
        set => Native.SoundSetSource(OwnerHandle, value);
    }
    /// <summary>
    /// Authored settings. Valid edits update live playbacks immediately. Unsupported
    /// live edits leave affected playbacks at their prior settings and emit a diagnostic;
    /// authored values remain available for the next Play. Audio.LastError provides the
    /// latest playback-service diagnostic, rather than a promise that every edit succeeded.
    /// </summary>
    public AudioPlaySettings Settings
    {
        get => Native.SoundGetSettings(OwnerHandle);
        set => Native.SoundSetSettings(OwnerHandle, value);
    }
    public float Volume
    {
        get => Native.SoundGetVolume(OwnerHandle);
        set => Native.SoundSetVolume(OwnerHandle, value);
    }
    public float Pitch
    {
        get => Native.SoundGetPitch(OwnerHandle);
        set => Native.SoundSetPitch(OwnerHandle, value);
    }
}
