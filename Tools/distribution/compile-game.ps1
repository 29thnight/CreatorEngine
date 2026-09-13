# Compatibility entry point; C# compilation runs in CreatorBuildTool.exe.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$EngineDistribution,
    [Parameter(Mandatory)][string]$Project,
    [Parameter(Mandatory)][string]$Output,
    [ValidateSet('Debug','Release')][string]$Config = 'Release',
    [string]$PrebuiltAssembly = '', [string]$LogPath = ''
)
$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
. (Join-Path $PSScriptRoot '../Invoke-CreatorBuildTool.ps1')
$forward = @{} + $PSBoundParameters
$forward['Config'] = $Config
Invoke-CreatorBuildTool -Command 'compile-game' -Parameters $forward -Repository $repository
