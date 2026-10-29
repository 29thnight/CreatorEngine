param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
    [Parameter(Mandatory = $true)][string]$FixtureProject,
    [Parameter(Mandatory = $true)][string]$FormatFixtures,
    [Parameter(Mandatory = $true)][string]$EngineDistribution
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
Copy-Item -Path (Join-Path $PSScriptRoot 'fixtures/audio-player/*') -Destination (Join-Path $project 'Assets/Script') -Force
$tool = Join-Path $repo "Bin/x64-$Configuration/Tools/CreatorBuildTool/CreatorBuildTool.exe"
$distribution = [IO.Path]::GetFullPath($EngineDistribution)
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
