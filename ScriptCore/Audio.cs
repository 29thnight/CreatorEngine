using System.Buffers.Binary;
using System.Runtime.InteropServices;

namespace CreatorEngine;

/// <summary>A canonical 16-byte asset identity. It is never a file path or a native pointer.</summary>
[StructLayout(LayoutKind.Sequential)]
public readonly struct AudioAssetId : IEquatable<AudioAssetId>
{
    public readonly ulong First;
    public readonly ulong Second;

    public AudioAssetId(Guid guid)
    {
        Span<byte> bytes = stackalloc byte[16];
        guid.TryWriteBytes(bytes, bigEndian: true, out _);
        First = BinaryPrimitives.ReadUInt64LittleEndian(bytes);
        Second = BinaryPrimitives.ReadUInt64LittleEndian(bytes[8..]);
    }

    public static AudioAssetId Parse(string guid) => new(Guid.Parse(guid));
    public bool IsValid => First != 0 || Second != 0;
    public bool Equals(AudioAssetId other) => First == other.First && Second == other.Second;
    public override bool Equals(object? obj) => obj is AudioAssetId other && Equals(other);
    public override int GetHashCode() => HashCode.Combine(First, Second);
    public override string ToString()
    {
        Span<byte> bytes = stackalloc byte[16];
        BinaryPrimitives.WriteUInt64LittleEndian(bytes, First);
        BinaryPrimitives.WriteUInt64LittleEndian(bytes[8..], Second);
        return new Guid(bytes, bigEndian: true).ToString();
    }
}

public enum AudioSourceKind { Clip, Preset, Graph }
public enum AudioOwnerDestroyedPolicy { Stop, DetachAndFinish }
public enum AudioPlaybackState { Stopped, Playing, Paused }
public enum AudioBus { BGM, SFX, Player, Monster, UI }
public enum AudioStealPolicy { Reject, Oldest, Quietest, LowestPriority }
public enum AudioReverbPreset { Off, Room, Hall }
public enum AudioRolloff { Linear, Inverse, Custom }
public enum AudioPlaybackEndReason { Finished, Stopped, ScopeEnded, OwnerDestroyed, Replaced }
public enum AudioParameterType { Boolean, Integer, Float, String }

/// <summary>A typed initial graph parameter, captured atomically with the Play request.</summary>
public readonly struct AudioParameter
{
    public string Name { get; }
    public AudioParameterType Type { get; }
    internal int Integer { get; }
    internal float Number { get; }
    internal string? Text { get; }
    public AudioParameter(string name, bool value) { Name = name; Type = AudioParameterType.Boolean; Integer = value ? 1 : 0; }
    public AudioParameter(string name, int value) { Name = name; Type = AudioParameterType.Integer; Integer = value; }
    public AudioParameter(string name, float value) { Name = name; Type = AudioParameterType.Float; Number = value; }
    public AudioParameter(string name, string value) { Name = name; Type = AudioParameterType.String; Text = value ?? throw new ArgumentNullException(nameof(value)); }
}

public readonly record struct AudioSource(AudioAssetId Asset, AudioSourceKind Kind = AudioSourceKind.Clip);
public readonly record struct AudioScope(ulong Value)
{
    public bool IsValid => (Value >> 32) != 0;
}

/// <summary>A non-owning logical playback value. GC and copies never stop the sound.</summary>
public readonly record struct PlaybackHandle(ulong Value)
{
    public bool IsValid => (Value >> 32) != 0;
    public AudioPlaybackState State => Native.AudioState(Value);
    public bool IsPlaying => State != AudioPlaybackState.Stopped;
    public void Stop() => Native.AudioControl(Value, 0, 0, 0);
    public void Pause() => Native.AudioControl(Value, 1, 0, 0);
    public void Resume() => Native.AudioControl(Value, 2, 0, 0);
    public void SetGainPitch(float gain, float pitch) => Native.AudioControl(Value, 3, gain, pitch);
    public void SetTransform(Float3 position, Float3 velocity) => Native.AudioSetTransform(Value, position, velocity);
    public bool SetParameter(string name, bool value) => Native.AudioSetParameter(Value, name, 0, value ? 1 : 0, 0, null);
    public bool SetParameter(string name, int value) => Native.AudioSetParameter(Value, name, 1, value, 0, null);
    public bool SetParameter(string name, float value) => Native.AudioSetParameter(Value, name, 2, 0, value, null);
    public bool SetParameter(string name, string value) => Native.AudioSetParameter(Value, name, 3, 0, 0, value);
}

