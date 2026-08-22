[CmdletBinding()]
param(
    [string]$MSBuild = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$project = Join-Path $repoRoot 'Player\Player.vcxproj'

if ([string]::IsNullOrWhiteSpace($MSBuild)) {
    $command = Get-Command 'MSBuild.exe' -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        $MSBuild = $command.Source
    } else {
        $programFilesX86 = ${env:ProgramFiles(x86)}
        $vswhere = Join-Path $programFilesX86 'Microsoft Visual Studio\Installer\vswhere.exe'
        if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
            throw 'MSBuild.exe is not on PATH and vswhere.exe was not found.'
        }
        $found = @(& $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild `
            -find 'MSBuild\**\Bin\amd64\MSBuild.exe')
        if ($LASTEXITCODE -ne 0 -or $found.Count -eq 0) {
            throw 'vswhere.exe did not find an amd64 MSBuild.exe.'
        }
        $MSBuild = $found[0]
    }
}
$MSBuild = [IO.Path]::GetFullPath($MSBuild)
if (-not (Test-Path -LiteralPath $MSBuild -PathType Leaf)) {
    throw "MSBuild.exe not found: $MSBuild"
}

$output = @(& $MSBuild $project `
    '-p:Configuration=Release' '-p:Platform=x64' `
    '-getProperty:PreferredToolArchitecture,VCToolsVersion,IntDir')
if ($LASTEXITCODE -ne 0) {
    throw "MSBuild property query failed with exit code $LASTEXITCODE."
}

$properties = (($output -join [Environment]::NewLine) | ConvertFrom-Json).Properties
if ($properties.PreferredToolArchitecture -ne 'x64') {
    throw "Release|x64 selected host tools '$($properties.PreferredToolArchitecture)', expected 'x64'."
}

[pscustomobject]@{
    MSBuild = $MSBuild
    PreferredToolArchitecture = $properties.PreferredToolArchitecture
    VCToolsVersion = $properties.VCToolsVersion
    IntDir = $properties.IntDir
} | Format-List

Write-Host '[MSBUILD TOOL ARCH] passed.' -ForegroundColor Green
