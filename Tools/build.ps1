# Compatibility entry point; the implementation is BuildTool/CreatorBuildTool.csproj.
[CmdletBinding()]
param(
    [ValidateSet('Game')][string]$Target = 'Game',
    [ValidateSet('Debug','Release')][string]$Config = 'Debug',
    [string]$Project = '',
    [ValidateSet('Project','Workspace','Tracked')][string]$InputMode = 'Project',
    [string]$RenderBackend = '', [string]$StartupScene = '',
    [switch]$BuildNative, [switch]$SkipVerify,
    [string]$EngineDistribution = '', [string]$GameScriptsAssembly = '',
    [switch]$Shipping,
    [ValidateRange(1,1000000)][int]$SmokeFrames = 120,
    [ValidateRange(10,3600)][int]$SmokeTimeoutSec = 180,
    [string]$StageRoot = ''
)
$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
. (Join-Path $PSScriptRoot 'Invoke-CreatorBuildTool.ps1')
$forward = @{} + $PSBoundParameters
$forward['Config'] = $Config
$forward['Repository'] = $repository
Invoke-CreatorBuildTool -Command 'package-game' -Parameters $forward -Repository $repository
