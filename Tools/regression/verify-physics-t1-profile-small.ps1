param([switch]$SkipBuild, [ValidateRange(1,10)][int]$Repetitions=2, [ValidateSet('profile')][string]$CandidateSide='profile', [ValidateSet('cpu','gpu')][string[]]$Backends=@('cpu','gpu'), [ValidateSet(16)][int[]]$ActiveCounts=@(16), [ValidateSet(0,1,2,4)][int[]]$WorkerBudgets=@(2,0))
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if(Get-Process cl,link,Player,CreatorEditor,CreatorBuildTool,AssetCooker -ErrorAction SilentlyContinue){throw 'Finish other build/product gates before the benchmark'}
if(!$SkipBuild){
    & "$PSScriptRoot/build-physics-t1-benchmark.ps1" -Side $CandidateSide
    & "$PSScriptRoot/build-physics-t1-benchmark.ps1" -Side serial -LegacyPublication:($CandidateSide -eq 'profile')
}
foreach($side in @($CandidateSide,'serial')){
    $binary=Get-Item (Join-Path $repo "Build/Obj/Phase19T1Bench/$side/physics-t1-bench.exe")
    foreach($source in @('Engine/SceneRuntime/ScenePhysicsSimulation.cpp','Engine/SceneRuntime/ScenePhysicsSimulation.h','Engine/Physics/PhysicsScene.cpp','Tools/regression/physics_t1_benchmark.cpp')){
        if((Get-Item (Join-Path $repo $source)).LastWriteTimeUtc -gt $binary.LastWriteTimeUtc){throw 'Fresh benchmark build required'}
    }
}
$root=Join-Path $repo ('Build/Obj/Phase19T1ProfileSmall/run-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $root|Out-Null
$records=@()
foreach($backend in $Backends) {
    foreach($case in @($ActiveCounts | ForEach-Object {@{workload='small';active=$_}})) {
        $active=$case.active
        $workload=$case.workload
        foreach($Workers in $WorkerBudgets) {
        foreach($profile in @('off','on')) {
            $ordinal=0
            foreach($round in 1..$Repetitions) {
                foreach($side in @('serial',$CandidateSide,$CandidateSide,'serial')) {
                    $ordinal++
                    $name="$backend-$workload-$active-w$Workers-$profile-$ordinal-$side"
                    $capture=[IO.Path]::GetFullPath("$root/$name.ceprof")
                    $exe=Join-Path $repo "Build/Obj/Phase19T1Bench/$side/physics-t1-bench.exe"
                    $process=Start-Process $exe -ArgumentList @($backend,"$active",$profile,('"'+$capture+'"'),"$Workers",$workload) -WindowStyle Hidden -PassThru -RedirectStandardOutput "$root/$name.json" -RedirectStandardError "$root/$name.err"
                    $handle=$process.Handle
                    if(!$process.WaitForExit(60000)){$process.Kill();$process.WaitForExit();throw "Benchmark timeout $name"}
                    $process.WaitForExit()
                    if($process.ExitCode){throw "Benchmark exit $($process.ExitCode): $name"}
                    $data=Get-Content "$root/$name.json" -Raw|ConvertFrom-Json
                    $expected=if($Workers){$Workers}else{[Math]::Min(256,[Math]::Max(1,$data.hardwareThreads-4))}
                    if($data.backend -ne $backend -or $data.active -ne $active -or $data.samples -ne 240 -or $data.workersActual -ne $expected -or $data.workersRequested -ne $Workers -or $data.profile -ne ($profile -eq 'on')){throw "Unexpected benchmark case $name"}
                    if($profile -eq 'on') {
                        if($data.profileCosts.unmatchedTasks -ne 0 -or $data.profileCosts.hierarchyViolations -ne 0){throw "Incomplete task hierarchy $name"}
                        foreach($marker in @('Physics.FetchWait','Physics.FetchResults','Physics.DispatcherDrain','Physics.SnapshotStatistics','Physics.RenderPrepare','Physics.RenderMerge')) {
                            if($data.profileCosts.inclusivePerTick.$marker.ticks -ne 240){throw "Incomplete measurement window $marker in $name"}
                        }
                    }
                    if($data.workload -ne $workload -or $data.bodies -ne $active){throw "Wrong workload $name"}
                    if($profile -eq 'on' -and ($data.profileCosts.unmatchedCompletions -ne 0 -or $data.profileCosts.completionTasks -ne $data.profileCosts.sdkTasks)){throw "Incomplete completion lineage $name"}
                    $records+=@{side=$side;round=$round;order=$ordinal;data=$data;capture=$(if($profile -eq 'on'){$capture}else{$null})}
                    "T1_BENCH_OK $name meanUs=$($data.meanUs) p99Us=$($data.p99Us)"
                }
            }
        }
    }
}
}
$records|ConvertTo-Json -Depth 10|Set-Content "$root/result.json" -Encoding utf8
"T1_PROFILE_SMALL_ABBA_OK evidence=$root"
