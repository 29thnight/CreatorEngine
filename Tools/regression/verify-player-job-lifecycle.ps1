[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$CookedFixture,
    [ValidateSet('Debug','Release')][string]$Configuration = 'Release',
    [string]$Work = ''
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (-not $Work) { $Work = Join-Path $repo "Build/Obj/Phase13Jobs/PlayerRuntime/$Configuration-gate" }
$Work = [IO.Path]::GetFullPath($Work)
$CookedFixture = [IO.Path]::GetFullPath($CookedFixture)
$bin = Join-Path $repo "Bin/x64-$Configuration"
if (-not (Test-Path -LiteralPath "$CookedFixture/GameAssets.pak")) { throw 'A cooked Player fixture is required' }
# This gate reuses cooked content and .NET, overlays the latest native/managed
# binaries, and records their hashes. It is not a fresh package certification.
$stage = Join-Path $Work ('stage-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $stage -Force | Out-Null
Copy-Item -LiteralPath "$CookedFixture/GameAssets.pak" -Destination $stage
Copy-Item -LiteralPath "$CookedFixture/Runtime" -Destination $stage -Recurse
Copy-Item -LiteralPath "$CookedFixture/Managed" -Destination $stage -Recurse
Copy-Item -Path "$bin/Runtime/Common/*" -Destination "$stage/Runtime/Common" -Recurse -Force
Copy-Item -LiteralPath "$bin/Runtime/layout.version" -Destination "$stage/Runtime" -Force
Copy-Item -LiteralPath "$bin/Player/Player.exe","$bin/Player/Player.runtime.dll" -Destination $stage -Force
Copy-Item -Path "$bin/Managed/*" -Destination "$stage/Managed" -Recurse -Force
$hashes = @(Get-ChildItem -LiteralPath "$bin/Runtime/Common" -File -Filter '*.dll' | ForEach-Object {
    $actual = (Get-FileHash -LiteralPath (Join-Path "$stage/Runtime/Common" $_.Name)).Hash
    $expected = (Get-FileHash -LiteralPath $_.FullName).Hash
    if ($actual -ne $expected) { throw "Native runtime mismatch: $($_.Name)" }
    @{ file=$_.Name; sha256=$actual }
})
foreach ($name in @('Player.exe','Player.runtime.dll')) {
    $expected = (Get-FileHash -LiteralPath "$bin/Player/$name").Hash
    $actual = (Get-FileHash -LiteralPath "$stage/$name").Hash
    if ($actual -ne $expected) { throw "Player mismatch: $name" }
    $hashes += @{ file=$name; sha256=$actual }
}
$runs = @()
foreach ($mode in @('reload','normal-exit')) {
    $out = Join-Path $Work "$mode.out"
    $err = Join-Path $Work "$mode.err"
    $arguments = if ($mode -eq 'reload') { @('--smoke','120','--smoke-reload') } else { @('--command-service') }
    $proc = Start-Process -FilePath "$stage/Player.exe" -WorkingDirectory $stage -WindowStyle Hidden `
        -ArgumentList $arguments -RedirectStandardOutput $out -RedirectStandardError $err -PassThru
    $framesBefore = 0; $framesAfter = 0
    $runtimeRoot = Join-Path $env:TEMP "CreatorEngine/Player/$($proc.Id)"
    try {
        if ($mode -eq 'normal-exit') {
            $endpoint = Join-Path $runtimeRoot 'RuntimeData/Library/CommandService/endpoint.json'
            $deadline = (Get-Date).AddSeconds(180)
            while (-not (Test-Path -LiteralPath $endpoint)) {
                if ($proc.HasExited -or (Get-Date) -ge $deadline) { throw 'Player service boot failed' }
                Start-Sleep -Milliseconds 200
            }
            $info = Get-Content -LiteralPath $endpoint -Raw | ConvertFrom-Json
            if ($info.pid -ne $proc.Id) { throw 'Endpoint belongs to another process' }
            $auth = @{ Authorization="Bearer $($info.token)" }
            $base = "http://127.0.0.1:$($info.port)"
            $h1 = Invoke-RestMethod "$base/health" -Headers $auth -TimeoutSec 20
            Start-Sleep -Milliseconds 500
            $h2 = Invoke-RestMethod "$base/health" -Headers $auth -TimeoutSec 20
            $framesBefore = $h1.frame; $framesAfter = $h2.frame
            if ($h2.role -ne 'player' -or $h2.state -ne 'idle' -or $framesAfter -le $framesBefore) { throw 'Player frames are not advancing in idle state' }
            $status = Invoke-RestMethod "$base/command" -Headers $auth -Method Post -ContentType 'application/json' -Body '{"command":"player.status","mode":"sync"}' -TimeoutSec 20
            if ($status.status -ne 'succeeded' -or -not $status.data.gameStart) { throw 'Player simulation is not running' }
            Invoke-RestMethod "$base/command" -Headers $auth -Method Post -ContentType 'application/json' -Body '{"command":"quit","mode":"sync"}' -TimeoutSec 20 | Out-Null
        }
        if (-not $proc.WaitForExit(180000)) { throw "Player $mode timed out" }
        $proc.Refresh()
        $output = Get-Content -LiteralPath $out -Raw
        if ($proc.ExitCode -ne 0) { throw "Player $mode failed: exit=$($proc.ExitCode)" }
        if ((Get-Item -LiteralPath $err).Length -ne 0) { throw "Player $mode wrote stderr" }
        if ($output -notmatch '\[runtime.text-parser\] calls=0') { throw 'Cooked-only parser contract failed' }
        if ($mode -eq 'reload') {
            # The healthy Player writes bootstrap teardown to this trace; RHI
            # lifecycle stdout is not guaranteed after the presentation host closes.
            $tracePath = Join-Path $runtimeRoot 'RuntimeData/Log/shutdown_trace.txt'
            Copy-Item -LiteralPath $tracePath -Destination (Join-Path $Work 'reload-shutdown.txt') -Force
            $trace = Get-Content -LiteralPath $tracePath -Raw
            if ($trace -notmatch 'ce::get_job_scheduler\(\).shutdown\(\)' -or $trace -notmatch 'FinalizeRuntime 완료') { throw 'Engine scheduler teardown did not complete' }
            if ($output -notmatch '\[player.smoke.reload\] activated=true gameStart=true pending=false displayedAfterActivation=true') { throw 'Async reload did not reach a displayed frame' }
            if ([regex]::Matches($output, '\[SMOKE\] managed OnBeginSimulation: PackageSmokeProbe').Count -ne 2) { throw 'Managed simulation did not begin exactly once in each scene' }
            if ($output -notmatch '\[player.smoke\]') { throw 'Smoke completion missing' }
        } elseif (Test-Path -LiteralPath $runtimeRoot) { throw 'Normal exit left its process runtime directory' }
        $runs += @{ mode=$mode; exitCode=$proc.ExitCode; frameBefore=$framesBefore; frameAfter=$framesAfter }
        Write-Output "$Configuration Player $mode PASS"
    } finally {
        if (-not $proc.HasExited) { $proc.Kill(); $proc.WaitForExit() }
    }
}
@{ configuration=$Configuration; fixture=$CookedFixture; fixtureSha256=(Get-FileHash -LiteralPath "$stage/GameAssets.pak").Hash; stage=$stage; binaries=$hashes; runs=$runs } |
    ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $Work 'verification.json') -Encoding utf8
