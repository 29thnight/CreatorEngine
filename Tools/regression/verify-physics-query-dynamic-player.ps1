[CmdletBinding()]
param([Parameter(Mandatory)][string]$Stage)

$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out=Join-Path $repo ('Build/Obj/Phase19T2QueryBench/DynamicQueries-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $out|Out-Null
$receipts=@()

foreach($name in @('DOTNET_TieredCompilation','COMPlus_TieredCompilation')){
    if([Environment]::GetEnvironmentVariable($name)){throw "Inherited JIT override: $name"}
}

foreach($iteration in 1..2){
    if(Get-Process Player -ErrorAction SilentlyContinue){throw 'Dynamic query gate requires isolated Player'}

    $lines=@(& "$PSScriptRoot/verify-physics-b2-player.ps1" -Stage $Stage -ExpectedSceneGuid 16037efe-0cd6-4802-a24a-aafd56644346 -QueryStress -RequireMaximumBatch -QueryDynamic -SmokeFrames 2000)
    $line=$lines|Where-Object {$_ -like 'PHYSICS_B2_PLAYER_OK evidence=*'}|Select-Object -Last 1
    if(!$line){throw 'Dynamic Player evidence missing'}

    $evidence=$line.Substring($line.IndexOf('evidence=')+9)
    $stdout=Get-Content "$evidence/player.out" -Raw

    foreach($kind in @('batch','dense-batch')){
        $expected=if($kind -eq 'batch'){16}else{6}
        $checks=@([regex]::Matches($stdout,('\[physics.player.'+[regex]::Escape($kind)+'\] (\{[^\r\n]+\})'))|ForEach-Object {$_.Groups[1].Value|ConvertFrom-Json})

        if($checks.Count -ne 3 -or @($checks.role|Sort-Object -Unique).Count -ne 3 -or
           @($checks|Where-Object {$_.passed -ne $expected -or $_.failed -ne 0 -or !$_.complete}).Count){throw 'Baseline query checks incomplete'}
    }

    $receipts+=@{iteration=$iteration;evidence=$evidence;product=(Get-Content "$evidence/result.json" -Raw|ConvertFrom-Json);stress=(Get-Content "$evidence/query-stress.json" -Raw|ConvertFrom-Json);dynamic=(Get-Content "$evidence/query-dynamic.json" -Raw|ConvertFrom-Json);baselineChecks=104}
    "DYNAMIC_QUERY_PLAYER_OK iteration=$iteration evidence=$evidence"
}

$hashes=@{}
foreach($path in @('GameScripts/PhysicsB2PlayerProbe.cs','GameScripts/PhysicsB2DynamicQueryProbe.cs','Tools/regression/verify-physics-b2-player.ps1','Tools/regression/verify-physics-query-dynamic-player.ps1','Tools/regression/author-physics-query-dynamic.ps1','Tools/regression/fixtures/PhysicsQueryDynamic.creator')){
    $hashes[$path]=(Get-FileHash (Join-Path $repo $path)).Hash
}

@{result='PHYSICS_DYNAMIC_QUERY_PLAYER_OK';stage=$Stage;receipts=$receipts;sourceHashes=$hashes;performanceAccepted=$false;scope='Two fresh Release Player processes, default JIT/CPU; gravity-disabled non-contact dynamic128 out-and-back motion; observed pose changes and owner read windows are not physics tick counts; excludes Shipping/GPU solver/capture/performance acceptance'}|ConvertTo-Json -Depth 30|Set-Content "$out/result.json" -Encoding utf8
"PHYSICS_DYNAMIC_QUERY_PLAYER_OK evidence=$out"
