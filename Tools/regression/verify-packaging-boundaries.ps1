[CmdletBinding()]
param([string]$Stage = '')

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$pwshPath = (Get-Command 'pwsh.exe' -ErrorAction Stop).Source
$cases = @(
    [pscustomobject]@{ Name = 'AssetPacker boundaries'; Script = 'verify-asset-packer-boundaries.ps1'; UsesStage = $false },
    [pscustomobject]@{ Name = 'Build StageRoot junction'; Script = 'verify-build-stage-boundary.ps1'; UsesStage = $false },
    [pscustomobject]@{ Name = 'Build path aliases'; Script = 'verify-build-path-alias-boundaries.ps1'; UsesStage = $false },
    [pscustomobject]@{ Name = 'Player runtime junction'; Script = 'verify-player-runtime-boundary.ps1'; UsesStage = $true },
    [pscustomobject]@{ Name = 'Player missing pak'; Script = 'verify-player-missing-pak.ps1'; UsesStage = $true },
    [pscustomobject]@{ Name = 'Player normal exit'; Script = 'verify-player-normal-exit.ps1'; UsesStage = $true }
)

foreach ($case in $cases) {
    Write-Host "[PACKAGING REGRESSION] $($case.Name)" -ForegroundColor Cyan
    $arguments = @('-NoProfile', '-File', (Join-Path $PSScriptRoot $case.Script))
    if ($case.UsesStage -and -not [string]::IsNullOrWhiteSpace($Stage)) {
        $arguments += @('-Stage', [IO.Path]::GetFullPath($Stage))
    }
    & $pwshPath @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$($case.Name) failed with exit code $LASTEXITCODE."
    }
}

Write-Host '[PACKAGING REGRESSION] all cases passed.' -ForegroundColor Green
