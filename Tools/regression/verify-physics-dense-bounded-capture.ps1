[CmdletBinding()]
param([Parameter(Mandatory)][string]$Stage)

$ErrorActionPreference='Stop'
foreach($name in @('DOTNET_TieredCompilation','COMPlus_TieredCompilation')){
    if([Environment]::GetEnvironmentVariable($name)){throw "Inherited JIT override: $name"}
}

$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out=Join-Path $repo ('Build/Obj/Phase19T2QueryBench/DenseBounded-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $out|Out-Null
$receipts=@()

foreach($iteration in 1..2){
    if(Get-Process Player -ErrorAction SilentlyContinue){throw 'Dense capture requires isolated Player'}

    $lines=@(& "$PSScriptRoot/verify-physics-b2-player.ps1" -Stage $Stage -ExpectedSceneGuid ebe286e4-199c-4405-967b-151ee5158a71 -QueryStress -RequireMaximumBatch -QueryBenchmark -QueryProfile on -BoundedDenseCapture -RequireCpuAccounting -SmokeFrames 2000)
    $line=$lines|Where-Object {$_ -like 'PHYSICS_B2_PLAYER_OK evidence=*'}|Select-Object -Last 1
    if(!$line){throw 'Dense Player evidence missing'}

    $evidence=$line.Substring($line.IndexOf('evidence=')+9)
    $raw=& "$repo/Build/Obj/Phase19T2QueryBench/CaptureProbe/capture-probe.exe" "$evidence/query.ceprof" "$evidence/bridge-costs.csv" --dense-bounded 2> "$evidence/capture.stderr"
    if($LASTEXITCODE){throw "Dense hierarchy/coverage capture failed: $evidence"}

    $capture=$raw|ConvertFrom-Json
    $capture|ConvertTo-Json -Depth 10|Set-Content "$evidence/capture-result.json" -Encoding utf8
    $receipts+=@{iteration=$iteration;evidence=$evidence;capture=$capture;product=(Get-Content "$evidence/result.json" -Raw|ConvertFrom-Json);query=(Get-Content "$evidence/query-stress.json" -Raw|ConvertFrom-Json)}
    "DENSE_BOUNDED_CAPTURE_OK iteration=$iteration evidence=$evidence"
}

$hashes=@{}
foreach($path in @('GameScripts/PhysicsB2PlayerProbe.cs','Tools/regression/verify-physics-b2-player.ps1','Tools/regression/physics_managed_query_capture_probe.cpp','Tools/regression/verify-physics-dense-bounded-capture.ps1','Dynamic_CPP/Assets/Scenes/PhysicsQueryStress.creator')){
    $hashes[$path]=(Get-FileHash (Join-Path $repo $path)).Hash
}

@{result='PHYSICS_DENSE_BOUNDED_CAPTURE_OK';receipts=$receipts;sourceHashes=$hashes;stage=$Stage;performanceAccepted=$false;scope='Two fresh Release Player processes; default JIT/CPU; 16 blocks, warmup2 and samples20 each; fixed product capture budget; hierarchy and full workload coverage, excludes p99/performance acceptance'}|ConvertTo-Json -Depth 30|Set-Content "$out/result.json" -Encoding utf8
"PHYSICS_DENSE_BOUNDED_CAPTURE_OK evidence=$out"
