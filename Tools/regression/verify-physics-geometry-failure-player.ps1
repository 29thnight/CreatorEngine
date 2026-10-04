[CmdletBinding()]
param([Parameter(Mandatory)][string]$Stage, [Parameter(Mandatory)][string]$Mutator, [int]$TimeoutSeconds = 240, [switch]$Transition, [switch]$Ddol, [switch]$Hierarchy, [switch]$ResourceProbe, [switch]$WrapperProbe)
$ErrorActionPreference = 'Stop'
if ($Hierarchy) { $Ddol = $true }
if ($Ddol) { $Transition = $true }
if ($WrapperProbe -and !$Transition) { throw 'Wrapper probe requires a healthy source transition' }
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
if ($Transition -and -not ('PhysicsDdolFailureWindow' -as [type])) {
    Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class PhysicsDdolFailureWindow {
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr window, IntPtr after, int x, int y, int width, int height, uint flags);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint process);
}
'@
}

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
    if ($Ddol) { $launch.ArgumentList += @('--smoke-ddol-character', 'CharacterGateActor') }
    if ($Hierarchy) { $launch.ArgumentList += '--smoke-ddol-hierarchy' }
    $process = Start-Process "$clone/Player.exe" @launch -WorkingDirectory $clone -WindowStyle Hidden -Environment @{TEMP=$runtime; TMP=$runtime; CE_PHYSICS_RESOURCE_PROBE=$(if($ResourceProbe){'1'}else{'0'}); CE_PHYSICS_DDOL_HIERARCHY=$(if($Hierarchy){'1'}else{'0'}); CE_PHYSICS_WRAPPER_PROBE=$(if($WrapperProbe){'1'}else{'0'}); CE_PHYSICS_WRAPPER_RETAINED=$(if($Ddol){'1'}else{'0'})} -RedirectStandardOutput "$case/player.out" -RedirectStandardError "$case/player.err" -PassThru
    try {
        if ($Transition) {
            $windowDeadline = (Get-Date).AddSeconds(30)
            do {
                $process.Refresh()
                if ($process.HasExited -or (Get-Date) -ge $windowDeadline) { throw 'Owned Player window unavailable' }
                Start-Sleep -Milliseconds 50
            } until ($process.MainWindowHandle -ne [IntPtr]::Zero)

            [uint32]$windowOwner = 0
            [PhysicsDdolFailureWindow]::GetWindowThreadProcessId($process.MainWindowHandle, [ref]$windowOwner) | Out-Null
            if ($windowOwner -ne $process.Id -or ![PhysicsDdolFailureWindow]::SetWindowPos($process.MainWindowHandle, [IntPtr]::Zero, 0, 0, 960, 540, 4)) {
                throw 'Owned Player window resize failed'
            }
        }

        $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
        while (!$process.WaitForExit(1000)) {
            if ((Get-Date) -ge $deadline) { throw "$mode Player timed out" }
        }
        $stdout = Get-Content "$case/player.out" -Raw
        $stderr = Get-Content "$case/player.err" -Raw
        $resourceEvidence = $null
        if ($ResourceProbe) {
            if ($stdout -notmatch '\[physics.player.resources\] (\{[^\r\n]+\})') {
                throw "$mode missing final physics resource ledger"
            }
            $resourceEvidence = $Matches[1] | ConvertFrom-Json
            if (!$resourceEvidence.enabled -or !$resourceEvidence.balanced -or $resourceEvidence.created.Count -ne 8 -or
                ($resourceEvidence.created -join ',') -ne ($resourceEvidence.released -join ',') -or
                $resourceEvidence.created[0] -eq 0 -or $resourceEvidence.created[1] -eq 0 -or
                $resourceEvidence.created[5] -eq 0 -or $resourceEvidence.created[6] -eq 0 -or
                ($Ddol -and $resourceEvidence.created[7] -eq 0)) {
                throw "$mode physics resource ownership did not balance after shutdown"
            }
        }

        $logs = Get-ChildItem $runtime -Recurse -File -Filter '*.html' | ForEach-Object { Get-Content $_.FullName -Raw }
        $combined = $stdout + $stderr + ($logs -join "`n")
        $probe = $null
        $ddolEvidence = $null
        $wrapperEvidence = $null
        if ($WrapperProbe -and $mode -ne 'normal') {
            $match = [regex]::Match($stdout, '\[physics.player.wrappers\] (\{[^\r\n]+\})')
            if (!$match.Success) { throw "$mode missing managed wrapper evidence" }
            $wrapperEvidence = $match.Groups[1].Value | ConvertFrom-Json
            if ($wrapperEvidence.passed -ne 16 -or $wrapperEvidence.failed -ne 0 -or
                !$wrapperEvidence.complete -or $wrapperEvidence.retained -ne [bool]$Ddol) {
                throw "$mode managed wrapper lifetime contract failed"
            }
        }
        if ($WrapperProbe -and $mode -eq 'normal' -and $stdout -match '\[physics.player.wrappers\]') {
            throw 'Normal control unexpectedly dispatched failure probe'
        }
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
            if ($Hierarchy) { $source = '[scene.document] source=cooked guid=af55790d-53c0-43b6-b4dc-6515b414e0e1' }
            if (!$Ddol) {
                # Retirement can use either known healthy source, without marking its hierarchy DDOL.
                $sourceMatch = [regex]::Match($stdout, '\[scene.document\] source=cooked guid=(?:cf654e3e-050c-412f-81b2-1df250d4c806|af55790d-53c0-43b6-b4dc-6515b414e0e1)')
                if (!$sourceMatch.Success) { throw "$mode unknown healthy source" }
                $source = $sourceMatch.Value
            }
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
                if ($Ddol) {
                    $match = [regex]::Match($stdout, '\[physics.player.ddol\] (\{[^\r\n]+\})')
                    if (!$match.Success) { throw 'Normal DDOL lifecycle evidence missing' }
                    $ddolEvidence = $match.Groups[1].Value | ConvertFrom-Json
                    if (!$ddolEvidence.complete -or $ddolEvidence.failed -ne 0 -or $ddolEvidence.passed -ne 8) {
                        throw 'Normal DDOL lifecycle/motion failed'
                    }
                    $kind = if ($Hierarchy) { 'hierarchy' } else { 'handles' }
                    $handlesMatch = [regex]::Match($stdout, ('\[physics.player.' + $kind + '\] (\{[^\r\n]+\})'))
                    if (!$handlesMatch.Success) { throw 'Normal DDOL handle evidence missing' }
                    $handles = $handlesMatch.Groups[1].Value | ConvertFrom-Json
                    $expectedChecks = if ($Hierarchy) { 56 } else { 8 }
                    $expectedNodes = if ($Hierarchy) { 5 } else { 1 }
                    if (!$handles.complete -or $handles.failed -ne 0 -or $handles.passed -ne $expectedChecks -or $handles.nodes -ne $expectedNodes) {
                        throw 'Normal DDOL handle migration failed'
                    }
                    $ddolEvidence = @{lifecycle=$ddolEvidence; handles=$handles}
                }
            } else {
                if ($process.ExitCode -ne 3 -or $stderr -notmatch '\[player.simulation.failed\] exit=3 reason=\S' -or
                    $stdout.Substring($destinationPosition) -match '\[physics.player(?:\.convex|\.heightfield)?\] .*"passed":12' -or
                    $stdout -match 'displayedAfterActivation=true') {
                    throw "$mode destination physics rejection did not reach the production fatal policy"
                }
                if ($Ddol) {
                    $match = [regex]::Match($stdout.Substring($destinationPosition), '\[physics.scene.activation.failed\] transferredPersistent=(\d+)')
                    $expectedNodes = if ($Hierarchy) { 5 } else { 1 }
                    if (!$match.Success -or [int]$match.Groups[1].Value -ne $expectedNodes) {
                        throw "$mode did not reject the destination with its transferred live DDOL owner"
                    }
                    $expectedReason = if ($mode -eq 'revision') { 'Invalid or incompatible cooked geometry artifact' } else { 'Player cooked geometry unavailable' }
                    if (!$stderr.Contains($expectedReason) -or $stdout -match '\[physics.player.ddol\]') {
                        throw "$mode wrong failure reason or unexpected completed DDOL simulation"
                    }
                    $ddolEvidence = @{transferredPersistent=[int]$match.Groups[1].Value}
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
        $cases += @{mode=$mode; probeEnabled=$probeEnabled; probe=$probe; ddolEvidence=$ddolEvidence; wrappers=$wrapperEvidence; resources=$resourceEvidence; exitCode=$process.ExitCode; immutable=$true; evidence=$case}
    } finally {
        if (!$process.HasExited) { $process.Kill(); $process.WaitForExit() }
    }
}
if ((FileSet $Stage) -ne $before) { throw 'Original Stage changed' }
@{result='PHYSICS_GEOMETRY_FAILURE_PLAYER_OK'; stage=$Stage; transition=[bool]$Transition; ddol=[bool]$Ddol; hierarchy=[bool]$Hierarchy; wrapperProbe=[bool]$WrapperProbe; originalImmutable=$true; cases=$cases} | ConvertTo-Json -Depth 15 | Set-Content "$out/result.json" -Encoding utf8
Write-Output "PHYSICS_GEOMETRY_FAILURE_PLAYER_OK evidence=$out"
