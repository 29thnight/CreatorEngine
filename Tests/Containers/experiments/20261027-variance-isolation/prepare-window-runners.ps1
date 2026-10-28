$ErrorActionPreference='Stop'
$dir=$PSScriptRoot
$root=(Resolve-Path "$dir/../..").Path
$s=[IO.File]::ReadAllText("$dir/run-acceptance.ps1")
$s=$s.Replace('  Write-Output "Isolated repeat $run [$index/82] $($summaries[0])"','  Write-Output "Isolated repeat $run [$index/82] $($summaries[0])"'+"`n"+'  Invoke-MeasurementCheckpoint -Stage "general/$run/$index"')
$s=$s.Replace(' if($LASTEXITCODE -ne 0){throw ''MPSC measurement failed''}', ' if($LASTEXITCODE -ne 0){throw ''MPSC measurement failed''}'+"`n"+' Invoke-MeasurementCheckpoint -Stage "mpsc-single/$run"')
$s=$s.Replace('No rebuild during collection;', 'Fixed agent activity windows between completed processes; no UI/log analysis during timed processes. No rebuild during collection;')
[IO.File]::WriteAllText("$dir/run-window-acceptance.ps1",$s)
$s=[IO.File]::ReadAllText("$root/run-module-mpsc-primary.ps1").Replace('$root=$PSScriptRoot;','$root=(Resolve-Path (Join-Path $PSScriptRoot ''../..'')).Path;')
# Each primary case is a separate process; checkpoint only after it has exited.
$s=$s.Replace('  & $exe | Set-Content', '  Invoke-MeasurementCheckpoint -Stage "primary-start/$run" -Force'+"`n"+'  & $exe | Set-Content')
$s=$s.Replace('  Get-Content "$out/$Tag-primary-$run.csv"|Where-Object {$_ -like ''mpsc_summary,*''}', '  Get-Content "$out/$Tag-primary-$run.csv"|Where-Object {$_ -like ''mpsc_summary,*''}'+"`n"+'  Invoke-MeasurementCheckpoint -Stage "primary-end/$run"')
[IO.File]::WriteAllText("$dir/run-window-primary.ps1",$s)
$s=[IO.File]::ReadAllText("$dir/run-values.ps1").Replace('20261027-variance-values','20261027-variance-window-values')
$s=$s.Replace('    foreach($valueRepeat in 1..3){','    foreach($valueRepeat in 1..3){'+"`n"+'        Invoke-MeasurementCheckpoint -Stage "values-start/$valueRepeat" -Force')
$s=$s.Replace('        if($LASTEXITCODE){throw ''Value module measurement failed''}', '        if($LASTEXITCODE){throw ''Value module measurement failed''}'+"`n"+'        Invoke-MeasurementCheckpoint -Stage "values-end/$valueRepeat"')
[IO.File]::WriteAllText("$dir/run-window-values.ps1",$s)
