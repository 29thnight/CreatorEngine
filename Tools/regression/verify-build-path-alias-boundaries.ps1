[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$buildScript = Join-Path $repoRoot 'Tools\build.ps1'
$pwshPath = (Get-Command 'pwsh.exe' -ErrorAction Stop).Source

if (-not ('CreatorEngineBuildShortPath' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;

public static class CreatorEngineBuildShortPath
{
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern uint GetShortPathNameW(
        string longPath, StringBuilder shortPath, uint bufferLength);

    public static string TryGet(string path)
    {
        StringBuilder buffer = new StringBuilder(32768);
        uint length = GetShortPathNameW(path, buffer, (uint)buffer.Capacity);
        if (length == 0 || length >= buffer.Capacity) return String.Empty;
        return buffer.ToString();
    }
}
'@
}

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

function Invoke-BuildBoundary {
    param(
        [Parameter(Mandatory)][string]$Project,
        [Parameter(Mandatory)][string]$StageRoot
    )

    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $pwshPath
    $startInfo.WorkingDirectory = $repoRoot
    $startInfo.UseShellExecute = $false
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    foreach ($argument in @(
        '-NoProfile', '-File', $buildScript,
        '-Config', 'Release', '-InputMode', 'Project',
        '-Project', $Project, '-SkipVerify', '-StageRoot', $StageRoot
    )) {
        [void]$startInfo.ArgumentList.Add($argument)
    }

    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    try {
        if (-not $process.Start()) { throw 'build path-boundary child failed to start.' }
        $stdoutTask = $process.StandardOutput.ReadToEndAsync()
        $stderrTask = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit(30000)) {
            $process.Kill($true)
            $process.WaitForExit()
            throw 'build path-boundary child did not fail within 30 seconds.'
        }
        $process.WaitForExit()
        return [pscustomobject]@{
            ExitCode = $process.ExitCode
            Output = $stdoutTask.GetAwaiter().GetResult() + "`n" +
                $stderrTask.GetAwaiter().GetResult()
        }
    } finally {
        $process.Dispose()
    }
}

$tempBase = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$tempPrefix = $tempBase.TrimEnd([char[]]@('\', '/')) + [IO.Path]::DirectorySeparatorChar
$testRoot = [IO.Path]::GetFullPath((Join-Path $tempBase `
    ('CreatorEngine-BuildAlias-' + [Guid]::NewGuid().ToString('N'))))
if (-not $testRoot.StartsWith($tempPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "build alias temp root escaped system temp: $testRoot"
}

$project = Join-Path $testRoot 'AliasProject'
$assets = Join-Path $project 'Assets'
$settings = Join-Path $project 'ProjectSetting'
try {
    New-Item -ItemType Directory -Path $assets, $settings | Out-Null
    [IO.File]::WriteAllText((Join-Path $assets 'asset-sentinel.txt'), 'asset sentinel',
        [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $settings 'settings-sentinel.asset'),
        'settings sentinel', [Text.UTF8Encoding]::new($false))
    $before = Get-TreeSnapshot -Root $project

    $extendedStage = '\\?\' + (Join-Path $assets 'extended-stage')
    $extended = Invoke-BuildBoundary -Project $project -StageRoot $extendedStage
    $extendedRejected = $extended.ExitCode -ne 0 -and
        $extended.Output -match 'device/extended path namespace' -and
        -not (Test-Path -LiteralPath (Join-Path $assets 'extended-stage'))

    $deviceStage = '\\.\' + (Join-Path $assets 'device-stage')
    $device = Invoke-BuildBoundary -Project $project -StageRoot $deviceStage
    $deviceRejected = $device.ExitCode -ne 0 -and
        $device.Output -match 'device/extended path namespace' -and
        -not (Test-Path -LiteralPath (Join-Path $assets 'device-stage'))

    $shortAssets = [CreatorEngineBuildShortPath]::TryGet($assets)
    $shortAliasStatus = 'SKIPPED_NO_8DOT3_ALIAS'
    $shortAliasAccepted = $true
    if (-not [string]::IsNullOrWhiteSpace($shortAssets) -and
        -not $shortAssets.Equals($assets, [StringComparison]::OrdinalIgnoreCase)) {
        $shortStage = Join-Path $shortAssets 'short-stage'
        $short = Invoke-BuildBoundary -Project $project -StageRoot $shortStage
        $shortAliasAccepted = $short.ExitCode -ne 0 -and
            $short.Output -match 'trees overlap' -and
            -not (Test-Path -LiteralPath $shortStage)
        $shortAliasStatus = if ($shortAliasAccepted) { 'REJECTED' } else { 'FAILED' }
    }

    $after = Get-TreeSnapshot -Root $project
    $projectStable = ($before -join "`n") -eq ($after -join "`n")
    [pscustomobject]@{
        ExtendedPathRejected = $extendedRejected
        DevicePathRejected = $deviceRejected
        ShortAlias = $shortAliasStatus
        IsolatedProjectStable = $projectStable
    } | Format-List

    if (-not $extendedRejected -or -not $deviceRejected -or
        -not $shortAliasAccepted -or -not $projectStable) {
        throw 'build path-alias boundary regression failed.'
    }
} finally {
    if (Test-Path -LiteralPath $testRoot) {
        $rootItem = Get-Item -LiteralPath $testRoot -Force
        if (($rootItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "refusing cleanup because build alias root became a reparse point: $testRoot"
        }
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