[StructLayout(LayoutKind.Sequential)]
public struct AudioPlaySettings
{
    public float Volume = 1;
    public float Pitch = 1;
    public int Priority = 128;
    public int Loop;
    public AudioBus Bus = AudioBus.SFX;
    public float SpatialBlend = 1;
    public float MinimumDistance = 1;
    public float MaximumDistance = 50;
    public AudioRolloff Rolloff = AudioRolloff.Inverse;
    public int UseReverbSend;
    public float ReverbSendDecibels;
    public AudioOwnerDestroyedPolicy OwnerDestroyedPolicy;
    public uint ConcurrencyGroup;
    public int PreemptSameClip;
    public int AllowVirtualization = 1;
    public AudioPlaySettings() { }
}

[StructLayout(LayoutKind.Sequential)]
public readonly struct AudioPlaybackCompletion
{
    public readonly ulong PlaybackValue;
    public readonly ulong ScopeValue;
    public readonly ulong OwnerId;
    public readonly AudioPlaybackEndReason Reason;
    public PlaybackHandle Playback => new(PlaybackValue);
    public AudioScope Scope => new(ScopeValue);
}

/// <summary>Game-thread audio entry points. Every play explicitly chooses a world or session scope.</summary>
public static class Audio
{
    /// <summary>Caps are 0–1024; zero disables the bus or group. Group IDs are 1–65535.</summary>
    public static bool ConfigureBus(AudioBus bus, int cap, AudioStealPolicy policy)
        => Native.AudioConfigure((uint)bus, cap, (int)policy, false);
    public static bool ConfigureMaster(int physicalVoiceCap, AudioStealPolicy policy)
        => Native.AudioConfigure(5, physicalVoiceCap, (int)policy, false);
    public static bool ConfigureConcurrencyGroup(uint group, int cap, AudioStealPolicy policy)
        => Native.AudioConfigure(group, cap, (int)policy, true);
    public static bool SetReverbPreset(AudioReverbPreset preset) => Native.AudioSetReverbPreset((int)preset);
    public static string LastError => Native.AudioLastError();
    public static void SetMasterVolume(float linearGain) => Native.AudioSetBusVolume(5, linearGain);
    public static void SetBusVolume(AudioBus bus, float linearGain) => Native.AudioSetBusVolume((int)bus, linearGain);
    public static AudioScope World => new(Native.AudioGetScope(0));
    /// <summary>Explicit opt-in for music that survives scene changes. Ends when Play mode ends.</summary>
    public static AudioScope Session => new(Native.AudioGetScope(1));
    public static PlaybackHandle Play2D(AudioScope scope, AudioSource source, AudioPlaySettings? settings = null, IReadOnlyList<AudioParameter>? parameters = null)
        => new(Native.AudioPlay(scope.Value, source, 0, default, default, settings, parameters));
    public static PlaybackHandle PlayAt(AudioScope scope, AudioSource source, Float3 position, AudioPlaySettings? settings = null, IReadOnlyList<AudioParameter>? parameters = null)
        => new(Native.AudioPlay(scope.Value, source, 1, position, default, settings, parameters));
    public static PlaybackHandle PlayAttached(AudioScope scope, AudioSource source, Entity owner, AudioPlaySettings? settings = null, IReadOnlyList<AudioParameter>? parameters = null)
        => new(Native.AudioPlay(scope.Value, source, 2, default, owner.Handle, settings, parameters));
    /// <summary>Poll value completions on the game thread; callbacks never run on the audio device thread.</summary>
    public static bool TryDequeueCompletion(out AudioPlaybackCompletion completion)
        => Native.AudioTakeCompletion(out completion);
}
