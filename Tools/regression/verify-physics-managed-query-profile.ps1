param([Parameter(Mandatory)][string]$Stage, [switch]$DisableTieredCompilation, [switch]$IsolateGameThread, [switch]$RequireCpuAccounting, [switch]$RequireBridgeCosts, [switch]$RequireDenseBatch, [ValidatePattern('^[a-zA-Z0-9-]+$')][string]$RunName='ManagedProfile')
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out=Join-Path $repo "Build/Obj/Phase19T2QueryBench/$RunName"
New-Item -ItemType Directory -Force $out|Out-Null
$receipts=@()
foreach($state in @('off','on','on','off')){
    if(Get-Process Player -ErrorAction SilentlyContinue){throw 'Profile comparison requires an isolated Player process'}
    $lines=@(& "$PSScriptRoot/verify-physics-b2-player.ps1" -Stage $Stage -QueryBenchmark -DisableTieredCompilation:$DisableTieredCompilation -IsolateGameThread:$IsolateGameThread -RequireCpuAccounting:$RequireCpuAccounting -QueryProfile $state -SmokeFrames 2000)
    $line=$lines|Where-Object {$_ -like 'PHYSICS_B2_PLAYER_OK evidence=*'}|Select-Object -Last 1
    if(!$line){throw 'Player profile benchmark receipt missing'}
    $evidence=$line.Substring($line.IndexOf('evidence=')+9)
    $dense=@()
    if($RequireDenseBatch){
        $dense=@(Get-Content "$evidence/player.out"|Where-Object {$_ -match '\[physics.player.dense-batch\]'}|ForEach-Object {($_ -split '\[physics.player.dense-batch\] ',2)[1]|ConvertFrom-Json})
        if($dense.Count -ne 3 -or @($dense.role|Select-Object -Unique).Count -ne 3 -or @($dense|Where-Object {$_.passed -ne 6 -or $_.failed -ne 0 -or !$_.complete}).Count){throw "Dense batch product gate failed: $evidence"}
    }
    $capture=$null
    if($state -eq 'on'){
        $raw=& "$repo/Build/Obj/Phase19T2QueryBench/CaptureProbe/capture-probe.exe" "$evidence/query.ceprof" 2> "$evidence/capture.stderr"
        if($LASTEXITCODE){throw "Product hierarchy capture failed: $evidence"}
        $capture=$raw|ConvertFrom-Json
        if($RequireBridgeCosts){
            foreach($count in @(16,64)){
                $cost=@($capture.bridgeCosts|Where-Object requests -EQ $count)
                if($cost.Count -ne 1 -or $cost[0].samples -lt 2640){throw "Bridge cost coverage missing for $count requests: $evidence"}
                foreach($field in @('totalMeanUs','validateMeanUs','prepareMeanUs','sdkMeanUs','translateMeanUs','commitMeanUs','residualMeanUs')){
                    $value=[double]$cost[0].$field
                    if([double]::IsNaN($value) -or [double]::IsInfinity($value) -or $value -lt 0){throw "Invalid bridge cost $field"}
                }
            }
        }
        $capture|ConvertTo-Json -Depth 10|Set-Content "$evidence/capture-result.json"
    }
    $receipts+=@{profile=$state;evidence=$evidence;denseBatchChecks=$dense;cpuPlacement=$(if($IsolateGameThread){Get-Content "$evidence/cpu-placement.json" -Raw|ConvertFrom-Json}else{$null});product=(Get-Content "$evidence/result.json" -Raw|ConvertFrom-Json);rows=@(Get-Content "$evidence/query-benchmark.json" -Raw|ConvertFrom-Json);capture=$capture}
    "T2_MANAGED_PROFILE_OK state=$state evidence=$evidence"
}
$hashes=@('Engine/SceneRuntime/ClrHost.cpp','Player/PlayerMain.cpp','Player/PlayerCommands.cpp','Engine/RuntimeHost/CommandCore/CommandDescriptorSeeds.cpp','GameScripts/PhysicsB2PlayerProbe.cs','Tools/regression/verify-physics-b2-player.ps1','Tools/regression/verify-physics-managed-query-profile.ps1','Tools/regression/physics_managed_query_capture_probe.cpp')|ForEach-Object {@{path=$_;sha256=(Get-FileHash (Join-Path $repo $_)).Hash}}
@{status='managed_query_profile_validated';configuration='Release';tieredCompilationDisabled=[bool]$DisableTieredCompilation;gameThreadIsolated=[bool]$IsolateGameThread;cpuAccountingRequired=[bool]$RequireCpuAccounting;receipts=$receipts;sourceHashes=$hashes;denseBatchRequired=[bool]$RequireDenseBatch;bridgeCostsRequired=[bool]$RequireBridgeCosts;performanceAccepted=$false;ordering='off/on/on/off processes; inner scalar/batch ABBA ABBA';excluded='Capture before Player frame publication: 1785 dropped counters';scope='Managed owner wall time, separate moving-scene blocks; excludes rendering and preparation'}|ConvertTo-Json -Depth 30|Set-Content "$out/result.json" -Encoding utf8
