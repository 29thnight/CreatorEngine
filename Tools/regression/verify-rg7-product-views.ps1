#Requires -Version 7.0
param(
    [Parameter(Mandatory)][string]$FixtureProject,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [string]$Python = 'python',
    [ValidateSet('Debug','Release')][string[]]$Configurations = @('Debug','Release'),
    [ValidateRange(0,32)][int]$PreparationSamples = 0,
    [switch]$RequireMemoryAccounting,
    [switch]$MemorySampling,
    [ValidateSet('OnFirst','OffFirst')][string]$RunOrder = 'OnFirst',
    [string]$CompilerFixtureDirectory = ''
)
$ErrorActionPreference = 'Stop'
$out = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $out)
{
    throw 'Use a new product evidence directory; failed evidence is preserved.'
}
New-Item -ItemType Directory -Path $out | Out-Null
$environment = @{}
foreach ($name in @('CREATOR_RENDERGRAPH_ALIASING','CREATOR_RENDERGRAPH_EXTEND_LIFETIMES',
    'CREATOR_EDITOR_WORKSPACE_DIR','CREATOR_EDITOR_LEGACY_INI'))
{
    $environment[$name] = [Environment]::GetEnvironmentVariable($name)
}
try
{
    foreach ($configuration in $Configurations)
    {
        $fixtureEvidence = $CompilerFixtureDirectory
        $modes = if ($RunOrder -eq 'OnFirst') { @('On','Off') } else { @('Off','On') }
        foreach ($mode in $modes)
        {
            $env:CREATOR_RENDERGRAPH_ALIASING = if ($mode -eq 'On') { '1' } else { '0' }
            $env:CREATOR_RENDERGRAPH_EXTEND_LIFETIMES = '0'
            $env:CREATOR_EDITOR_WORKSPACE_DIR = Join-Path $out "workspace-$configuration-$mode"
            $env:CREATOR_EDITOR_LEGACY_INI = Join-Path $env:CREATOR_EDITOR_WORKSPACE_DIR 'legacy.ini'
            New-Item -ItemType Directory -Path $env:CREATOR_EDITOR_WORKSPACE_DIR | Out-Null
            $arguments = @{Configuration=$configuration; FixtureProject=$FixtureProject;
                OutputDirectory=(Join-Path $out "$configuration-$mode"); ShowEditor=$true;
                PreparationSamples=$PreparationSamples; MemorySampling=$MemorySampling}
            if ($fixtureEvidence)
            {
                # Reuse only same-binary native fixture evidence; the existing
                # harness validates both hashes and the fixture success marker.
                $arguments.CompilerFixtureDirectory = $fixtureEvidence
            }
            & (Join-Path $PSScriptRoot 'verify-graph-recovery-rgv.ps1') @arguments
            $fixtureEvidence = Join-Path $out "$configuration-$mode"
            @{configuration=$configuration; mode=$mode; aliasing=$env:CREATOR_RENDERGRAPH_ALIASING;
                extendedLifetimes=$env:CREATOR_RENDERGRAPH_EXTEND_LIFETIMES} |
                ConvertTo-Json | Set-Content (Join-Path $out "$configuration-$mode/rg7-environment.json") -Encoding utf8
            "RG7_PRODUCT_RUN_OK configuration=$configuration mode=$mode"
        }
    }
    & (Join-Path $PSScriptRoot 'audit-rg7-product-views.ps1') -EvidenceDirectory $out -Python $Python -Configurations $Configurations
    if ($PreparationSamples -gt 0)
    {
        & (Join-Path $PSScriptRoot 'audit-rg7-product-preparation.ps1') -EvidenceDirectory $out `
            -Python $Python -Configurations $Configurations -ExpectedSamples $PreparationSamples `
            -RequireMemoryAccounting:$RequireMemoryAccounting
    }
}
finally
{
    foreach ($entry in $environment.GetEnumerator())
    {
        [Environment]::SetEnvironmentVariable($entry.Key, $entry.Value)
    }
}
