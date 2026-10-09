namespace CreatorEngine.Scripts;

/// <summary>Actual Editor/Player audio regression; uses the current project's Intro GUID.</summary>
public sealed partial class Phase22ProductAudioProbe : Component
{
    private static PlaybackHandle _world;
    private static PlaybackHandle _session;
    private static readonly List<PlaybackHandle> _load = new();
    private static AudioSource Source => new(AudioAssetId.Parse("41ec131c-cf46-402f-b5f3-b0484468ca5d"));
    private static AudioSource ResidentSource => new(AudioAssetId.Parse("276fc6b7-8304-4f68-a750-2672355e0867"));
    private static AudioPlaySettings Settings => new() { Volume = 0.001f, Loop = 1, SpatialBlend = 0 };

    [EngineCallable]
    public static string SetLoad(int voices, bool reverb)
    {
        Require(voices is >= 0 and <= 128, "Voice workload bound");
        foreach (var handle in _load)
        {
            handle.Stop();
        }
        _load.Clear();
        Require(Audio.ConfigureBus(AudioBus.SFX, 128, AudioStealPolicy.Oldest), "Bus configuration");
        Require(Audio.SetReverbPreset(reverb ? AudioReverbPreset.Hall : AudioReverbPreset.Off), "Reverb configuration");
        var stream = new AudioSource(AudioAssetId.Parse("f1b819cf-d082-4c27-858e-d9d61c8f7cb0"));
        var settings = Settings;
        settings.UseReverbSend = reverb ? 1 : 0;
        settings.ReverbSendDecibels = -12f;
        for (int index = 0; index < voices; ++index)
        {
            var handle = Audio.Play2D(Audio.Session, index % 2 == 0 ? ResidentSource : stream, settings);
            Require(handle.IsPlaying, "Workload voice " + index);
            _load.Add(handle);
        }
        return "LOAD=" + voices;
    }

    private static void Require(bool condition, string message)
    {
        if (!condition)
        {
            throw new InvalidOperationException(message + " native=" + Audio.LastError);
        }
    }

    [EngineCallable]
    public static string StartScopes()
    {
        Require(Audio.World.IsValid && Audio.Session.IsValid, "World/session scope missing");
        _world = Audio.Play2D(Audio.World, Source, Settings);
        _session = Audio.Play2D(Audio.Session, Source, Settings);
        Require(_world.IsPlaying && _session.IsPlaying, "Scope playback missing");
        GC.Collect();
        Require(_world.IsPlaying && _session.IsPlaying, "GC stopped value handles");
        return "WORLD_SESSION_PLAYING";
    }

    [EngineCallable]
    public static string CheckSceneTransfer()
    {
        Require(!_world.IsPlaying && _session.IsPlaying, "World must end; session must survive scene transfer");
        return "WORLD_ENDED_SESSION_PLAYING";
    }

    [EngineCallable]
    public static string StopScopes()
    {
        _world.Stop();
        _session.Stop();
        Require(!_world.IsPlaying && !_session.IsPlaying, "Stop scopes failed");
        return "SCOPES_STOPPED";
    }

    public override async Task OnSimulate()
    {
        int completions = 0;
        var stream = new AudioSource(AudioAssetId.Parse("f1b819cf-d082-4c27-858e-d9d61c8f7cb0"));
        var mp3 = new AudioSource(AudioAssetId.Parse("8af04b49-d9aa-4d78-840a-bd17b478b538"));
        var flac = new AudioSource(AudioAssetId.Parse("cbdedeb5-7efa-4911-b7db-96a1b4b3cefa"));
        for (int cycle = 0; cycle < 100; ++cycle)
        {
            var source = (cycle % 4) switch { 0 => flac, 1 => mp3, 2 => ResidentSource, _ => stream };
            var handle = Audio.Play2D(Audio.World, source, Settings);
            Require(handle.IsValid && handle.IsPlaying, "Play " + cycle);
            GC.Collect();
            await Scope.Delay(0f);
            Require(handle.IsPlaying, "GC value handle " + cycle);
            handle.Pause();
            Require(handle.State == AudioPlaybackState.Paused, "Pause " + cycle);
            handle.Resume();
            Require(handle.State == AudioPlaybackState.Playing, "Resume " + cycle);
            handle.SetGainPitch(0.001f, 1f);
            handle.Stop();
            await Scope.Delay(0f);
            while (Audio.TryDequeueCompletion(out var completion))
            {
                if (completion.Playback == handle && completion.Reason == AudioPlaybackEndReason.Stopped)
                {
                    ++completions;
                }
            }
        }
        Require(completions == 100, "Completion count " + completions);
        var mono = new AudioSource(AudioAssetId.Parse("276fc6b7-8304-4f68-a750-2672355e0867"));
        var attached = Audio.PlayAttached(Audio.World, mono, Entity, Settings);
        var positioned = Audio.PlayAt(Audio.Session, mono, default, Settings);
        Require(attached.IsPlaying && positioned.IsPlaying, "Attached/positioned playback");
        await Scope.Delay(0f);
        attached.Stop();
        positioned.Stop();
        Console.WriteLine("[AUDIO_CLR_PASS] cycles=100 completions=100 GC=valueHandle scopes=World,Session attached=pass positioned=pass resident=50 stream=50 mp3=25 flac=25");
    }
}
