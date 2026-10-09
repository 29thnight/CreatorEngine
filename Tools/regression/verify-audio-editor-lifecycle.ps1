param(
    [Parameter(Mandatory = $true)][string]$Project,
    [Parameter(Mandatory = $true)][string]$EngineDistribution,
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$Project = [IO.Path]::GetFullPath($Project)
$ownedRoot = [IO.Path]::GetFullPath((Join-Path $repo 'Build/Validation/Phase22Player')) + [IO.Path]::DirectorySeparatorChar
if (-not $Project.StartsWith($ownedRoot, [StringComparison]::OrdinalIgnoreCase))
{
    throw 'Use the isolated Phase22 Player fixture project.'
}
$work = Join-Path $Project 'Saved/AudioLifecycleGate'
New-Item -ItemType Directory -Path $work -Force | Out-Null
$source = @'
using CreatorEngine;
using System;

public sealed partial class AudioEditorProbe : Component
{
    private static PlaybackHandle world;
    private static PlaybackHandle session;
    private static PlaybackHandle attached;
    private static int starts;
    private static int stops;

    private static void Require(bool value, string message)
    {
        if (!value)
        {
            Console.WriteLine("[AUDIO_EDITOR_FAIL] " + message + " native=" + Audio.LastError);
            throw new InvalidOperationException(message);
        }
    }

    public override void OnBeginSimulation()
    {
        Require(world.State == AudioPlaybackState.Stopped && session.State == AudioPlaybackState.Stopped &&
            attached.State == AudioPlaybackState.Stopped, "Previous generation cleaned");
        var source = new AudioSource(AudioAssetId.Parse("01234567-89ab-4cde-8fab-0123456789ab"));
        var settings = new AudioPlaySettings { Volume = 0.001f, Loop = 1, SpatialBlend = 0 };
        world = Audio.Play2D(Audio.World, source, settings);
        session = Audio.Play2D(Audio.Session, source, settings);
        attached = Audio.PlayAttached(Audio.World, source, Entity, settings);
        Require(world.IsValid && session.IsValid && attached.IsValid, "Three native play paths");
        GC.Collect();
        Require(world.IsPlaying && session.IsPlaying && attached.IsPlaying, "GC preserves value handles");
        ++starts;
        Console.WriteLine("[AUDIO_EDITOR_BEGIN] starts=" + starts);
    }

    public void AssertStopped()
    {
        Require(world.State == AudioPlaybackState.Stopped && session.State == AudioPlaybackState.Stopped &&
            attached.State == AudioPlaybackState.Stopped, "Stop ends world/session/attached playback");
        ++stops;
        Console.WriteLine("[AUDIO_EDITOR_STOP] stops=" + stops);
    }
}
'@
[IO.File]::WriteAllText((Join-Path $Project 'Assets/Script/AudioEditorProbe.cs'), $source)
# The Player-only component is replaced for this second, independently compiled fixture.
[IO.File]::WriteAllText((Join-Path $Project 'Assets/Script/PackageSmokeProbe.cs'),
    'using CreatorEngine; public sealed class PackageSmokeProbe : Component { }')
$tool = Join-Path $repo "Bin/x64-$Configuration/Tools/CreatorBuildTool/CreatorBuildTool.exe"
$managed = Join-Path $work 'Managed'
& $tool compile-game --engine-distribution $EngineDistribution --project $Project --output $managed --config $Configuration *> (Join-Path $work 'compile.log')
if ($LASTEXITCODE -ne 0)
{
    throw 'Editor audio fixture compilation failed.'
}
$commands = [Collections.Generic.List[string]]::new()
foreach ($line in @('wait 60', 'scene.new Phase22Lifecycle', 'wait 30', 'object.create AudioLifecycle', 'script.add AudioLifecycle AudioEditorProbe', 'wait 30'))
{
    $commands.Add($line)
}
for ($cycle = 0; $cycle -lt 100; ++$cycle)
{
    foreach ($line in @('play', 'wait 10', 'play.state', 'stop', 'wait 10', 'play.state', 'script.invoke AudioEditorProbe AssertStopped'))
    {
        $commands.Add($line)
    }
}
$commands.Add('quit')
$commandFile = Join-Path $work 'commands.txt'
[IO.File]::WriteAllLines($commandFile, $commands)
$exe = Join-Path $repo "Bin/x64-$Configuration/Editor/CreatorEditor.exe"
$priorWorkspace = $env:CREATOR_EDITOR_WORKSPACE_DIR
$env:CREATOR_EDITOR_WORKSPACE_DIR = Join-Path $work 'workspace'
try
{
    $process = Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe) -WindowStyle Hidden -PassThru `
        -ArgumentList @('--development-project', ('"' + $Project + '"'), '--managed-root', ('"' + $managed + '"'), '--commandlet-script', ('"' + $commandFile + '"')) `
        -RedirectStandardOutput (Join-Path $work 'editor.log') -RedirectStandardError (Join-Path $work 'editor.stderr.log')
    $deadline = [DateTime]::UtcNow.AddSeconds(600)
    while (-not $process.HasExited -and [DateTime]::UtcNow -lt $deadline)
    {
        Start-Sleep -Seconds 1
        $process.Refresh()
    }
    if (-not $process.HasExited)
    {
        $process.Kill()
        throw 'Owned Editor lifecycle probe timed out.'
    }
    $process.WaitForExit()
    $text = Get-Content (Join-Path $work 'editor.log') -Raw
    if ($process.ExitCode -ne 0 -or $text -match '\[AUDIO_EDITOR_FAIL\]' -or
        [regex]::Matches($text, '\[AUDIO_EDITOR_BEGIN\]').Count -ne 100 -or
        [regex]::Matches($text, '\[AUDIO_EDITOR_STOP\]').Count -ne 100 -or
        [regex]::Matches($text, '\[play.state\] gameStart=1 .*committed=1').Count -ne 100 -or
        [regex]::Matches($text, '\[play.state\] gameStart=0 .*committed=0').Count -ne 100)
    {
        throw "Actual Editor audio lifecycle acceptance failed. Inspect $work/editor.log"
    }
    "AUDIO_EDITOR_LIFECYCLE_OK configuration=$Configuration playCycles=100 managedStarts=100 cleanup=100 output=$work"
}
finally
{
    $env:CREATOR_EDITOR_WORKSPACE_DIR = $priorWorkspace
}
