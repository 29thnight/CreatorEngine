[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Stage,
    [Parameter(Mandatory)][string]$ExpectedSceneGuid,
    [switch]$PreferLastCore,
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out = Join-Path $repo ('Build/Verification/ContactStream/M3Acceptance/Performance-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out | Out-Null
$receipt = [ordered]@{
    status = 'running'
    configuration = $Configuration
    stage = [IO.Path]::GetFullPath($Stage)
    scope = 'Actual rendered Player; moving convex mixed ray/overlap queries, 16/64 requests; 600 samples per block; owner CPU pinned; tiered compilation disabled'
    preferLastCore = [bool]$PreferLastCore
    order = @('off', 'on', 'on', 'off')
    runs = @()
    performanceAccepted = $false
}

function SaveReceipt {
    $receipt | ConvertTo-Json -Depth 30 | Set-Content "$out/result.json" -Encoding utf8
}

SaveReceipt
try {
    for ($attempt = 0; $attempt -lt $receipt.order.Count; ++$attempt) {
        if (Get-Process Player -ErrorAction SilentlyContinue) { throw 'Performance run requires isolated Player' }
        $mode = $receipt.order[$attempt]
        $lines = @(& "$PSScriptRoot/verify-physics-b2-player.ps1" -Stage $Stage -ExpectedSceneGuid $ExpectedSceneGuid -QueryBenchmark -QueryProfile $mode -WaitForRenderedCapture -DisableTieredCompilation -IsolateGameThread -PreferLastCore:$PreferLastCore -RequireCpuAccounting -SmokeFrames 2000)
        $lines | Set-Content "$out/run-$attempt.log" -Encoding utf8
        $line = $lines | Where-Object { $_ -like 'PHYSICS_B2_PLAYER_OK evidence=*' } | Select-Object -Last 1
        if (!$line) { throw 'Player performance receipt missing' }
        $evidence = $line.Substring($line.IndexOf('evidence=') + 9)
        $receipt.runs += @{
            attempt = $attempt
            profile = $mode
            evidence = $evidence
            product = (Get-Content "$evidence/result.json" -Raw | ConvertFrom-Json)
            rows = @(Get-Content "$evidence/query-benchmark.json" -Raw | ConvertFrom-Json)
        }
        SaveReceipt
        Write-Output "M3_PLAYER_PERFORMANCE_RUN_OK attempt=$attempt profile=$mode evidence=$evidence"
    }
    $receipt.status = 'measurements_complete'
    SaveReceipt
    Write-Output "M3_PLAYER_PERFORMANCE_MEASURED receipt=$out/result.json"
}
catch {
    $receipt.status = 'failed'
    $receipt.error = $_.ToString()
    SaveReceipt
    throw
}