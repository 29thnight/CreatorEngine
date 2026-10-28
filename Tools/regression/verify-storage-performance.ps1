param([Parameter(Mandatory)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$output=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path $output){throw 'Use a fresh verification directory'}
New-Item -ItemType Directory $output|Out-Null
$guard=Join-Path $repo 'Tests/Containers/experiments/20261027-variance-isolation/with-guard.ps1'
if(!(Test-Path $guard)){throw 'The local CPU5 measurement guard is required; see storage acceptance documentation'}
$vs=& "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe" -latest -products '*' -requires Microsoft.Component.MSBuild -property installationPath
& "$vs/MSBuild/Current/Bin/amd64/MSBuild.exe" "$PSScriptRoot/StoragePerformanceProbe.vcxproj" /t:Build /p:Configuration=Release /p:Platform=x64 /p:UseDynamicDebugging=false /p:LinkIncremental=false "/p:SolutionDir=$repo\" /m:2 /v:minimal /nologo *> "$output/build.log"
if($LASTEXITCODE){throw 'Storage performance module build failed'}
$exe=Join-Path $output 'StoragePerformanceProbe.exe'
Copy-Item "$repo/Build/Validation/UtilityModules/StoragePerformanceProbe/x64-Release/StoragePerformanceProbe.exe" $exe
$paths=@('Engine/Containers/storage/storage.h','Engine/Containers/segmented_deque/segmented_deque.h','Engine/Containers/storage_queue/storage_queue.h','Engine/Containers/storage_stack/storage_stack.h','Engine/Containers/storage_priority_queue/storage_priority_queue.h','Engine/Containers/extractable_hash_map/extractable_hash_map.h','Engine/Utility_Framework/Foundation/EngineStorage.h','Tools/regression/storage_performance_probe.cpp','Tools/regression/StoragePerformanceProbe.vcxproj','Tools/regression/verify-storage-performance.ps1')
$inputs=@($paths|ForEach-Object {Get-FileHash "$repo/$_"})+@(Get-FileHash $exe)
$inputs|ConvertTo-Json|Set-Content "$output/inputs.json"
$results=@();$guards=@()
$token=[guid]::NewGuid().ToString('N')
for($run=1;$run -le 3;++$run)
{
    $csv=Join-Path $output "run-$run.csv"
    $argument=if($run -eq 2){'--reverse'}else{'--forward'}
    $tag="20261028-storage-final-$token-$run"
    & $guard -Tag $tag -High -Work {& $exe $argument > $csv;if($LASTEXITCODE){throw 'Performance execution failed'}}
    $guardPath=Join-Path $repo "Tests/Containers/results/$tag-guard.json"
    $state=Get-Content $guardPath -Raw|ConvertFrom-Json
    if(!$state.success -or !$state.aliveAtWorkEnd){throw 'Measurement guard did not cover execution'}
    $guards+=Get-FileHash $guardPath
    $summary=@(Import-Csv $csv|Group-Object case,count|ForEach-Object {
        $c=@($_.Group.ce_ns|ForEach-Object {[double]$_}|Sort-Object)
        $s=@($_.Group.stl_ns|ForEach-Object {[double]$_}|Sort-Object)
        if($c.Count -ne 15){throw 'Expected 15 paired samples'}
        [pscustomobject]@{run=$run;case=$_.Name;ratio=$c[7]/$s[7];ceSpread=$c[13]/$c[1];stlSpread=$s[13]/$s[1];pass=($c[7]/$s[7] -le 1.10 -and $c[13]/$c[1] -le 1.30 -and $s[13]/$s[1] -le 1.30)}
    })
    $summary|ConvertTo-Json|Set-Content "$output/summary-$run.json"
    if($summary.Count -ne 15 -or @($summary|Where-Object {!$_.pass}).Count){throw "Storage performance acceptance failed run=$run"}
    $results+=$summary
    "STORAGE_FINAL_PERFORMANCE_PASS run=$run cases=15"
}
foreach($input in $inputs){if((Get-FileHash $input.Path).Hash -ne $input.Hash){throw 'Frozen input changed'}}
@{pass=$true;conditions=15;runs=3;pairedSamples=675;ratioLimit=1.10;spreadLimit=1.30;inputs=$inputs;guards=$guards;raw=@(Get-ChildItem "$output/run-*.csv"|Get-FileHash);results=$results}|ConvertTo-Json -Depth 8|Set-Content "$output/result.json"
'STORAGE_FINAL_PERFORMANCE_ACCEPTED 15 x 3'
