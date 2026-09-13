# Compatibility entry point; engine publishing runs in CreatorBuildTool.exe.
[CmdletBinding()]
param(
    [ValidateSet('Debug','Release')][string]$Config = 'Release',
    [string]$Repository = (Join-Path $PSScriptRoot '../..'),
    [string]$OutputRoot = '', [switch]$Build, [switch]$Shipping
)
$ErrorActionPreference = 'Stop'
$Repository = [IO.Path]::GetFullPath($Repository)
. (Join-Path $PSScriptRoot '../Invoke-CreatorBuildTool.ps1')
$forward = @{} + $PSBoundParameters
$forward['Config'] = $Config
$forward['Repository'] = $Repository
Invoke-CreatorBuildTool -Command 'publish-engine' -Parameters $forward -Repository $Repository
