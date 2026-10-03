param()
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out=Join-Path $repo 'Build/Obj/Phase19T2QueryBench/Release'
$exe=Join-Path $out 'physics-t2-bench.exe'
$rows=@()
foreach($backend in @('cpu','gpu')) {
    foreach($count in @(1,16,64)) {
        foreach($profile in @('off','on')) {
            # ABBA repeated twice; separate processes isolate profiler state.
            $ordinal=0
            foreach($mode in @('scalar','batch','batch','scalar','scalar','batch','batch','scalar')) {
                $id="$backend-$count-$profile-$ordinal-$mode"
                $capture=Join-Path $out "$id.ceprof"
                $process=Start-Process $exe -ArgumentList @($backend,$mode,$profile,$count,('"'+$capture+'"')) -WindowStyle Hidden -PassThru -RedirectStandardOutput "$out/$id.json" -RedirectStandardError "$out/$id.stderr"
                $handle=$process.Handle
                if(!$process.WaitForExit(60000)) {$process.Kill();$process.WaitForExit();throw "$id timeout"}
                $process.WaitForExit()
                if($process.ExitCode) {throw "$id failed exit=$($process.ExitCode): $(Get-Content "$out/$id.stderr" -Raw)"}
                $row=Get-Content "$out/$id.json" -Raw|ConvertFrom-Json
                if(!$row.parity -or $row.hierarchyViolations -or ($profile -eq 'on' -and !(Test-Path $capture))) {throw "$id invalid result"}
                $row|Add-Member -NotePropertyName ordinal -NotePropertyValue $ordinal
                $rows+=$row
                $ordinal++
            }
            Write-Output "T2_QUERY_BENCH_OK $backend count=$count profile=$profile"
        }
    }
}
$hashes=@('Engine/Physics/PhysicsScene.cpp','Tools/regression/physics_t2_benchmark.cpp','Tools/regression/build-physics-t2-benchmark.ps1','Tools/regression/verify-physics-t2-benchmark.ps1','Tools/regression/summarize-physics-t2-benchmark.py')|ForEach-Object { @{path=$_;sha256=(Get-FileHash (Join-Path $repo $_) -Algorithm SHA256).Hash} }
@{status='native_query_benchmark_validated';runs=$rows;sourceHashes=$hashes;productPerformanceAccepted=$false;configuration='Release O2';warmup=60;timedSamples=600;ordering='ABBA ABBA';scope='Synchronous native mixed query wall time; excludes CLR and simulation step'}|ConvertTo-Json -Depth 12|Set-Content "$out/result.json" -Encoding utf8

& python "$PSScriptRoot/summarize-physics-t2-benchmark.py"
if($LASTEXITCODE) {throw "T2 benchmark summary failed"}
