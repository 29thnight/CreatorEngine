[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$buildScript = Join-Path $repoRoot 'Tools\build.ps1'
$pwshPath = (Get-Command 'pwsh.exe' -ErrorAction Stop).Source

function Get-TreeSnapshot {
    param([Parameter(Mandatory)][string]$Root)

    return @(
        Get-ChildItem -LiteralPath $Root -Force -Recurse |
            ForEach-Object {
                $relative = [IO.Path]::GetRelativePath($Root, $_.FullName)
                if ($_.PSIsContainer) {
                    "D|$relative"
                } else {
                    "F|$relative|$((Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash)"
                }
            } |
            Sort-Object
    )
}

$tempBase = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$tempPrefix = $tempBase.TrimEnd([char[]]@('\', '/')) + [IO.Path]::DirectorySeparatorChar
$testRoot = [IO.Path]::GetFullPath((Join-Path $tempBase `
    ('CreatorEngine-StageBoundary-' + [Guid]::NewGuid().ToString('N'))))
if (-not $testRoot.StartsWith($tempPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "stage-boundary temp root escaped system temp: $testRoot"
}
New-Item -ItemType Directory -Path $testRoot | Out-Null
$junction = Join-Path $testRoot 'stage'
$target = Join-Path $testRoot 'isolated-target'
$project = Join-Path $testRoot 'isolated-project'
$assets = Join-Path $project 'Assets'
$settings = Join-Path $project 'ProjectSetting'
New-Item -ItemType Directory -Path $target, $assets, $settings | Out-Null
$sentinel = Join-Path $target 'sentinel.txt'
[IO.File]::WriteAllText($sentinel, 'CreatorEngine isolated StageRoot boundary sentinel',
    [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $assets 'asset-sentinel.txt'), 'isolated asset input',
    [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $settings 'settings-sentinel.asset'),
    'isolated settings input', [Text.UTF8Encoding]::new($false))

$sentinelHash = (Get-FileHash -LiteralPath $sentinel -Algorithm SHA256).Hash
$targetBefore = Get-TreeSnapshot -Root $target
$projectBefore = Get-TreeSnapshot -Root $project

try {
    New-Item -ItemType Junction -Path $junction -Target $target | Out-Null
    $junctionItem = Get-Item -LiteralPath $junction -Force
    if (($junctionItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) {
        throw 'test setup did not create a StageRoot reparse point.'
    }

    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $pwshPath
    $startInfo.WorkingDirectory = $repoRoot
    $startInfo.UseShellExecute = $false
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    foreach ($argument in @(
        '-NoProfile', '-File', $buildScript,
        '-Config', 'Release',
        '-InputMode', 'Project',
        '-Project', $project,
        '-SkipVerify',
        '-StageRoot', $junction
    )) {
        [void]$startInfo.ArgumentList.Add($argument)
    }

    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    try {
        if (-not $process.Start()) { throw 'stage-boundary child failed to start.' }
        $stdoutTask = $process.StandardOutput.ReadToEndAsync()
        $stderrTask = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit(30000)) {
            $process.Kill($true)
            $process.WaitForExit()
            throw 'stage-boundary child did not fail closed within 30 seconds.'
        }
        $process.WaitForExit()
        $output = $stdoutTask.GetAwaiter().GetResult() + "`n" +
            $stderrTask.GetAwaiter().GetResult()
        $exitCode = $process.ExitCode
    } finally {
        $process.Dispose()
    }

    $sentinelStable = (Test-Path -LiteralPath $sentinel -PathType Leaf) -and
        $sentinelHash -eq (Get-FileHash -LiteralPath $sentinel -Algorithm SHA256).Hash
    $targetAfter = Get-TreeSnapshot -Root $target
    $targetTreeStable = ($targetBefore -join "`n") -eq ($targetAfter -join "`n")
    $projectAfter = Get-TreeSnapshot -Root $project
    $projectTreeStable = ($projectBefore -join "`n") -eq ($projectAfter -join "`n")
    $reparseRejected = $output -match 'crosses a symbolic/reparse point'

    [pscustomobject]@{
        ExitCode = $exitCode
        ReparseRejected = $reparseRejected
        SentinelStable = $sentinelStable
        IsolatedTargetStable = $targetTreeStable
        IsolatedProjectStable = $projectTreeStable
    } | Format-List

    if ($exitCode -eq 0 -or -not $reparseRejected -or
        -not $sentinelStable -or -not $targetTreeStable -or -not $projectTreeStable) {
        throw 'Build StageRoot reparse boundary did not fail closed.'
    }
} finally {
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
