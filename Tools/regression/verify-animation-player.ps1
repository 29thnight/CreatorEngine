[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$CookedStage,
    [string]$Work = ''
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$CookedStage = [IO.Path]::GetFullPath($CookedStage)
if (-not $Work) { $Work = Join-Path $repo 'Build/Obj/Phase13S3l/PlayerGate' }
$Work = [IO.Path]::GetFullPath($Work)
$manifestPath = Join-Path $CookedStage 'package-manifest.json'
if (-not (Test-Path -LiteralPath $manifestPath)) { throw 'A cooked Player stage is required' }
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($manifest.startupScene -ne 'AnimationPlayer.creator' -or $manifest.config -ne 'Release' -or
    $manifest.verification -ne 'passed') { throw 'Expected a verified Release AnimationPlayer stage' }
if (Test-Path -LiteralPath $Work) { throw 'Use a new work directory for independent evidence' }
New-Item -ItemType Directory -Path $Work | Out-Null
$stage = Join-Path $Work 'stage'
Copy-Item -LiteralPath $CookedStage -Destination $stage -Recurse -Force
$bin = Join-Path $repo 'Bin/x64-Release'
Copy-Item -LiteralPath "$bin/Player/Player.exe","$bin/Player/Player.runtime.dll" -Destination $stage -Force
Copy-Item -Path "$bin/Runtime/Common/*" -Destination "$stage/Runtime/Common" -Recurse -Force
Copy-Item -LiteralPath "$bin/Runtime/layout.version" -Destination "$stage/Runtime" -Force
$binaries = @('Player.exe','Player.runtime.dll') | ForEach-Object {
    @{ file=$_; sha256=(Get-FileHash -LiteralPath (Join-Path $stage $_)).Hash }
}

$smokeOut = Join-Path $Work 'smoke.out'
$smokeErr = Join-Path $Work 'smoke.err'
$smoke = Start-Process -FilePath "$stage/Player.exe" -WorkingDirectory $stage -WindowStyle Hidden `
    -ArgumentList @('--smoke','120') -RedirectStandardOutput $smokeOut -RedirectStandardError $smokeErr -PassThru
try {
    if (-not $smoke.WaitForExit(300000)) { throw 'Player smoke timed out' }
    $smoke.Refresh()
    $smokeText = Get-Content $smokeOut -Raw
    $smokeMatch = [regex]::Match($smokeText, '(?m)^\[player\.smoke\] (\{[^\r\n]+\})')
    if ($smoke.ExitCode -ne 0 -or (Get-Item $smokeErr).Length -ne 0 -or -not $smokeMatch.Success) {
        throw "Player smoke failed: exit=$($smoke.ExitCode)"
    }
    $smokeData = $smokeMatch.Groups[1].Value | ConvertFrom-Json
    if ($smokeData.displayPromotions -lt 2) { throw 'Player did not present two completed frames' }
    if ($smokeText -notmatch '\[runtime\.text-parser\] calls=0') {
        throw 'Player used an authoring text parser'
    }
    $logRoot = Join-Path $env:TEMP "CreatorEngine/Player/$($smoke.Id)/RuntimeData/Log"
    $runtimeLog = (Get-ChildItem -LiteralPath $logRoot -Recurse -File -ErrorAction Stop |
        Where-Object { $_.Extension -in '.html','.log','.txt' } |
        Get-Content -Raw) -join "`n"
    if ($runtimeLog -match '\[model\.generation\].*게시 전 검증 실패|MeshRenderer 모델 generation 해석 실패') {
        throw 'Player rejected a cooked model generation'
    }
} finally {
    if (-not $smoke.HasExited) { $smoke.Kill(); $smoke.WaitForExit() }
}

