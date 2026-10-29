# 2026-10-22 replacement: OnSimulate/Scope coroutine authoring was removed.
# Native F# tests cover startup gates, update isolation, reentrancy, teardown and reload.
# The owned Editor gate covers fixed updates, disable/enable, self removal, pause and two Play/Stop generations.
# Contact subscriptions additionally use verify-physics-contact-stream-http/player.ps1.
param([string]$Exe='', [string]$Work=$env:TEMP, [int]$TimeoutSeconds=300)
$configuration=if($Exe -match 'x64-Debug'){'Debug'}else{'Release'}
& "$PSScriptRoot/verify-fsharp-fixed-update.ps1" -Configuration $configuration -SkipNative -Product -EditorExe $Exe
if($LASTEXITCODE){exit $LASTEXITCODE}
