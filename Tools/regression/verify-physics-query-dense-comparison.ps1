param([Parameter(Mandatory)][string]$BeforeStage, [Parameter(Mandatory)][string]$AfterStage, [ValidatePattern('^[a-zA-Z0-9-]+$')][string]$RunName='ManagedDenseComparison', [switch]$DefaultJit, [switch]$OffOnly, [switch]$QueryStress, [string]$BeforeProbeSource, [string]$AfterProbeSource, [string]$ExpectedSceneGuid='5703e1d4-b1f5-4a32-b047-dd351c713483')
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out=Join-Path $repo "Build/Obj/Phase19T2QueryBench/$RunName"
New-Item -ItemType Directory -Force $out|Out-Null
$matched=$null
if($BeforeProbeSource -or $AfterProbeSource){
    if(!$BeforeProbeSource -or !$AfterProbeSource){throw 'Both probe sources are required'}
    $beforeHash=(Get-FileHash -LiteralPath $BeforeProbeSource).Hash
    $afterHash=(Get-FileHash -LiteralPath $AfterProbeSource).Hash
    if($beforeHash -ne $afterHash){throw 'Managed probe sources differ'}
    $matched=@{before=$BeforeProbeSource;after=$AfterProbeSource;sha256=$beforeHash}
}
$jitEnvironment=@{}
foreach($key in @('DOTNET_TieredCompilation','COMPlus_TieredCompilation')){
    $jitEnvironment[$key]=[Environment]::GetEnvironmentVariable($key)
    if($DefaultJit -and $jitEnvironment[$key]){throw "Default JIT run requires no inherited override: $key"}
}
$items=@(@('before','off'),@('after','off'),@('after','off'),@('before','off'))
if(!$OffOnly){$items+=@(@('after','on'),@('after','on'))}
$receipts=@()
foreach($item in $items){
    if(Get-Process Player -ErrorAction SilentlyContinue){throw 'Comparison requires isolated Player'}
    $revision=$item[0]; $state=$item[1]
    $stage=if($revision -eq 'before'){$BeforeStage}else{$AfterStage}
    $lines=@(& "$PSScriptRoot/verify-physics-b2-player.ps1" -Stage $stage -ExpectedSceneGuid $ExpectedSceneGuid -QueryBenchmark -QueryStress:$QueryStress -DisableTieredCompilation:(!$DefaultJit) -RequireCpuAccounting -QueryProfile $state -SmokeFrames 2000)
    $line=$lines|Where-Object {$_ -like 'PHYSICS_B2_PLAYER_OK evidence=*'}|Select-Object -Last 1
    if(!$line){throw 'Player comparison receipt missing'}
    $evidence=$line.Substring($line.IndexOf('evidence=')+9)
    $dense=@()
    if($revision -eq 'after' -or $matched){
        $dense=@(Get-Content "$evidence/player.out"|Where-Object {$_ -match '\[physics.player.dense-batch\]'}|ForEach-Object {($_ -split '\[physics.player.dense-batch\] ',2)[1]|ConvertFrom-Json})
        if($dense.Count -ne 3 -or @($dense.role|Select-Object -Unique).Count -ne 3 -or @($dense|Where-Object {$_.passed -ne 6 -or $_.failed -ne 0 -or !$_.complete}).Count){throw "Dense batch gate failed: $evidence"}
    }
    $stress=@()
    if($QueryStress){
        $stress=@(Get-Content "$evidence/player.out"|Where-Object {$_ -match '\[physics.player.stress\]'}|ForEach-Object {($_ -split '\[physics.player.stress\] ',2)[1]|ConvertFrom-Json})
        if($stress.Count -ne 1 -or $stress[0].passed -ne 7 -or $stress[0].failed -ne 0 -or !$stress[0].complete){throw "Stress gate failed: $evidence"}
        $bench=@(Get-Content "$evidence/query-benchmark.json" -Raw|ConvertFrom-Json)
        if(@($bench|Where-Object {$_.workload -ne 'dense128' -or $_.overlapRequired -ne 128}).Count){throw 'Stress benchmark workload mismatch'}
    }
    $capture=$null
    if($state -eq 'on'){
        $raw=& "$repo/Build/Obj/Phase19T2QueryBench/CaptureProbe/capture-probe.exe" "$evidence/query.ceprof" 2> "$evidence/capture.stderr"
        if($LASTEXITCODE){throw "Capture hierarchy failed: $evidence"}
        $capture=$raw|ConvertFrom-Json
        foreach($count in @(16,64)){
            if(@($capture.bridgeCosts|Where-Object {$_.requests -eq $count -and $_.samples -ge 2640}).Count -ne 1){throw 'Incomplete bridge coverage'}
        }
        $capture|ConvertTo-Json -Depth 12|Set-Content "$evidence/capture-result.json"
    }
    $receipts+=@{revision=$revision;profile=$state;evidence=$evidence;stressChecks=$stress;denseBatchChecks=$dense;product=(Get-Content "$evidence/result.json" -Raw|ConvertFrom-Json);rows=@(Get-Content "$evidence/query-benchmark.json" -Raw|ConvertFrom-Json);capture=$capture}
    "DENSE_COMPARISON_OK revision=$revision profile=$state evidence=$evidence"
}
$hashes=@('Engine/SceneRuntime/ClrHost.cpp','GameScripts/PhysicsB2PlayerProbe.cs','Tools/regression/verify-physics-query-dense-comparison.ps1','Tools/regression/physics_managed_query_capture_probe.cpp')|ForEach-Object {@{path=$_;sha256=(Get-FileHash (Join-Path $repo $_)).Hash}}
$result=@{status='dense_comparison_validated';tieredCompilationDisabled=(!$DefaultJit);queryStress=[bool]$QueryStress;offOnly=[bool]$OffOnly;matchedProbe=$matched;jitEnvironment=$jitEnvironment;receipts=$receipts;beforeStage=$BeforeStage;afterStage=$AfterStage;sourceHashes=$hashes;performanceAccepted=$false;ordering=$(if($OffOnly){'before/after/after/before off processes; inner scalar/batch ABBA ABBA'}else{'before/after/after/before off processes, then after on/on; inner scalar/batch ABBA ABBA'});scope='Fresh before/after Release processes, default CPU placement; JIT override and matched managed probe are recorded explicitly; authored setup outside timing'}
$result|ConvertTo-Json -Depth 35|Set-Content "$out/result.json" -Encoding utf8
$after=@($receipts|Where-Object revision -EQ 'after')
# Preserve actual execution order rather than claiming process-level off/on ABBA.
@{status='managed_query_profile_validated';configuration='Release';tieredCompilationDisabled=(!$DefaultJit);gameThreadIsolated=$false;cpuAccountingRequired=$true;denseBatchRequired=$true;bridgeCostsRequired=$true;receipts=$after;sourceHashes=$hashes;performanceAccepted=$false;ordering=$(if($OffOnly){'after off/off subset of before/after/after/before; inner scalar/batch ABBA ABBA'}else{'after off/off/on/on within fresh before/after/after/before comparison; inner scalar/batch ABBA ABBA'})}|ConvertTo-Json -Depth 35|Set-Content "$out/after-result.json" -Encoding utf8
