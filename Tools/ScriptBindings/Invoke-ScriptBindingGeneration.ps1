#Requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateSet('Debug', 'Release')][string]$Configuration,
    [ValidateSet('x64')][string]$Platform = 'x64',
    [ValidateSet('true', 'false')][string]$EngineShipping = 'false',
    [ValidateSet('true', 'false')][string]$EngineAsan = 'false',
    [Parameter(Mandatory)][string]$OutputDirectory,
    [string]$MSBuild = '',
    [string]$ReflgenTargets = '',
    [string]$ReflgenExecutable = '',
    [string]$ReflgenOutputDirectory = '',
    [string]$PowerShellExecutable = 'pwsh',
    [string]$VcpkgInstalledDirectory = '',
    [string]$VcpkgTriplet = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not $IsWindows) {
    throw 'Source-checkout ScriptCore generation requires Windows and the VS x64 C++ toolchain. Published engine consumers use the already-built ScriptCore.dll.'
}

$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$project = Join-Path $repository 'Engine\SceneRuntime\SceneRuntime.vcxproj'

if ([string]::IsNullOrWhiteSpace($MSBuild)) {
    # A Developer PowerShell already identifies the intended installation.
    if (-not [string]::IsNullOrWhiteSpace($env:VSINSTALLDIR)) {
        $candidate = Join-Path $env:VSINSTALLDIR 'MSBuild\Current\Bin\amd64\MSBuild.exe'
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { $MSBuild = $candidate }
    }
    if ([string]::IsNullOrWhiteSpace($MSBuild)) {
        $programFilesX86 = ${env:ProgramFiles(x86)}
        if ([string]::IsNullOrWhiteSpace($programFilesX86)) {
            throw 'Visual Studio was not found. Set EngineScriptBindingsMSBuild to the VS x64 MSBuild.exe path.'
        }
        $vswhere = Join-Path $programFilesX86 'Microsoft Visual Studio\Installer\vswhere.exe'
        if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
            throw 'vswhere.exe was not found. Set EngineScriptBindingsMSBuild to the VS x64 MSBuild.exe path.'
        }
        $found = @(& $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            -find 'MSBuild\Current\Bin\amd64\MSBuild.exe')
        if ($LASTEXITCODE -ne 0 -or $found.Count -eq 0) {
            throw 'Visual Studio with x64 MSBuild and C++ tools was not found. Source-checkout ScriptCore generation needs the engine native build prerequisites.'
        }
        $MSBuild = $found[0]
    }
}
$MSBuild = [IO.Path]::GetFullPath($MSBuild)
if (-not (Test-Path -LiteralPath $MSBuild -PathType Leaf)) {
    throw "VS MSBuild.exe not found: $MSBuild"
}

# Use a fresh VS MSBuild process, not the .NET SDK's MSBuild implementation.
# Do not inherit managed-only build properties such as TargetFramework or
# RuntimeIdentifier, and never ask this entry point to build native binaries.
$arguments = [Collections.Generic.List[string]]::new()
foreach ($argument in @(
    $project,
    '/nologo', '/verbosity:minimal', '/nodeReuse:false',
    '/target:EngineGenerateScriptBindings',
    "/property:Configuration=$Configuration",
    "/property:Platform=$Platform",
    "/property:EngineShipping=$EngineShipping",
    "/property:EngineAsan=$EngineAsan",
    '/property:PreferredToolArchitecture=x64',
    "/property:EngineScriptBindingsOutputDirectory=$([IO.Path]::GetFullPath($OutputDirectory))"
)) { $arguments.Add($argument) }

foreach ($property in @(
    @{ Name = 'EngineReflgenTargets'; Value = $ReflgenTargets },
    @{ Name = 'ReflgenExecutable'; Value = $ReflgenExecutable },
    @{ Name = 'ReflgenOutputDirectory'; Value = $ReflgenOutputDirectory },
    @{ Name = 'EngineScriptBindingsPowerShell'; Value = $PowerShellExecutable },
    @{ Name = 'VcpkgInstalledDir'; Value = $VcpkgInstalledDirectory },
    @{ Name = 'VcpkgTriplet'; Value = $VcpkgTriplet }
)) {
    if (-not [string]::IsNullOrWhiteSpace($property.Value)) {
        $arguments.Add("/property:$($property.Name)=$($property.Value)")
    }
}

& $MSBuild @arguments
if ($LASTEXITCODE -ne 0) {
    throw "Engine script binding generation failed (MSBuild exit code $LASTEXITCODE)."
}
