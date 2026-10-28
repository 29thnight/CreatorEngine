param([Parameter(Mandatory)][string]$Tag,[Parameter(Mandatory)][string]$GuardTag)
$ErrorActionPreference='Stop';$root=$PSScriptRoot;$out=Join-Path $root 'results'
$suiteTag=$Tag
if($GuardTag.StartsWith("$Tag-")){throw 'Guard tag must use a different prefix'}
& "$root/experiments/20261026-small-array/with-guard.ps1" -Tag $GuardTag -Work {
 & "$root/run-module-acceptance.ps1" -Tag $suiteTag
}
foreach($run in 1..5){
 & "$root/build-modules/modules/Release/module_queue_acceptance.exe"|Set-Content "$out/$Tag-ppl-$run.csv" -Encoding utf8
 if($LASTEXITCODE -ne 0){throw 'FAA acceptance failed'}
 "FAA $run/5 collected"
}
Get-FileHash "$root/build-modules/modules/Release/module_concurrent_tests.exe"|ConvertTo-Json|Set-Content "$out/$Tag-concurrent-before.json" -Encoding utf8
foreach($run in 1..3){
 & "$root/build-modules/modules/Release/module_concurrent_tests.exe"|Set-Content "$out/$Tag-concurrent-$run.csv" -Encoding utf8
 if($LASTEXITCODE -ne 0){throw 'Concurrent throughput failed'}
 "Concurrent $run/3 collected"
}
$original=Get-Content "$out/20261026-module-accept1-reference-files.json" -Raw|ConvertFrom-Json
@($original|ForEach-Object {Get-FileHash $_.Path})|ConvertTo-Json|Set-Content "$out/$Tag-reference-files.json" -Encoding utf8
& "$root/run-module-mpsc-primary.ps1" -Tag $Tag
& "$root/verify-module-acceptance.ps1" -Tag $Tag -GuardTag $GuardTag
