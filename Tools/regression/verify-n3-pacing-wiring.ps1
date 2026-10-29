[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out=Join-Path $repo ('Build/Verification/N3Pacing/Wiring-'+[guid]::NewGuid().ToString('N'))
$root=Join-Path $out 'Fixture'
New-Item -ItemType Directory -Force "$root/Tools/regression","$root/Engine/Scripting/Bindings","$root/Stage"|Out-Null
foreach($name in @('run-all.ps1','network_handoff_suite.py','network-handoff.suite.json')){Copy-Item "$PSScriptRoot/$name" "$root/Tools/regression"}
$manifest=Get-Content "$root/Tools/regression/network-handoff.suite.json" -Raw|ConvertFrom-Json
$products=@('verify-simulation-exchange-http.ps1','verify-scene-gc.ps1','verify-archive-parser-product.ps1','verify-n1-diagnostic-product.ps1','verify-n3-pacing-product.ps1')
foreach($path in @($manifest.gates.script)+@($products|ForEach-Object {'Tools/regression/'+$_})){
 $target=Join-Path $root $path;New-Item -ItemType Directory -Force (Split-Path $target)|Out-Null
 'param([string]$Configuration)'+"`nexit 0"|Set-Content $target -Encoding utf8
}
''|Set-Content "$root/Editor.exe";''|Set-Content "$root/Stage/Player.runtime.dll"
$shell=(Get-Process -Id $PID).Path
function Run([string]$label){
 & $shell -NoProfile -File "$root/Tools/regression/run-all.ps1" -Suite NetworkHandoff -Configuration All -Product -EditorExe "$root/Editor.exe" -CookedStage "$root/Stage" *> "$out/$label.log"
 $exit=$LASTEXITCODE
 $result=Get-ChildItem "$root/Build/Verification/NDA" -Directory -Filter 'Suite-*'|Sort-Object LastWriteTime -Descending|Select-Object -First 1
 return @{exit=$exit;data=(Get-Content (Join-Path $result.FullName 'result.json') -Raw|ConvertFrom-Json)}
}
$ok=Run 'coverage'
if($ok.exit -ne 0 -or @($ok.data.passed).Count -ne 15){throw 'Fixture runner did not require all 15 registered groups'}
$cases=@()
foreach($mode in @('cadence','moving')){
 $failure=if($mode -eq 'moving'){'if($MovingQuality){exit 37};exit 0'}else{'if(!$MovingQuality){exit 37};exit 0'}
 'param([string]$Configuration,[int[]]$Rates,[switch]$MovingQuality)'+"`n"+$failure|Set-Content "$root/Tools/regression/verify-n3-pacing-product.ps1" -Encoding utf8
 $r=Run $mode;$expected=if($mode -eq 'moving'){'N3MovingGpuProduct'}else{'N3PacingProduct'}
 if($r.exit -eq 0 -or !($r.data.failures|Where-Object {$_.gate -eq $expected -and $_.exitCode -eq 37})){throw "N3 $mode failure was skipped"}
 $cases+=@{mode=$mode;gate=$expected;failedGateExit=37;propagated=$true}
}
@{result='N3_PACING_WIRING_OK';fixtureOnly=$true;registeredGroups=15;cases=$cases;productionSourcesChanged=$false}|ConvertTo-Json -Depth 8|Set-Content "$out/result.json" -Encoding utf8
"N3_PACING_WIRING_OK evidence=$out"
