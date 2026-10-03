[CmdletBinding()]
param([Parameter(Mandatory)][string]$Stage, [Parameter(Mandatory)][string]$Mutator, [int]$TimeoutSeconds = 240, [switch]$Transition)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$Stage = [IO.Path]::GetFullPath($Stage)
$out = Join-Path $repo ('Build/Obj/PhysicsGeometryFailure/run-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $out | Out-Null
function FileSet($root) {
    @(Get-ChildItem -LiteralPath $root -File -Recurse | ForEach-Object {
        [IO.Path]::GetRelativePath($root, $_.FullName) + '|' + (Get-FileHash $_.FullName).Hash
    } | Sort-Object) -join "`n"
}
if (Get-ChildItem -LiteralPath $Stage -Force -Recurse | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }) {
    throw 'Stage contains reparse points'
}
$before = FileSet $Stage
$cases = @()
$scenarios = if ($Transition) { @('normal', 'missing', 'corrupt', 'revision') } else { @('missing', 'corrupt', 'revision', 'probe-missing', 'probe-corrupt', 'probe-revision') }
foreach ($scenario in $scenarios) {
    $probeEnabled = $scenario.StartsWith('probe-')
    $mode = $scenario.Replace('probe-', '')
    $case = Join-Path $out $scenario
    $clone = Join-Path $case 'Stage'
    $runtime = Join-Path $case 'Runtime'
    New-Item -ItemType Directory -Force $clone, $runtime | Out-Null
    Get-ChildItem -LiteralPath $Stage -Force | Copy-Item -Destination $clone -Recurse
    if ($mode -ne 'normal') {
        & $Mutator "$clone/GameAssets.pak" "$clone/Mutated.pak" $mode '578cd3b3-bd67-450c-a1d1-786ee613f24f' *> "$case/mutation.log"
        if ($LASTEXITCODE) { throw "$mode Pak mutation failed" }
        Move-Item -LiteralPath "$clone/Mutated.pak" -Destination "$clone/GameAssets.pak" -Force
    }
    $cloneBefore = FileSet $clone
    $launch = @{}
    if ($probeEnabled) { $launch.ArgumentList = @('--smoke', '2000', '--smoke-promotions', '8', '--smoke-geometry-failure') }
    if ($Transition) { $launch.ArgumentList = @('--smoke', '2000', '--smoke-promotions', '8', '--smoke-reload-destination', 'PhysicsCharacterGeometry.creator') }
    $process = Start-Process "$clone/Player.exe" @launch -WorkingDirectory $clone -WindowStyle Hidden -Environment @{TEMP=$runtime; TMP=$runtime} -RedirectStandardOutput "$case/player.out" -RedirectStandardError "$case/player.err" -PassThru
    try {
        $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
        while (!$process.WaitForExit(1000)) {
            if ((Get-Date) -ge $deadline) { throw "$mode Player timed out" }
        }
        $stdout = Get-Content "$case/player.out" -Raw
        $stderr = Get-Content "$case/player.err" -Raw
        $logs = Get-ChildItem $runtime -Recurse -File -Filter '*.html' | ForEach-Object { Get-Content $_.FullName -Raw }
        $combined = $stdout + $stderr + ($logs -join "`n")
        $probe = $null
        if ($probeEnabled) {
            if ($stdout -notmatch '\[physics.player.geometry.rejected\] (\{[^\r\n]+\})') {
                throw "$mode missing body rejection evidence; exit=$($process.ExitCode)"
            }
            $probe = $Matches[1] | ConvertFrom-Json
            if ($process.ExitCode -ne 3 -or $probe.failed -ne 0 -or $probe.passed -ne 5 -or !$probe.complete) {
                throw "$mode geometry rejection gate failed"
            }
        }
        if ($Transition) {
            $source = '[scene.document] source=cooked guid=cf654e3e-050c-412f-81b2-1df250d4c806'
            $destination = '[scene.document] source=cooked guid=9e8090b1-8f12-46ee-b554-610bc247dd08'
            $sourcePosition = $stdout.IndexOf($source)
            $destinationPosition = $stdout.IndexOf($destination)
            $motionMatch = [regex]::Match($stdout, '\[physics.player\] .*"passed":12,"failed":0')
            if ($sourcePosition -lt 0 -or $destinationPosition -le $sourcePosition -or
                (!$motionMatch.Success -or $motionMatch.Index -ge $destinationPosition) -or
                $combined -notmatch '\[runtime.text-parser\] calls=0') {
                throw "$mode did not simulate the healthy source before loading the cooked destination"
            }
            if ($mode -eq 'normal') {
                if ($process.ExitCode -ne 0 -or $stderr -match '\[player.simulation.failed\]' -or
                    $stdout -notmatch '\[player.smoke.reload\] activated=true gameStart=true pending=false displayedAfterActivation=true') {
                    throw 'Healthy transition control failed'
                }
            } else {
                if ($process.ExitCode -ne 3 -or $stderr -notmatch '\[player.simulation.failed\] exit=3 reason=\S' -or
                    $stdout.Substring($destinationPosition) -match '\[physics.player(?:\.convex|\.heightfield)?\] .*"passed":12' -or
                    $stdout -match 'displayedAfterActivation=true') {
                    throw "$mode destination physics rejection did not reach the production fatal policy"
                }
            }
        } else {
            if ($process.ExitCode -ne 3 -or $stderr -notmatch '\[player.simulation.failed\] exit=3 reason=\S') {
                throw "$scenario missing production fatal exit evidence"
            }
            if ($combined -match '\[physics.player\] .*"passed":12' -or $combined -notmatch '\[runtime.text-parser\] calls=0') {
                throw "$mode unexpectedly grounded on a fallback or used text parsing"
            }
        }
        if ((FileSet $clone) -ne $cloneBefore) { throw "$mode mutated its package" }
        $cases += @{mode=$mode; probeEnabled=$probeEnabled; probe=$probe; exitCode=$process.ExitCode; immutable=$true; evidence=$case}
    } finally {
        if (!$process.HasExited) { $process.Kill(); $process.WaitForExit() }
    }
}
if ((FileSet $Stage) -ne $before) { throw 'Original Stage changed' }
@{result='PHYSICS_GEOMETRY_FAILURE_PLAYER_OK'; stage=$Stage; transition=[bool]$Transition; originalImmutable=$true; cases=$cases} | ConvertTo-Json -Depth 15 | Set-Content "$out/result.json" -Encoding utf8
Write-Output "PHYSICS_GEOMETRY_FAILURE_PLAYER_OK evidence=$out"
