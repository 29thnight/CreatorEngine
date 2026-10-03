param([switch]$SkipBuild, [ValidateRange(1,10)][int]$Repetitions=2)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if(Get-Process cl,link,Player,CreatorEditor,CreatorBuildTool,AssetCooker -ErrorAction SilentlyContinue){throw 'Finish build/product gates before measurement'}
if(!$SkipBuild){& "$PSScriptRoot/build-physics-t1-benchmark.ps1" -Side serial}
$exe=Join-Path $repo 'Build/Obj/Phase19T1Bench/serial/physics-t1-bench.exe'
foreach($source in @('Engine/Physics/PhysicsScene.cpp','Engine/SceneRuntime/ScenePhysicsSimulation.cpp','Engine/SceneRuntime/ScenePhysicsSimulation.h','Tools/regression/physics_t1_benchmark.cpp')) {
    if((Get-Item (Join-Path $repo $source)).LastWriteTimeUtc -gt (Get-Item $exe).LastWriteTimeUtc){throw 'Fresh benchmark build required'}
}
$root=Join-Path $repo ('Build/Obj/Phase19T1Costs/run-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $root|Out-Null
$records=@()
foreach($backend in @('cpu','gpu')) {
    foreach($active in @(16,256,1024)) {
        $ordinal=0
        foreach($round in 1..$Repetitions) {
            foreach($profile in @('off','on','on','off')) {
                $ordinal++
                $name="$backend-$active-$ordinal-$profile"
                $capture=[IO.Path]::GetFullPath("$root/$name.ceprof")
                $process=Start-Process $exe -ArgumentList @($backend,"$active",$profile,('"'+$capture+'"')) -WindowStyle Hidden -PassThru -RedirectStandardOutput "$root/$name.json" -RedirectStandardError "$root/$name.err"
                $handle=$process.Handle
                if(!$process.WaitForExit(60000)){$process.Kill();$process.WaitForExit();throw "Timeout $name"}
                $process.WaitForExit()
                if($process.ExitCode){throw "Exit $($process.ExitCode): $name"}
                $data=Get-Content "$root/$name.json" -Raw|ConvertFrom-Json
                if($data.backend -ne $backend -or $data.active -ne $active -or $data.samples -ne 240){throw "Unexpected case $name"}
                if($profile -eq 'on') {
                    foreach($marker in @('Physics.SimulateSubmit','Physics.EventCollect','Physics.CharacterPoseCollect','Physics.Counters','Physics.SnapshotPublish','Physics.FetchWait','Physics.FetchResults','Physics.DispatcherDrain','Physics.SnapshotStatistics','Physics.ActivePoseCollect','Physics.RenderMerge','Physics.RenderPrepare')) {
                        if($data.profileCosts.inclusivePerTick.$marker.ticks -ne 240){throw "Missing cost spans $marker in $name"}
                    }
                    if($data.profileCosts.workerTasks -le 0 -or $data.profileCosts.unmatchedTasks -ne 0 -or $data.profileCosts.hierarchyViolations -ne 0){throw "Task correlation incomplete $name"}
                }
                $records+=@{round=$round;order=$ordinal;data=$data;capture=$(if($profile -eq 'on'){$capture}else{$null})}
                "T1_COST_OK $name meanUs=$($data.meanUs) p99Us=$($data.p99Us)"
            }
        }
    }
}
$records|ConvertTo-Json -Depth 15|Set-Content "$root/result.json" -Encoding utf8
"T1_COST_SWEEP_OK evidence=$root"
