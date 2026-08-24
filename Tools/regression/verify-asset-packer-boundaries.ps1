[CmdletBinding()]
param([string]$AssetPacker = '')

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if ([string]::IsNullOrWhiteSpace($AssetPacker)) {
$AssetPacker = Join-Path $repoRoot 'Bin\x64-Release\Tools\AssetPacker\AssetPacker.exe'
}
$packerPath = [IO.Path]::GetFullPath($AssetPacker)
if (-not (Test-Path -LiteralPath $packerPath -PathType Leaf)) {
    throw "AssetPacker is missing: $packerPath"
}

if (-not ('CreatorEngineShortPath' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;

public static class CreatorEngineShortPath
{
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern uint GetShortPathNameW(
        string longPath, StringBuilder shortPath, uint bufferLength);

    public static string TryGet(string path)
    {
        StringBuilder buffer = new StringBuilder(32768);
        uint length = GetShortPathNameW(path, buffer, (uint)buffer.Capacity);
        if (length == 0) return String.Empty;
        if (length >= buffer.Capacity)
            throw new InvalidOperationException("GetShortPathNameW buffer was too small.",
                new Win32Exception(Marshal.GetLastWin32Error()));
        return buffer.ToString();
    }
}
'@
}

function Invoke-AssetPacker {
    param(
        [Parameter(Mandatory)][string]$Assets,
        [Parameter(Mandatory)][string]$Settings,
        [Parameter(Mandatory)][string]$Output
    )

    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $packerPath
    $startInfo.WorkingDirectory = $repoRoot
    $startInfo.UseShellExecute = $false
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    foreach ($argument in @('--assets', $Assets, '--settings', $Settings, '--output', $Output)) {
        [void]$startInfo.ArgumentList.Add($argument)
    }

    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    try {
        if (-not $process.Start()) { throw 'AssetPacker child failed to start.' }
        $stdoutTask = $process.StandardOutput.ReadToEndAsync()
        $stderrTask = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit(30000)) {
            $process.Kill($true)
            $process.WaitForExit()
            throw 'AssetPacker child timed out.'
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
    ('CreatorEngine-AssetPacker-' + [Guid]::NewGuid().ToString('N'))))
if (-not $testRoot.StartsWith($tempPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "AssetPacker test root escaped system temp: $testRoot"
}

$assets = Join-Path $testRoot 'Assets'
$settings = Join-Path $testRoot 'ProjectSetting'
$outputRoot = Join-Path $testRoot 'Output'
$baselinePak = Join-Path $outputRoot 'baseline.pak'
$nestedOutput = Join-Path $assets 'must-not-exist.pak'
$junctionTarget = Join-Path $testRoot 'junction-target'
$childJunction = Join-Path $assets 'linked'
$junctionOutput = Join-Path $outputRoot 'junction.pak'
try {
    New-Item -ItemType Directory -Path $assets, $settings, $outputRoot, $junctionTarget | Out-Null
    [IO.File]::WriteAllText((Join-Path $assets 'asset.txt'), 'asset',
        [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $settings 'settings.asset'), 'settings',
        [Text.UTF8Encoding]::new($false))
    $outsideSentinel = Join-Path $junctionTarget 'outside.txt'
    [IO.File]::WriteAllText($outsideSentinel, 'outside sentinel',
        [Text.UTF8Encoding]::new($false))
    $outsideSentinelHash = (Get-FileHash -LiteralPath $outsideSentinel -Algorithm SHA256).Hash

    $baseline = Invoke-AssetPacker -Assets $assets -Settings $settings -Output $baselinePak
    if ($baseline.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $baselinePak -PathType Leaf)) {
        throw "AssetPacker baseline failed: $($baseline.Output)"
    }
    $baselineHash = (Get-FileHash -LiteralPath $baselinePak -Algorithm SHA256).Hash

    $nested = Invoke-AssetPacker -Assets $assets -Settings $settings -Output $nestedOutput
    $nestedRejected = $nested.ExitCode -ne 0 -and
        $nested.Output -match 'output must be outside package input roots' -and
        -not (Test-Path -LiteralPath $nestedOutput)

    New-Item -ItemType Junction -Path $childJunction -Target $junctionTarget | Out-Null
    $junctionResult = Invoke-AssetPacker -Assets $assets -Settings $settings `
        -Output $junctionOutput
    $outsideSentinelStable = (Test-Path -LiteralPath $outsideSentinel -PathType Leaf) -and
        $outsideSentinelHash -eq
            (Get-FileHash -LiteralPath $outsideSentinel -Algorithm SHA256).Hash
    $junctionRejected = $junctionResult.ExitCode -ne 0 -and
        $junctionResult.Output -match '(symbolic|reparse)' -and
        -not (Test-Path -LiteralPath $junctionOutput) -and $outsideSentinelStable

    $junctionItem = Get-Item -LiteralPath $childJunction -Force
    if (($junctionItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) {
        throw "test child junction changed type: $childJunction"
    }
    Remove-Item -LiteralPath $childJunction -Force

    $extendedNestedOutput = '\\?\' + (Join-Path $assets 'extended\must-not-exist.pak')
    $extendedResult = Invoke-AssetPacker -Assets $assets -Settings $settings `
        -Output $extendedNestedOutput
    $extendedRejected = $extendedResult.ExitCode -ne 0 -and
        $extendedResult.Output -match 'device/extended path namespace' -and
        -not (Test-Path -LiteralPath (Join-Path $assets 'extended'))

    $deviceNestedOutput = '\\.\' + (Join-Path $assets 'device-must-not-exist.pak')
    $deviceResult = Invoke-AssetPacker -Assets $assets -Settings $settings `
        -Output $deviceNestedOutput
    $deviceRejected = $deviceResult.ExitCode -ne 0 -and
        $deviceResult.Output -match 'device/extended path namespace' -and
        -not (Test-Path -LiteralPath (Join-Path $assets 'device-must-not-exist.pak'))

    $shortAssets = [CreatorEngineShortPath]::TryGet($assets)
    $shortAliasStatus = 'SKIPPED_NO_8DOT3_ALIAS'
    $shortAliasAccepted = $true
    if (-not [string]::IsNullOrWhiteSpace($shortAssets) -and
        -not $shortAssets.Equals($assets, [StringComparison]::OrdinalIgnoreCase)) {
        $shortOutput = Join-Path $shortAssets 'short-must-not-exist.pak'
        $shortResult = Invoke-AssetPacker -Assets $assets -Settings $settings -Output $shortOutput
        $shortAliasAccepted = $shortResult.ExitCode -ne 0 -and
            $shortResult.Output -match 'outside package input roots' -and
            -not (Test-Path -LiteralPath $shortOutput)
        $shortAliasStatus = if ($shortAliasAccepted) { 'REJECTED' } else { 'FAILED' }
    }

    $assetStream = [IO.File]::Open((Join-Path $assets 'asset.txt'), [IO.FileMode]::Open,
        [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
    try {
        $lateFailureResult = Invoke-AssetPacker -Assets $assets -Settings $settings `
            -Output $baselinePak
    } finally {
        $assetStream.Dispose()
    }
    $lateFailureOutputPreserved = $lateFailureResult.ExitCode -ne 0 -and
        $baselineHash -eq (Get-FileHash -LiteralPath $baselinePak -Algorithm SHA256).Hash
    $candidateResidue = @(Get-ChildItem -LiteralPath $testRoot -File -Recurse -Force |
        Where-Object { $_.Name -like '*.candidate.*' })

    [pscustomobject]@{
        BaselineCreated = $true
        NestedOutputRejected = $nestedRejected
        ChildJunctionRejected = $junctionRejected
        ChildJunctionExitCode = $junctionResult.ExitCode
        OutsideSentinelStable = $outsideSentinelStable
        ExtendedPathRejected = $extendedRejected
        DevicePathRejected = $deviceRejected
        ShortAlias = $shortAliasStatus
        LateFailureOutputPreserved = $lateFailureOutputPreserved
        CandidateResidueCount = $candidateResidue.Count
    } | Format-List

    if (-not $nestedRejected -or -not $junctionRejected -or
        -not $extendedRejected -or -not $deviceRejected -or -not $shortAliasAccepted -or
        -not $lateFailureOutputPreserved -or $candidateResidue.Count -ne 0) {
        throw 'AssetPacker boundary regression failed.'
    }
} finally {
    if (Test-Path -LiteralPath $childJunction) {
        $junctionItem = Get-Item -LiteralPath $childJunction -Force
        if (($junctionItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) {
            throw "refusing cleanup because the expected junction changed type: $childJunction"
        }
        Remove-Item -LiteralPath $childJunction -Force
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
