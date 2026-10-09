[CmdletBinding()]
param([ValidateSet('cpu','gpu')][string]$Backend='cpu')

$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$exe=Join-Path $repo 'Build/Obj/Phase19T1Bench/serial/physics-t1-bench.exe'
$sources=@('Engine/Physics/PhysicsScene.cpp','Engine/SceneRuntime/ScenePhysicsSimulation.cpp','Engine/SceneRuntime/ScenePhysicsSimulation.h','Tools/regression/physics_t1_benchmark.cpp')
foreach($source in $sources) {
    if((Get-Item (Join-Path $repo $source)).LastWriteTimeUtc -gt (Get-Item $exe).LastWriteTimeUtc){throw 'Fresh native solver benchmark build required'}
}
$out=Join-Path $repo ('Build/Verification/ContactStream/M3Acceptance/Solver-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $out|Out-Null
$receipt=[ordered]@{status='running';backend=$Backend;configuration='Release';scope='Native current ScenePhysicsSimulation; 1024 total dynamic bodies; SetVelocity writes plus Advance wall time; not whole Player frame';order=@('off','on','on','off');runs=@();performanceAccepted=$false;binarySha256=(Get-FileHash $exe).Hash;sourceHashes=@($sources|ForEach-Object {@{path=$_;sha256=(Get-FileHash (Join-Path $repo $_)).Hash}})}
function SaveReceipt {$receipt|ConvertTo-Json -Depth 25|Set-Content "$out/result.json" -Encoding utf8}
SaveReceipt
try {
    foreach($active in @(16,256,1024)) {
        foreach($writes in @(0,($active/2),$active)) {
            foreach($profile in $receipt.order) {
                $ordinal=$receipt.runs.Count
                $stem="$Backend-$active-$writes-$profile-$ordinal"
                $capture="$out/$stem.ceprof"
                $process=Start-Process $exe -ArgumentList @($Backend,"$active",$profile,('"'+$capture+'"'),'2','free',"$writes") -WindowStyle Hidden -PassThru -RedirectStandardOutput "$out/$stem.json" -RedirectStandardError "$out/$stem.err"
                $null=$process.Handle
                if(!$process.WaitForExit(30000)){$process.Kill();$process.WaitForExit();throw "Solver timeout $stem"}
                $process.WaitForExit()
                if($process.ExitCode -ne 0){throw "Solver exit $($process.ExitCode): $stem"}
                $row=Get-Content "$out/$stem.json" -Raw|ConvertFrom-Json
                if($row.active -ne $active -or $row.bodies -ne 1024 -or $row.writesPerTick -ne $writes -or $row.samples -ne 240 -or $row.rawUs.Count -ne 240 -or $row.workersActual -ne 2 -or $row.profile -ne ($profile -eq 'on') -or $row.minChanged -ne $writes -or $row.maxChanged -ne $writes){throw "Solver workload mismatch $stem"}
                if($profile -eq 'on') {
                    if($row.profileCosts.unmatchedTasks -ne 0 -or $row.profileCosts.unmatchedCompletions -ne 0 -or $row.profileCosts.hierarchyViolations -ne 0){throw "Solver hierarchy mismatch $stem"}
                    foreach($marker in @('Physics.FetchWait','Physics.FetchResults','Physics.DispatcherDrain','Physics.SnapshotStatistics','Physics.RenderPrepare','Physics.RenderMerge')) {
                        if($row.profileCosts.inclusivePerTick.$marker.ticks -ne 240){throw "Solver measurement window mismatch $marker"}
                    }
                }
                $receipt.runs+=@{data=$row;capture=$(if($profile -eq 'on'){$capture}else{$null})}
                SaveReceipt
                "M3_SOLVER_RUN_OK $stem meanUs=$($row.meanUs) p99Us=$($row.p99Us)"
            }
        }
    }
    $receipt.status='measurements_complete'
    SaveReceipt
    "M3_SOLVER_MATRIX_OK receipt=$out/result.json"
}
catch {$receipt.status='failed';$receipt.error=$_.ToString();SaveReceipt;throw}