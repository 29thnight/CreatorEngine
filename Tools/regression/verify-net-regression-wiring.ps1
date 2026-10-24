[CmdletBinding()]
param([Parameter(Mandatory)][string]$EvidenceRoot)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$root=Join-Path $EvidenceRoot 'RunnerCanary'
New-Item -ItemType Directory -Force "$root/Tools/regression","$root/ScriptCore"|Out-Null
foreach($name in @('run-all.ps1','network_handoff_suite.py','network-handoff.suite.json')){Copy-Item "$PSScriptRoot/$name" "$root/Tools/regression"}
$manifest=Get-Content "$root/Tools/regression/network-handoff.suite.json" -Raw|ConvertFrom-Json
foreach($gate in $manifest.gates) {
    $path=Join-Path $root $gate.script
    New-Item -ItemType Directory -Force (Split-Path $path)|Out-Null
    $exitCode=if($gate.id -eq 'ND-a'){37}else{0}
    "param([string]`$Configuration)`nWrite-Output 'runner fixture only'`nexit $exitCode"|Set-Content $path -Encoding utf8
}
$shell=(Get-Process -Id $PID).Path
& $shell -NoProfile -File "$root/Tools/regression/run-all.ps1" -Suite NetworkHandoff -Configuration Release *> "$root/failure.log"
if(!$LASTEXITCODE){throw 'An injected ND-a failure produced a successful suite'}
$resultFile=Get-ChildItem "$root/Build/Verification/NDA" -Recurse -File -Filter result.json|Select-Object -First 1
if(!$resultFile){throw 'Failure propagation receipt missing'}
$result=Get-Content $resultFile.FullName -Raw|ConvertFrom-Json
if($result.result -ne 'NETWORK_HANDOFF_REGRESSIONS_FAILED' -or !@($result.failures|Where-Object {$_.gate -eq 'ND-a-Release' -and $_.exitCode -eq 37}).Count){throw 'ND-a exit code was not preserved'}
if((Get-Content "$root/failure.log" -Raw) -match 'NETWORK_HANDOFF_REGRESSIONS_OK gates='){throw 'A failing suite emitted a success marker'}
$manifest.gates=@($manifest.gates|Where-Object id -ne 'ND-a')
$manifest|ConvertTo-Json -Depth 6|Set-Content "$root/Tools/regression/network-handoff.suite.json" -Encoding utf8
& $shell -NoProfile -File "$root/Tools/regression/run-all.ps1" -Suite NetworkHandoff -Configuration Release *> "$root/removal.log"
if(!$LASTEXITCODE -or (Get-Content "$root/removal.log" -Raw) -notmatch 'NET_SUITE_REGISTRATION'){throw 'Removed ND-a registration was not rejected by the real launcher'}
$manifest=Get-Content "$PSScriptRoot/network-handoff.suite.json" -Raw|ConvertFrom-Json
$manifest.gates=@($manifest.gates|Where-Object id -ne 'W4-ND-b')
$manifest|ConvertTo-Json -Depth 6|Set-Content "$root/Tools/regression/network-handoff.suite.json" -Encoding utf8
& $shell -NoProfile -File "$root/Tools/regression/run-all.ps1" -Suite NetworkHandoff -Configuration Release *> "$root/observation-removal.log"
if(!$LASTEXITCODE -or (Get-Content "$root/observation-removal.log" -Raw) -notmatch 'NET_SUITE_REGISTRATION'){throw 'Removed W4/ND-b registration was not rejected'}
$manifest=Get-Content "$PSScriptRoot/network-handoff.suite.json" -Raw|ConvertFrom-Json
$manifest.gates=@($manifest.gates|Where-Object id -ne 'N1')
$manifest|ConvertTo-Json -Depth 6|Set-Content "$root/Tools/regression/network-handoff.suite.json" -Encoding utf8
& $shell -NoProfile -File "$root/Tools/regression/run-all.ps1" -Suite NetworkHandoff -Configuration Release *> "$root/archive-removal.log"
if(!$LASTEXITCODE -or (Get-Content "$root/archive-removal.log" -Raw) -notmatch 'NET_SUITE_REGISTRATION'){throw 'Removed N1 registration was not rejected'}
$manifest=Get-Content "$PSScriptRoot/network-handoff.suite.json" -Raw|ConvertFrom-Json
$manifest.gates=@($manifest.gates|Where-Object id -ne 'HeadlessBoundary')
$manifest|ConvertTo-Json -Depth 6|Set-Content "$root/Tools/regression/network-handoff.suite.json" -Encoding utf8
& $shell -NoProfile -File "$root/Tools/regression/run-all.ps1" -Suite NetworkHandoff -Configuration Release *> "$root/headless-removal.log"
if(!$LASTEXITCODE -or (Get-Content "$root/headless-removal.log" -Raw) -notmatch 'NET_SUITE_REGISTRATION'){throw 'Removed headless registration was not rejected'}
@{result='NET_REGRESSION_WIRING_CANARIES_OK';failedGateExit=37;failurePropagated=$true;removedGateRejected=$true;removedObservationGateRejected=$true;removedArchiveGateRejected=$true;removedHeadlessGateRejected=$true;productionSourcesChanged=$false}|ConvertTo-Json|Set-Content "$root/result.json" -Encoding utf8
'NET_REGRESSION_WIRING_CANARIES_OK failure=37 removal=rejected observationRemoval=rejected archiveRemoval=rejected headlessRemoval=rejected'
