[CmdletBinding()]
param([Parameter(Mandatory)][string]$Project, [Parameter(Mandatory)][string]$EngineDistribution)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '../Invoke-CreatorBuildTool.ps1')
$config = (Get-Content -LiteralPath (Join-Path $EngineDistribution 'engine.manifest.json') -Raw | ConvertFrom-Json).configuration
$forward = @{} + $PSBoundParameters
$forward['Config'] = $config
Invoke-CreatorBuildTool -Command 'select-engine' -Parameters $forward -Repository ([IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..')))
