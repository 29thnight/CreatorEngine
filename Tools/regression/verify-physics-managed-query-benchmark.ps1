param([Parameter(Mandatory)][string]$Stage)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out=Join-Path $repo 'Build/Obj/Phase19T2QueryBench/Managed'
New-Item -ItemType Directory -Force $out|Out-Null
$receipts=@()
for($attempt=0;$attempt -lt 2;$attempt++){
    if(Get-Process Player -ErrorAction SilentlyContinue){throw 'Managed benchmark requires an isolated Player process'}
    $lines=@(& "$PSScriptRoot/verify-physics-b2-player.ps1" -Stage $Stage -QueryBenchmark -SmokeFrames 2000)
    $line=$lines|Where-Object {$_ -like 'PHYSICS_B2_PLAYER_OK evidence=*'}|Select-Object -Last 1
    if(!$line){throw 'Managed benchmark receipt missing'}
    $evidence=$line.Substring($line.IndexOf('evidence=')+9)
    $receipts+=@{evidence=$evidence;product=(Get-Content "$evidence/result.json" -Raw|ConvertFrom-Json);rows=@(Get-Content "$evidence/query-benchmark.json" -Raw|ConvertFrom-Json)}
    "T2_MANAGED_QUERY_BENCH_OK attempt=$attempt evidence=$evidence"
}
$hashes=@('GameScripts/PhysicsB2PlayerProbe.cs','Tools/regression/verify-physics-b2-player.ps1','Tools/regression/verify-physics-managed-query-benchmark.ps1')|ForEach-Object {@{path=$_;sha256=(Get-FileHash (Join-Path $repo $_)).Hash}}
@{status='managed_moving_query_benchmark_validated';configuration='Release';receipts=$receipts;sourceHashes=$hashes;profile=$false;performanceAccepted=$false;excluded='Initial overlapping Player run';scope='Managed synchronous mixed query wall time; movement occurs between blocks, requests fixed within each block'}|ConvertTo-Json -Depth 30|Set-Content "$out/result.json" -Encoding utf8
