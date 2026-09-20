#Requires -Version 7
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [Parameter(Mandatory)][string]$Work,
    [int]$TimeoutSec = 300
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$exe = Join-Path $repo "Bin/x64-$Configuration/Editor/CreatorEditor.exe"
$Work = [IO.Path]::GetFullPath($Work)
New-Item -ItemType Directory -Force -Path $Work | Out-Null
$results = @()
foreach ($command in @('dx12.parallel', 'vk.parallel')) {
    $commands = Join-Path $Work "$command.commands.txt"
    $result = Join-Path $Work "$command.result.jsonl"
    $stdout = Join-Path $Work "$command.out.txt"
    $stderr = Join-Path $Work "$command.err.txt"
    if (Test-Path -LiteralPath $result) { throw "Use a fresh result directory: $result" }
    Set-Content -LiteralPath $commands -Value @($command, 'wait 10', 'quit') -Encoding utf8
    $process = Start-Process -FilePath $exe -WorkingDirectory $repo -WindowStyle Hidden -PassThru `
        -ArgumentList '--commandlet-script', ('"' + $commands + '"'), '--result-format', 'jsonl', `
            '--result-file', ('"' + $result + '"') `
        -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    if (-not $process.WaitForExit($TimeoutSec * 1000)) {
        $process.Kill($true)
        throw "$command timed out"
    }
    $records = @(Get-Content -LiteralPath $result | ForEach-Object { $_ | ConvertFrom-Json })
    $verdict = @($records | Where-Object { $_.command -eq $command })
    $errBytes = (Get-Item -LiteralPath $stderr).Length
    if ($process.ExitCode -ne 0 -or $errBytes -ne 0 -or $verdict.Count -ne 1 -or
        $verdict[0].status -ne 'succeeded' -or -not $verdict[0].data.passed) {
        throw "$command failed: exit=$($process.ExitCode), stderr=$errBytes; inspect $result"
    }
    $results += [pscustomobject]@{
        configuration = $Configuration; command = $command; status = $verdict[0].status
        exitCode = $process.ExitCode; stderrBytes = $errBytes; log = $verdict[0].data.log
    }
    Write-Output "$Configuration $command passed (exit=0, stderr=0)"
}
$results | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $Work 'verification.json') -Encoding utf8
