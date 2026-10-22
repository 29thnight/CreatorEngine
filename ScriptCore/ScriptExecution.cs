namespace CreatorEngine;

/// <summary>Public, language-neutral guards for managed script execution.</summary>
public static class ScriptExecution
{
    /// <summary>Whether the native ScriptCore API table has been bound.</summary>
    public static bool IsReady => Native.IsReady;

    /// <summary>
    /// True only after the host has bound ScriptCore and on its game thread.
    /// The native guard's pre-initialization permissiveness is deliberately not
    /// exposed as authorization to start a script scheduler.
    /// </summary>
    public static bool IsGameThread => Native.IsReady && Native.IsGameThread;

    /// <summary>Fail before starting or resuming work outside the game thread.</summary>
    public static void RequireGameThread()
    {
        if (!IsGameThread)
            throw new InvalidOperationException("Script execution requires the initialized CreatorEngine game thread.");
    }
}
