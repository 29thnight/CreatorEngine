namespace CreatorEngine.Scripts;

/// <summary>Actual Editor/Player audio regression; uses the current project's Intro GUID.</summary>
public sealed partial class AudioAcceptanceProbe : Component
{
    private static PlaybackHandle _world;
    private static PlaybackHandle _session;
    private static readonly List<PlaybackHandle> _load = new();
    private static AudioSource Source => new(AudioAssetId.Parse("41ec131c-cf46-402f-b5f3-b0484468ca5d"));
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
            var handle = Audio.Play2D(Audio.Session, index % 2 == 0 ? Source : stream, settings);
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

    private int _cycle, _phase, _completions;
    private PlaybackHandle _handle, _attached, _positioned;
    public void OnTickSimulate(float dt)
    {
        if (_cycle < 100)
        {
            if (_phase == 0)
            {
                _handle = Audio.Play2D(Audio.World, Source, Settings);
                Require(_handle.IsValid && _handle.IsPlaying, "Play " + _cycle);
                GC.Collect();
                _phase = 1;
            }
            else if (_phase == 1)
            {
                Require(_handle.IsPlaying, "GC value handle " + _cycle);
                _handle.Pause();
                Require(_handle.State == AudioPlaybackState.Paused, "Pause " + _cycle);
                _phase = 2;
            }
            else if (_phase == 2)
            {
                _handle.Resume();
                Require(_handle.State == AudioPlaybackState.Playing, "Resume " + _cycle);
                _handle.SetGainPitch(.001f, 1f);
                _handle.Stop();
                Require(_handle.State == AudioPlaybackState.Stopped, "Stop " + _cycle);
                _phase = 3;
            }
            else
            {
                bool completed = false;
                while (Audio.TryDequeueCompletion(out var completion))
                    if (completion.Playback == _handle && completion.Reason == AudioPlaybackEndReason.Stopped)
                    { ++_completions; completed = true; }
                if (!completed) return; // Native audio completion is asynchronous, not an assumed next-frame result.
                ++_cycle;
                _phase = 0;
            }
            return;
        }
        if (_phase == 0)
        {
            Require(_completions == 100, "Completion count " + _completions);
            var mono = new AudioSource(AudioAssetId.Parse("276fc6b7-8304-4f68-a750-2672355e0867"));
            _attached = Audio.PlayAttached(Audio.World, mono, Entity, Settings);
            _positioned = Audio.PlayAt(Audio.Session, mono, default, Settings);
            Require(_attached.IsPlaying && _positioned.IsPlaying, "Attached/positioned playback");
            _phase = 1;
        }
        else if (_phase == 1)
        {
            _attached.Stop(); _positioned.Stop(); _phase = 2;
            Console.WriteLine("[AUDIO_CLR_PASS] cycles=100 completions=100 GC=valueHandle scopes=World,Session attached=pass positioned=pass");
        }
    }

    public void OnEndSimulation() { _handle.Stop(); _attached.Stop(); _positioned.Stop(); }
}