$serviceOut = Join-Path $Work 'service.out'
$serviceErr = Join-Path $Work 'service.err'
$player = Start-Process -FilePath "$stage/Player.exe" -WorkingDirectory $stage -WindowStyle Hidden `
    -ArgumentList '--command-service' -RedirectStandardOutput $serviceOut -RedirectStandardError $serviceErr -PassThru
$samples = @()
try {
    $endpoint = Join-Path $env:TEMP "CreatorEngine/Player/$($player.Id)/RuntimeData/Library/CommandService/endpoint.json"
    $deadline = (Get-Date).AddSeconds(180)
    while (-not (Test-Path -LiteralPath $endpoint)) {
        if ($player.HasExited -or (Get-Date) -ge $deadline) { throw 'Player service boot failed' }
        Start-Sleep -Milliseconds 200
    }
    $info = Get-Content -LiteralPath $endpoint -Raw | ConvertFrom-Json
    if ($info.pid -ne $player.Id) { throw 'Endpoint belongs to another Player' }
    $auth = @{ Authorization="Bearer $($info.token)" }
    $base = "http://127.0.0.1:$($info.port)"
    function Send([string]$command, [string[]]$arguments = @()) {
        $body = @{ command=$command; args=$arguments; mode='sync' } | ConvertTo-Json -Compress
        Invoke-RestMethod "$base/command" -Headers $auth -Method Post -ContentType 'application/json' -Body $body -TimeoutSec 30
    }
    $scene = Send 'player.scene'
    $status = Send 'player.status'
    if ($scene.status -ne 'succeeded' -or $scene.data.name -ne 'AnimationPlayer' -or
        $status.status -ne 'succeeded' -or -not $status.data.gameStart) {
        throw 'AnimationPlayer scene did not begin simulation'
    }
    $deadline = (Get-Date).AddSeconds(90)
    do {
        $health = Invoke-RestMethod "$base/health" -Headers $auth -TimeoutSec 20
        $result = Send 'player.animation' @('CreatorRobot')
        if ($result.status -ne 'succeeded') { throw "player.animation failed: $($result.code)" }
        $data = $result.data
        if ($data.clip -ne 'Walk' -or $data.bones -lt 1 -or $data.skinnedMeshes -lt 1) {
            throw "CreatorRobot binding invalid: clip=$($data.clip) bones=$($data.bones) meshes=$($data.skinnedMeshes)"
        }
        $samples += @{ frame=$health.frame; time=$data.time; paletteDigest=$data.paletteDigest;
            bones=$data.bones; skinnedMeshes=$data.skinnedMeshes }
        $frames = @($samples | ForEach-Object frame | Select-Object -Unique)
        $times = @($samples | ForEach-Object time | Select-Object -Unique)
        $palettes = @($samples | ForEach-Object paletteDigest | Select-Object -Unique)
        if ($frames.Count -ge 2 -and $times.Count -ge 2 -and $palettes.Count -ge 2) { break }
        Start-Sleep -Milliseconds 300
    } while ((Get-Date) -lt $deadline)
    if ($frames.Count -lt 2 -or $times.Count -lt 2 -or $palettes.Count -lt 2) {
        throw 'CreatorRobot animation did not advance its Player pose'
    }
    $quit = Send 'quit'
    if ($quit.status -ne 'succeeded') { throw 'Player quit failed' }
    if (-not $player.WaitForExit(180000)) { throw 'Player normal exit timed out' }
    $player.Refresh()
    if ($player.ExitCode -ne 0 -or (Get-Item $serviceErr).Length -ne 0) {
        throw "Player service failed: exit=$($player.ExitCode)"
    }
    @{ fixture=$CookedStage; pakSha256=(Get-FileHash "$stage/GameAssets.pak").Hash;
        binaries=$binaries; samples=$samples; smokeExit=$smoke.ExitCode; serviceExit=$player.ExitCode } |
        ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $Work 'verification.json') -Encoding utf8
    Write-Output "ANIMATION_PLAYER_OK samples=$($samples.Count) work=$Work"
} finally {
    if (-not $player.HasExited) { $player.Kill(); $player.WaitForExit() }
}
