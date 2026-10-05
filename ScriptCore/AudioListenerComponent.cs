namespace CreatorEngine;

/// <summary>Explicit scene listener, independent of a rendering camera.</summary>
public sealed class AudioListenerComponent : NativeComponent
{
    public bool Active
    {
        get => Native.AudioListenerGetActive(OwnerHandle);
        set => Native.AudioListenerSetActive(OwnerHandle, value);
    }
}
