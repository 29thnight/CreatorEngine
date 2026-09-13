[CmdletBinding()]
param([Parameter(Mandatory)][string]$DevelopmentProject, [string]$EngineDistribution = (Join-Path $PSScriptRoot '../..'), [switch]$SelectEngine)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '../Invoke-CreatorBuildTool.ps1')
$config = (Get-Content -LiteralPath (Join-Path $EngineDistribution 'engine.manifest.json') -Raw | ConvertFrom-Json).configuration
$forward = @{} + $PSBoundParameters
$forward['Config'] = $config
$forward['EngineDistribution'] = $EngineDistribution
Invoke-CreatorBuildTool -Command 'open-project' -Parameters $forward -Repository ([IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..')))
