[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$EngineDistribution,
    [Parameter(Mandatory)][string]$FormatFixtures,
    [ValidateSet('Debug','Release')][string]$Configuration = 'Release',
    [string]$Work = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (!$Work) { $Work = Join-Path $repo ('Build/Verification/CliConsumers/Fixtures-' + [guid]::NewGuid().ToString('N')) }
$Work = [IO.Path]::GetFullPath($Work)
if (Test-Path -LiteralPath $Work) { throw 'Use a new fixture directory' }
New-Item -ItemType Directory -Path $Work | Out-Null
$tool = Join-Path $EngineDistribution "Bin/x64-$Configuration/Tools/CreatorBuildTool/CreatorBuildTool.exe"
$stages = [ordered]@{}

foreach ($kind in @('Jobs', 'Audio')) {
    $project = Join-Path $Work "$kind/Project"
    foreach ($directory in @('Assets/Script', 'Assets/Scenes', 'Assets/Sounds', 'Assets/Shaders/DefaultPassShader')) {
        New-Item -ItemType Directory -Path (Join-Path $project $directory) -Force | Out-Null
    }
    Copy-Item -LiteralPath (Join-Path $repo 'Dynamic_CPP/ProjectSetting') -Destination $project -Recurse
    Copy-Item -Path (Join-Path $repo 'Dynamic_CPP/Assets/Shaders/DefaultPassShader/*') `
        -Destination (Join-Path $project 'Assets/Shaders/DefaultPassShader') -Recurse

    $type = if ($kind -eq 'Jobs') { 'PackageSmokeProbe' } else { 'Phase22ProductAudioProbe' }
    $sceneName = if ($kind -eq 'Jobs') { 'CliJobs.creator' } else { 'Phase22ProductAcceptance.creator' }
    $scene = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'fixtures/PhysicsCharacterPlayer.creator'))
    $scene = $scene.Replace('m_scriptType: CharacterPlayerProbe', "m_scriptType: $type")
    [IO.File]::WriteAllText((Join-Path $project "Assets/Scenes/$sceneName"), $scene, [Text.UTF8Encoding]::new($false))
    $sceneMeta = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'fixtures/PhysicsCharacterPlayer.creator.meta'))
    [IO.File]::WriteAllText((Join-Path $project "Assets/Scenes/$sceneName.meta"), $sceneMeta, [Text.UTF8Encoding]::new($false))
    $source = if ($kind -eq 'Jobs') { "$repo/GameScripts/$type.fs" } else { "$repo/Dynamic_CPP/Assets/Script/$type.fs" }
    Copy-Item -LiteralPath $source -Destination (Join-Path $project "Assets/Script/$type.fs")
    Set-Content -LiteralPath (Join-Path $project 'Assets/Script/compile.order') -Value "$type.fs" -Encoding UTF8

    if ($kind -eq 'Audio') {
        $clips = @(
            @('cbdedeb5-7efa-4911-b7db-96a1b4b3cefa', 'sine.wav', 'Resident', 'Wav'),
            @('8af04b49-d9aa-4d78-840a-bd17b478b538', 'sine.wav', 'Stream', 'Wav'),
            @('276fc6b7-8304-4f68-a750-2672355e0867', 'silent.mp3', 'Resident', 'Mp3'),
            @('f1b819cf-d082-4c27-858e-d9d61c8f7cb0', 'silent.flac', 'Stream', 'Flac')
        )
        foreach ($clip in $clips) {
            $path = Join-Path $project ('Assets/Sounds/' + $clip[0] + [IO.Path]::GetExtension($clip[1]))
            Copy-Item -LiteralPath (Join-Path $FormatFixtures $clip[1]) -Destination $path
            $hash = (Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant()
            $size = (Get-Item -LiteralPath $path).Length
            @"
guid: $($clip[0])
audioClip:
  schemaVersion: 1
  loadMode: $($clip[2])
  spatialKind: PointMono
  codec: $($clip[3])
  payloadSize: $size
  sourceContentHash: $hash
"@ | Set-Content -LiteralPath ($path + '.meta') -Encoding UTF8
        }
    }

    & $tool select-engine --engine-distribution $EngineDistribution --project $project *> "$Work/$kind/select.log"
    if ($LASTEXITCODE) { throw "Fixture engine selection failed: $kind" }
    $stageRoot = Join-Path $Work "$kind/Staging"
    $smokeFrames = if ($kind -eq 'Jobs') { 120 } else { 600 }
    & $tool package-game --repository $repo --config $Configuration --project $project --stage-root $stageRoot `
        --input-mode Project --startup-scene $sceneName --engine-distribution $EngineDistribution --render-backend dx12 `
        --smoke-frames $smokeFrames --smoke-timeout-sec 300 *> "$Work/$kind/package.log"
    if ($LASTEXITCODE) { throw "Fixture packaging failed: $Work/$kind/package.log" }
    $pointer = Get-Content -LiteralPath (Join-Path $stageRoot 'Project.current.json') -Raw | ConvertFrom-Json
    if ($pointer.verification -ne 'passed') { throw "Fixture package not verified: $kind" }
    $stages[$kind] = Join-Path $stageRoot $pointer.releaseDirectory
}

$stages | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $Work 'stages.json') -Encoding UTF8
"CLI_CONSUMER_FIXTURES_OK evidence=$Work"
