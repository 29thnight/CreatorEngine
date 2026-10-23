[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$ProbeExe,
    [Parameter(Mandatory)][string]$Work,
    [switch]$UncachedCanary
)

$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path $Work | Out-Null
$results = @()
foreach ($expected in @(0, 23)) {
    $process = Start-Process -FilePath $ProbeExe -ArgumentList @('--exit-code', [string]$expected) -WindowStyle Hidden `
        -RedirectStandardOutput "$Work/exit-$expected.out" -RedirectStandardError "$Work/exit-$expected.err" -PassThru
    if (!$UncachedCanary) { $null = $process.Handle }
    try {
        $exited = $process.WaitForExit(5000)
        $actual = if ($exited) { $process.ExitCode } else { $null }
        $results += @{ expected = $expected; actual = $actual; exited = $exited }
        if (!$exited) { throw 'Exit-code probe did not finish' }
        if (!$UncachedCanary -and ($null -eq $actual -or $actual -ne $expected)) {
            throw "Incorrect native exit code: expected=$expected, actual=$actual"
        }
    } finally {
        if (!$process.HasExited) { $process.Kill(); $process.WaitForExit() }
        $process.Dispose()
    }
}

$result = if ($UncachedCanary) { 'UNCACHED_PROCESS_EXIT_CODE_OBSERVED' } else { 'PROCESS_EXIT_CODE_OK' }
@{ result = $result; shell = $PSVersionTable.PSVersion.ToString(); handleCached = (!$UncachedCanary); cases = $results } |
    ConvertTo-Json -Depth 6 | Set-Content "$Work/result.json" -Encoding utf8
"$result shell=$($PSVersionTable.PSVersion) cases=2 evidence=$Work"
