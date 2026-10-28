param([string]$Tag='20261026-smt-acceptance',[string]$GuardTag='20261026-smt-control')
$ErrorActionPreference='Stop';$root=$PSScriptRoot;$out=Join-Path $root 'results'
& "$root/verify-mpsc-results.ps1" -Tag $Tag
$receipt=Get-Content "$out/$Tag-environment.json" -Raw|ConvertFrom-Json
$guard=Get-Content "$out/$GuardTag-guard.json" -Raw|ConvertFrom-Json
if(!$guard.success -or !$guard.aliveAtWorkEnd){throw 'Guard coverage failed'}
foreach($file in @($guard.helper,$guard.script)){if((Get-FileHash $file.Path).Hash -ne $file.Hash){throw 'Guard source/binary receipt differs'}}
$start=[DateTimeOffset]::Parse($guard.started).UtcDateTime;$end=[DateTimeOffset]::Parse($guard.workEnded).UtcDateTime
$caseFiles=@(Get-ChildItem "$out/$Tag-cases" -File -Filter '*.csv')
if($caseFiles.Count -ne 246){throw 'Expected all 246 general case processes'}
foreach($file in $caseFiles){if($file.LastWriteTimeUtc -lt $start -or $file.LastWriteTimeUtc -gt $end){throw "Case outside guard lifetime: $($file.Name)"}}
$old=Get-Content "$out/20261026-final5-environment.json" -Raw|ConvertFrom-Json
foreach($target in @('container_tests','library_benchmark')){
 $a=@($old.binaries|Where-Object {[IO.Path]::GetFileNameWithoutExtension($_.Path) -eq $target})
 $b=@($receipt.binaries|Where-Object {[IO.Path]::GetFileNameWithoutExtension($_.Path) -eq $target})
 if($a.Count -ne 1 -or $b.Count -ne 1 -or $a[0].Hash -ne $b[0].Hash){throw 'Original general benchmark binary changed'}
}
$validated=Get-Content "$out/20261026-final2-environment.json" -Raw|ConvertFrom-Json
$hashes=@{};foreach($file in $validated.sources){$hashes[$file.Path]=$file.Hash}
$headers=@($receipt.sources|Where-Object {$_.Path -like '*\include\*'})
foreach($file in $headers){if($hashes[$file.Path] -ne $file.Hash){throw 'Library header differs from sanitizer-tested source'}}
$proof=[ordered]@{tag=$Tag;guardTag=$GuardTag;guardMaintained=$true;caseProcessCount=$caseFiles.Count;generalBinaryUnchanged=$true;sanitizerHeaderCount=$headers.Count;guardSource=@(Get-FileHash "$root/experiments/20261026-small-array/smt_guard.cpp","$root/experiments/20261026-small-array/with-guard.ps1"|Select-Object Path,Hash);inputs=@(Get-FileHash "$out/$Tag-environment.json","$out/$GuardTag-guard.json"|Select-Object Path,Hash)}
$proof|ConvertTo-Json -Depth 6|Set-Content "$out/$Tag-control-proof.json" -Encoding utf8
"SMT_ACCEPTANCE_VERIFIED: 246 general processes covered, original binaries unchanged, $($headers.Count) sanitizer headers match, guard exited after work."
