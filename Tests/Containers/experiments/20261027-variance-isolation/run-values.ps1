$ErrorActionPreference='Stop'
$root=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$engine=Join-Path (Split-Path $root) 'CreatorEngine'
$launcher=Join-Path $PSScriptRoot 'build/Release/controlled_process.exe'
$binary=Join-Path $engine 'Build/Validation/UtilityModules/ValueTypesBenchmark/x64-Release/ValueTypesBenchmark.exe'
$original=Get-Content "$engine/Build/Verification/ContainerMigration/ValueTypes20261027/value-measurement-inputs.json" -Raw|ConvertFrom-Json
foreach($file in $original){if((Get-FileHash $file.Path).Hash -ne $file.Hash){throw "Value input changed: $($file.Path)"}}
$prefix=Join-Path $root 'results/20261027-variance-values'
if(Test-Path "$prefix-1.csv"){throw 'Use a fresh value campaign'}
& "$PSScriptRoot/with-guard.ps1" -Tag '20261027-variance-values-guard1' -High -Work {
    foreach($valueRepeat in 1..3){
        & $launcher high $binary "$prefix-$valueRepeat.csv" ($valueRepeat % 2)
        if($LASTEXITCODE){throw 'Value module measurement failed'}
    }
}
& "$engine/Tools/regression/summarize-value-types-performance.ps1" -Prefix $prefix
foreach($file in $original){if((Get-FileHash $file.Path).Hash -ne $file.Hash){throw "Value input changed after measurement: $($file.Path)"}}
