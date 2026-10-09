param([switch]$SkipBuild, [ValidateRange(1,10)][int]$Repetitions=2, [switch]$EnableProfiler, [ValidateSet('free','contact')][string]$Workload='free', [ValidateSet(16,256,1024)][int[]]$ActiveCounts=@(16,256,1024))
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if(Get-Process cl,link,Player,CreatorEditor,CreatorBuildTool,AssetCooker -ErrorAction SilentlyContinue){throw 'Finish build/product gates before measurement'}
if(!$SkipBuild){& "$PSScriptRoot/build-physics-t1-benchmark.ps1" -Side serial}
$exe=Join-Path $repo 'Build/Obj/Phase19T1Bench/serial/physics-t1-bench.exe'
foreach($source in @('Engine/Physics/PhysicsScene.cpp','Engine/SceneRuntime/ScenePhysicsSimulation.cpp','Engine/SceneRuntime/ScenePhysicsSimulation.h','Tools/regression/physics_t1_benchmark.cpp')) {
    if((Get-Item (Join-Path $repo $source)).LastWriteTimeUtc -gt (Get-Item $exe).LastWriteTimeUtc){throw 'Fresh benchmark build required'}
}
$root=Join-Path $repo ('Build/Obj/Phase19T1Workers/run-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $root|Out-Null
$records=@()
$mode=if($EnableProfiler){'on'}else{'off'}
foreach($backend in @('cpu','gpu')) {
    foreach($active in $ActiveCounts) {
        $ordinal=0
        foreach($round in 1..$Repetitions) {
            foreach($workers in @(0,1,2,4,4,2,1,0)) {
                $ordinal++
                $name="$backend-$active-$ordinal-workers$workers-$mode-$Workload"
                $capture=[IO.Path]::GetFullPath("$root/$name.ceprof")
                $process=Start-Process $exe -ArgumentList @($backend,"$active",$mode,('"'+$capture+'"'),"$workers",$Workload) -WindowStyle Hidden -PassThru -RedirectStandardOutput "$root/$name.json" -RedirectStandardError "$root/$name.err"
                $handle=$process.Handle
                if(!$process.WaitForExit(60000)){$process.Kill();$process.WaitForExit();throw "Timeout $name"}
                $process.WaitForExit()
                if($process.ExitCode){throw "Exit $($process.ExitCode): $name"}
                $data=Get-Content "$root/$name.json" -Raw|ConvertFrom-Json
                $expected=if($workers){$workers}else{[Math]::Min(256,[Math]::Max(1,$data.hardwareThreads-4))}
                if($data.workload -ne $Workload -or $data.backend -ne $backend -or $data.active -ne $active -or $data.samples -ne 240 -or $data.workersRequested -ne $workers -or $data.workersActual -ne $expected -or $data.profile -ne [bool]$EnableProfiler){throw "Unexpected case $name"}
                if($EnableProfiler -and ($data.profileCosts.unmatchedTasks -ne 0 -or $data.profileCosts.hierarchyViolations -ne 0)){throw "Incomplete task hierarchy $name"}
                if($EnableProfiler) {
                    foreach($marker in @('Physics.FetchWait','Physics.FetchResults','Physics.DispatcherDrain','Physics.SnapshotStatistics','Physics.RenderPrepare','Physics.RenderMerge')) {
                        if($data.profileCosts.inclusivePerTick.$marker.ticks -ne 240){throw "Incomplete measurement window $marker in $name"}
                    }
                }
                $records+=@{round=$round;order=$ordinal;data=$data;capture=$(if($EnableProfiler){$capture}else{$null})}
                "T1_WORKERS_OK $name actual=$($data.workersActual) meanUs=$($data.meanUs) p99Us=$($data.p99Us)"
            }
        }
    }
}
$records|ConvertTo-Json -Depth 15|Set-Content "$root/result.json" -Encoding utf8
"T1_WORKERS_SWEEP_OK evidence=$root"
