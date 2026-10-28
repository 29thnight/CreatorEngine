param([string]$Tag='20261027-variance-window-full1',[string]$GuardTag='20261027-variance-window-guard1')
$ErrorActionPreference='Stop'
$root=(Resolve-Path "$PSScriptRoot/../..").Path
$log="$root/results/$Tag-driver.log"
if(Test-Path $log){throw 'Use a fresh campaign'}
$eventName='Local\CreatorMeasurementResume'+[guid]::NewGuid().ToString('N')
$resume=[Threading.EventWaitHandle]::new($false,[Threading.EventResetMode]::AutoReset,$eventName)
$windowCoordinator=@{resume=$resume;name=$eventName;watch=[Diagnostics.Stopwatch]::StartNew();completed=0;log=$log;events=[Collections.Generic.List[object]]::new()}
$checkpoint={
    param([string]$Stage,[switch]$Force)
    if(!$Force){$windowCoordinator.completed++}
    if(!$Force -and $windowCoordinator.watch.ElapsedMilliseconds -lt 25000){return}
    $windowCoordinator.watch.Stop()
    $pauseRecord=[ordered]@{stage=$Stage;completed=$windowCoordinator.completed;paused=(Get-Date -Format o);activeMs=$windowCoordinator.watch.ElapsedMilliseconds;resumed=$null;measuringAllowedAt=$null}
    $windowCoordinator.events.Add($pauseRecord)
    [Console]::Out.WriteLine("MEASUREMENT_PAUSED,$($windowCoordinator.name),$($windowCoordinator.completed),$Stage")
    [Console]::Out.Flush()
    if(!$windowCoordinator.resume.WaitOne(600000)){throw 'Measurement coordinator resume timed out'}
    $pauseRecord.resumed=Get-Date -Format o
    Start-Sleep -Seconds 3
    $pauseRecord.measuringAllowedAt=Get-Date -Format o
    $windowCoordinator.watch.Restart()
}.GetNewClosure()
Set-Item Function:Invoke-MeasurementCheckpoint -Value $checkpoint
$suiteTag=$Tag
try {
    Invoke-MeasurementCheckpoint -Stage initial -Force
    & "$PSScriptRoot/with-guard.ps1" -Tag $GuardTag -High -Work {
        & "$PSScriptRoot/run-window-acceptance.ps1" -Tag $suiteTag | Add-Content $log
    }
    foreach($queueRepeat in 1..5){
        & "$root/build-modules/modules/Release/module_queue_acceptance.exe"|Set-Content "$root/results/$Tag-ppl-$queueRepeat.csv" -Encoding utf8
        if($LASTEXITCODE){throw 'FAA acceptance failed'}
        Invoke-MeasurementCheckpoint -Stage "FAA/$queueRepeat"
    }
    Get-FileHash "$root/build-modules/modules/Release/module_concurrent_tests.exe"|ConvertTo-Json|Set-Content "$root/results/$Tag-concurrent-before.json" -Encoding utf8
    foreach($concurrentRepeat in 1..3){
        & "$root/build-modules/modules/Release/module_concurrent_tests.exe"|Set-Content "$root/results/$Tag-concurrent-$concurrentRepeat.csv" -Encoding utf8
        if($LASTEXITCODE){throw 'Concurrent acceptance failed'}
        Invoke-MeasurementCheckpoint -Stage "concurrent/$concurrentRepeat"
    }
    $references=Get-Content "$root/results/20261026-module-accept1-reference-files.json" -Raw|ConvertFrom-Json
    @($references|ForEach-Object {Get-FileHash $_.Path})|ConvertTo-Json|Set-Content "$root/results/$Tag-reference-files.json" -Encoding utf8
    & "$PSScriptRoot/run-window-primary.ps1" -Tag $Tag | Add-Content $log
    & "$root/verify-module-acceptance.ps1" -Tag $Tag -GuardTag $GuardTag | Add-Content $log
    $verdict=Get-Content "$root/results/$Tag-verdict.json" -Raw|ConvertFrom-Json
    if(!$verdict.pass){throw 'Full performance acceptance failed'}
    & "$PSScriptRoot/run-window-values.ps1" | Add-Content $log
    & "$PSScriptRoot/verify-preserved-state.ps1" | Add-Content $log
    [Console]::Out.WriteLine("MEASUREMENT_FINISHED,$Tag,PASS")
    [Console]::Out.Flush()
} catch {
    if(!(Test-Path "$root/results/$Tag-verdict.json")){
        [ordered]@{tag=$Tag;pass=$false;verified=$false;aborted=$true;reason=$_.Exception.Message;policy='All completed process files retained; incomplete campaign is never accepted'}|ConvertTo-Json|Set-Content "$root/results/$Tag-verdict.json" -Encoding utf8
    }
    throw
} finally {
    $windowCoordinator.events|ConvertTo-Json -Depth 4|Set-Content "$root/results/$Tag-observer-windows.json" -Encoding utf8
    $resume.Dispose()
}
