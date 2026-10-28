[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$NativeStage,
    [Parameter(Mandatory)][string]$LegacyStage,
    [ValidateRange(3,60)][int]$Seconds = 8
)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out = Join-Path $repo ('Build/Verification/ScriptPlayerFrames-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $out | Out-Null
$stages = @{ native = [IO.Path]::GetFullPath($NativeStage); legacy = [IO.Path]::GetFullPath($LegacyStage) }
$identities = @{}
foreach ($kind in @('native', 'legacy')) {
    $runtime = Join-Path $stages[$kind] 'Player.runtime.dll'
    $scripts = if ($kind -eq 'native') { 'Managed/FSharpNative/Scripts/GameScripts.dll' } else { 'Managed/Scripts/GameScripts.dll' }
    $identities[$kind] = @{ runtime = (Get-FileHash -LiteralPath $runtime).Hash; scripts = (Get-FileHash -LiteralPath (Join-Path $stages[$kind] $scripts)).Hash }
}
$sequence = @('legacy', 'native', 'native', 'legacy', 'native', 'legacy', 'legacy', 'native')
for ($i = 0; $i -lt $sequence.Count; ++$i) {
    if (Get-Process Player -ErrorAction SilentlyContinue) { throw 'Player measurement requires an isolated session' }
    $kind = $sequence[$i]
    $run = Join-Path $out ("$i-$kind")
    $runtime = Join-Path $run 'runtime'
    New-Item -ItemType Directory $runtime -Force | Out-Null
    $stage = $stages[$kind]
    $process = Start-Process (Join-Path $stage 'Player.exe') -ArgumentList @('--command-service') -WorkingDirectory $stage -WindowStyle Hidden -Environment @{ TEMP = $runtime; TMP = $runtime; CREATOR_FSHARP_NATIVE = $(if ($kind -eq 'legacy') { '0' } else { '1' }) } -RedirectStandardOutput "$run/player.out" -RedirectStandardError "$run/player.err" -PassThru
    try {
        $deadline = [DateTime]::UtcNow.AddSeconds(120)
        $endpoint = $null
        do {
            if ($process.HasExited -or [DateTime]::UtcNow -gt $deadline) { throw "Player endpoint unavailable: $run" }
            foreach ($file in Get-ChildItem $runtime -Recurse -Filter endpoint.json) {
                $candidate = Get-Content $file.FullName -Raw | ConvertFrom-Json
                if ($candidate.pid -eq $process.Id) { $endpoint = $candidate }
            }
            if (!$endpoint) { Start-Sleep -Milliseconds 100 }
        } until ($endpoint)
        $base = "http://127.0.0.1:$($endpoint.port)"
        $headers = @{ Authorization = 'Bearer ' + $endpoint.token }
        function Invoke-PlayerCommand([string]$Name, [string[]]$Arguments = @()) {
            $body = @{ command = $Name; args = @($Arguments); mode = 'sync' } | ConvertTo-Json -Compress
            $reply = Invoke-RestMethod "$base/command" -Method Post -Headers $headers -ContentType 'application/json' -Body $body -TimeoutSec 120
            while ($reply.operationId -and $reply.state -ne 'completed') {
                Start-Sleep -Milliseconds 50
                $reply = Invoke-RestMethod "$base$($reply.poll)" -Headers $headers
            }
            if ($reply.status -ne 'succeeded') { throw "$Name failed: $($reply.code)" }
            return $reply.data
        }
        do {
            $render = Invoke-PlayerCommand 'render.status'
            if ($process.HasExited -or [DateTime]::UtcNow -gt $deadline) { throw "Rendered frame admission failed: $run" }
            if (!$render.ready -or $render.promotions -lt 8) { Start-Sleep -Milliseconds 100 }
        } until ($render.ready -and $render.promotions -ge 8)
        Start-Sleep -Seconds 3
        Invoke-PlayerCommand 'profile.record' | Out-Null
        do {
            $state = Invoke-PlayerCommand 'profile.save' @('status')
            if ([DateTime]::UtcNow -gt $deadline) { throw 'Recording did not start' }
            if ($state.writer.state -ne 'recording') { Start-Sleep -Milliseconds 100 }
        } until ($state.writer.state -eq 'recording')
        Start-Sleep -Seconds $Seconds
        Invoke-PlayerCommand 'profile.pause' | Out-Null
        do {
            $state = Invoke-PlayerCommand 'profile.save' @('status')
            if ([DateTime]::UtcNow -gt $deadline) { throw 'Recording did not finalize' }
            if ($state.writer.state -ne 'finalized') { Start-Sleep -Milliseconds 100 }
        } until ($state.writer.state -eq 'finalized')
        $capture = Join-Path $run 'frames.ceprof'
        $state = Invoke-PlayerCommand 'profile.save' @($capture)
        while ($state.pending) {
            if ([DateTime]::UtcNow -gt $deadline) { throw 'Recording save timed out' }
            Start-Sleep -Milliseconds 100
            $state = Invoke-PlayerCommand 'profile.save' @('status')
        }
        if ($state.saveState -ne 'saved' -or !$state.complete -or $state.unacked -ne 0 -or !(Test-Path $capture)) { throw "Incomplete profile: $run" }
        & python (Join-Path $PSScriptRoot 'summarize-material-profile.py') $capture *> "$run/summary.json"
        if ($LASTEXITCODE) { throw "Profile analysis failed: $run" }
        $summary = Get-Content "$run/summary.json" -Raw | ConvertFrom-Json
        if (!$summary.nativeRealPerformanceGateEligible -or $summary.frames -lt 32) { throw "Profile is not a complete native-real sample: $run" }
        Invoke-PlayerCommand 'quit' | Out-Null
        if (!$process.WaitForExit(30000) -or $process.ExitCode -ne 0) { throw "Player did not shut down: $run" }
        if (!(Select-String -LiteralPath "$run/player.out" -Pattern '\[FSHARP_PLAYER_OK\]' -Quiet)) { throw "Script did not reach fixed tick gate: $run" }
    } finally {
        if (!$process.HasExited) { $process.Kill(); $process.WaitForExit() }
    }
}
@{ sequence = $sequence; nativeStage = $stages.native; legacyStage = $stages.legacy; identities = $identities; seconds = $Seconds } | ConvertTo-Json -Depth 4 | Set-Content "$out/receipt.json" -Encoding utf8
"SCRIPT_PLAYER_FRAMES_OK evidence=$out"
