param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
    [Parameter(Mandatory = $true)][string]$FixtureProject,
    [Parameter(Mandatory = $true)][string]$FormatFixtures
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$caseRoot = Join-Path $repo ('Build/Validation/Phase22Player/' + [Guid]::NewGuid().ToString('N'))
$project = Join-Path $caseRoot 'Project'
New-Item -ItemType Directory -Path $caseRoot -Force | Out-Null
Copy-Item -LiteralPath $FixtureProject -Destination $project -Recurse
# The scene fixture may predate newly required engine pass shaders.
Copy-Item -Path (Join-Path $repo 'Dynamic_CPP/Assets/Shaders/DefaultPassShader/*') `
    -Destination (Join-Path $project 'Assets/Shaders/DefaultPassShader') -Recurse -Force
$sounds = Join-Path $project 'Assets/Sounds'
New-Item -ItemType Directory -Path $sounds -Force | Out-Null
$clip = Join-Path $sounds 'phase22.mp3'
Copy-Item -LiteralPath (Join-Path $FormatFixtures 'silent.mp3') -Destination $clip
$hash = (Get-FileHash -LiteralPath $clip -Algorithm SHA256).Hash.ToLowerInvariant()
$size = (Get-Item -LiteralPath $clip).Length
@"
guid: 01234567-89ab-4cde-8fab-0123456789ab
audioClip:
  schemaVersion: 1
  loadMode: Stream
  spatialKind: NonSpatial
  codec: Mp3
  payloadSize: $size
  sourceContentHash: $hash
"@ | Set-Content -LiteralPath ($clip + '.meta') -Encoding utf8
$script = @'
using CreatorEngine;
using System;
using System.Threading.Tasks;

public sealed class PackageSmokeProbe : Component
{
    private static void Require(bool condition, string message)
    {
        if (!condition)
        {
            Console.WriteLine("[AUDIO_CLR_FAIL] " + message + " native=" + Audio.LastError);
            throw new InvalidOperationException(message);
        }
    }

    public override void OnInitialized() => Console.WriteLine("[SMOKE] managed OnInitialized: PackageSmokeProbe");
    public override void OnBeginSimulation()
    {
        Console.WriteLine("[SMOKE] managed OnBeginSimulation: PackageSmokeProbe");
        Require(Audio.World.IsValid && Audio.Session.IsValid, "World and session scopes");
        Require(Audio.ConfigureBus(AudioBus.SFX, 128, AudioStealPolicy.Oldest), "Configure bus");
        Require(Audio.SetReverbPreset(AudioReverbPreset.Hall), "Configure reverb");
    }

    private int _cycle, _phase, _completions;
    private PlaybackHandle _handle, _attached, _positioned;
    public override void OnTickSimulate(float dt)
    {
        if (_cycle < 100)
        {
            if (_phase == 0)
            {
                _handle = Audio.Play2D(Audio.World, new AudioSource(AudioAssetId.Parse("01234567-89ab-4cde-8fab-0123456789ab")), new AudioPlaySettings { Volume=.001f, Loop=1, SpatialBlend=0 });
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
            var mono = new AudioSource(AudioAssetId.Parse("01234567-89ab-4cde-8fab-0123456789ab"));
            _attached = Audio.PlayAttached(Audio.World, mono, Entity, new AudioPlaySettings { Volume=.001f, Loop=1, SpatialBlend=0 });
            _positioned = Audio.PlayAt(Audio.Session, mono, default, new AudioPlaySettings { Volume=.001f, Loop=1, SpatialBlend=0 });
            Require(_attached.IsPlaying && _positioned.IsPlaying, "Attached/positioned playback");
            _phase = 1;
        }
        else if (_phase == 1)
        {
            _attached.Stop(); _positioned.Stop(); _phase = 2;
            Console.WriteLine("[AUDIO_CLR_PASS] cycles=100 completions=100 GC=valueHandle scopes=World,Session attached=pass positioned=pass");
        }
    }

    public override void OnEndSimulation() { _handle.Stop(); _attached.Stop(); _positioned.Stop(); }
}

'@
[IO.File]::WriteAllText((Join-Path $project 'Assets/Script/PackageSmokeProbe.cs'), $script)
$tool = Join-Path $repo "Bin/x64-$Configuration/Tools/CreatorBuildTool/CreatorBuildTool.exe"
$publish = @(& $tool publish-engine --repository $repo --config $Configuration --output-root (Join-Path $caseRoot 'Engine') --no-pointer 2>&1)
$publishExit = $LASTEXITCODE
$publish | Set-Content (Join-Path $caseRoot 'engine.log') -Encoding utf8
if ($publishExit -ne 0)
{
    throw "Engine publication failed: $($publish -join "`n")"
}
$distribution = (Get-ChildItem (Join-Path $caseRoot 'Engine') -Directory | Where-Object Name -NotLike '.candidate-*').FullName
$selection = @(& $tool select-engine --engine-distribution $distribution --project $project 2>&1)
if ($LASTEXITCODE -ne 0)
{
    throw "Engine selection failed: $($selection -join "`n")"
}
$package = @(& $tool package-game --repository $repo --engine-distribution $distribution --project $project `
    --config $Configuration --stage-root (Join-Path $caseRoot 'Stage') --startup-scene LX_CookFixture.creator `
    --render-backend dx12 --smoke-offscreen --smoke-frames 600 --smoke-promotions 120 --smoke-timeout-sec 600 `
    --log-path (Join-Path $caseRoot 'package-live.log') 2>&1)
$packageExit = $LASTEXITCODE
$package | Set-Content (Join-Path $caseRoot 'package.log') -Encoding utf8
if ($packageExit -ne 0 -or !($package -match '\[AUDIO_CLR_PASS\]') -or ($package -match '\[AUDIO_CLR_FAIL\]'))
{
    throw "Packaged Player audio/CLR acceptance failed. Inspect $caseRoot/package.log"
}
$pointer = Get-Content (Join-Path $caseRoot 'Stage/Project.current.json') -Raw | ConvertFrom-Json
$packageRoot = Join-Path $caseRoot "Stage/$($pointer.releaseDirectory)"
$manifest = Get-Content (Join-Path $packageRoot 'package-manifest.json') -Raw | ConvertFrom-Json
if ($manifest.verification -ne 'passed' -or $manifest.smoke.textParserCalls -ne 0)
{
    throw 'Package verification or cooked-only runtime failed.'
}
foreach ($entry in $manifest.runtimeEntries)
{
    if ((Get-FileHash (Join-Path $packageRoot $entry.path) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.sha256)
    {
        throw "Player changed runtime entry: $($entry.path)"
    }
}
if ((Get-FileHash (Join-Path $packageRoot 'GameAssets.pak') -Algorithm SHA256).Hash.ToLowerInvariant() -ne $manifest.pakFileSha256)
{
    throw 'Player changed the packaged PAK.'
}
$caseRoot | Set-Content (Join-Path $repo "Build/Validation/Phase22Player/latest-$Configuration.txt")
"AUDIO_PLAYER_PACKAGE_OK configuration=$Configuration cycles=100 completions=100 gc=valueHandle packagePreserved=true output=$caseRoot"
