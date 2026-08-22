[CmdletBinding()]
param([string]$Stage = '')

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$stageRoot = Join-Path $repoRoot 'Build\Staging'
if ([string]::IsNullOrWhiteSpace($Stage)) {
    $pointer = Get-Content -LiteralPath (Join-Path $stageRoot 'Dynamic_CPP.current.json') `
        -Raw | ConvertFrom-Json
    $Stage = Join-Path $stageRoot ([string]$pointer.releaseDirectory)
}
$stagePath = [IO.Path]::GetFullPath($Stage)
$playerPath = Join-Path $stagePath 'Player.exe'
if (-not (Test-Path -LiteralPath $playerPath -PathType Leaf)) {
    throw "Player is missing: $playerPath"
}

$tempBase = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$tempPrefix = $tempBase.TrimEnd([char[]]@('\', '/')) + [IO.Path]::DirectorySeparatorChar
$testRoot = [IO.Path]::GetFullPath((Join-Path $tempBase `
    ('CreatorEngine-ReparseBoundary-' + [Guid]::NewGuid().ToString('N'))))
if (-not $testRoot.StartsWith($tempPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "runtime-boundary temp root escaped system temp: $testRoot"
}

$junction = Join-Path $testRoot 'CreatorEngine\Player'
$target = Join-Path $testRoot 'sentinel-target'
$sentinel = Join-Path $target 'sentinel.txt'
$expectedSentinel = 'CreatorEngine runtime boundary sentinel'
$process = $null
try {
    New-Item -ItemType Directory -Path (Split-Path -Parent $junction), $target -Force | Out-Null
    [IO.File]::WriteAllText($sentinel, $expectedSentinel, [Text.UTF8Encoding]::new($false))
    New-Item -ItemType Junction -Path $junction -Target $target | Out-Null
    $junctionItem = Get-Item -LiteralPath $junction -Force
    if (($junctionItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) {
        throw 'test setup did not create a reparse point.'
    }

    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $playerPath
    $startInfo.WorkingDirectory = $stagePath
    $startInfo.UseShellExecute = $false
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $startInfo.Environment['TEMP'] = $testRoot
    $startInfo.Environment['TMP'] = $testRoot
    $windowsRoot = [Environment]::GetFolderPath([Environment+SpecialFolder]::Windows)
    $startInfo.Environment['PATH'] = @(
        $stagePath,
        (Join-Path $windowsRoot 'System32'),
        $windowsRoot
    ) -join [IO.Path]::PathSeparator

    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    if (-not $process.Start()) { throw 'boundary Player failed to start.' }
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit(30000)) {
        throw 'boundary Player did not fail closed within 30 seconds.'
    }
    $process.WaitForExit()
    $stdout = $stdoutTask.GetAwaiter().GetResult()
    $stderr = $stderrTask.GetAwaiter().GetResult()
    $exitCode = $process.ExitCode
    $sentinelPreserved = (Test-Path -LiteralPath $sentinel -PathType Leaf) -and
        ([IO.File]::ReadAllText($sentinel) -eq $expectedSentinel)
    $reparseRejected = ($stdout + "`n" + $stderr) -match
        'runtime root crosses an unsafe reparse point'
    $cleanupRejected = ($stdout + "`n" + $stderr) -match
        'runtime ownership contract is invalid; cleanup refused'

    [pscustomobject]@{
        ExitCode = $exitCode
        ReparseRejected = $reparseRejected
        CleanupRejected = $cleanupRejected
        SentinelPreserved = $sentinelPreserved
    } | Format-List

    if ($exitCode -ne 2 -or -not $reparseRejected -or -not $cleanupRejected -or
        -not $sentinelPreserved) {
        throw 'Player runtime reparse boundary did not fail closed.'
    }
} finally {
    if ($null -ne $process) {
        if (-not $process.HasExited) {
            try { $process.Kill($true); $process.WaitForExit() } catch {}
        }
        $process.Dispose()
    }
    if (Test-Path -LiteralPath $junction) {
        $junctionItem = Get-Item -LiteralPath $junction -Force
        if (($junctionItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) {
            throw "refusing cleanup because the expected junction changed type: $junction"
        }
        Remove-Item -LiteralPath $junction -Force
    }
    if (Test-Path -LiteralPath $testRoot) {
        $remainingReparse = @(Get-ChildItem -LiteralPath $testRoot -Force -Recurse |
            Where-Object {
                ($_.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0
            })
        if ($remainingReparse.Count -ne 0) {
            throw "refusing recursive cleanup with remaining reparse points: $testRoot"
        }
        Remove-Item -LiteralPath $testRoot -Recurse -Force
    }
}
