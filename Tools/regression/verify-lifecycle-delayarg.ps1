# Retired 2026-10-22: dedicated simulation delay API removed by user decision.
# Replacement proves API absence, fixed OnTickSimulate dispatch, and internal subscription ownership.
# It does not claim to accept the retired zero/NaN/infinite-delay semantics.
param([string]$Exe='', [string]$Work=$env:TEMP, [int]$TimeoutSeconds=300)
& "$PSScriptRoot/verify-csharp-fixed-update.ps1" -SkipNative
if($LASTEXITCODE){exit $LASTEXITCODE}
