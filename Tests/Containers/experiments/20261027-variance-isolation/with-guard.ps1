param([Parameter(Mandatory)][string]$Tag,[Parameter(Mandatory)][scriptblock]$Work,[uint32]$TtlMilliseconds=3600000,[switch]$High)
$ErrorActionPreference='Stop'
$guardRepoRoot=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$guardExe=Join-Path $guardRepoRoot 'experiments/20261026-small-array/build/Release/smt_guard.exe'
$guardPrefix=Join-Path $guardRepoRoot "results/$Tag-guard"
if(Test-Path "$guardPrefix.json"){throw 'Use a fresh guard tag'}
$guardToken=[guid]::NewGuid().ToString('N')
$guardReadyName="Local\CreatorContainersReady$guardToken"
$guardStopName="Local\CreatorContainersStop$guardToken"
$guardReady=[Threading.EventWaitHandle]::new($false,[Threading.EventResetMode]::ManualReset,$guardReadyName)
$guardStop=[Threading.EventWaitHandle]::new($false,[Threading.EventResetMode]::ManualReset,$guardStopName)
$guardProcess=$null;$guardSuccess=$false;$guardStarted=Get-Date -Format o;$guardWorkEnded=$null;$guardAliveAtEnd=$false
try{
 $guardProcess=Start-Process -FilePath $guardExe -ArgumentList @('5',$guardReadyName,$guardStopName,"$TtlMilliseconds") -WindowStyle Hidden -PassThru -RedirectStandardOutput "$guardPrefix.txt" -RedirectStandardError "$guardPrefix.err.txt"
 if(!$guardReady.WaitOne(5000)){throw 'Guard did not become ready'}
 if($High){$guardProcess.PriorityClass='High';$guardProcess.Refresh();if($guardProcess.PriorityClass -ne 'High'){throw 'Own guard priority failed'}}
 & $Work
 $guardSuccess=$true
}finally{
 $guardWorkEnded=Get-Date -Format o
 if($guardProcess){$guardProcess.Refresh();$guardAliveAtEnd=-not $guardProcess.HasExited}
 if(!$guardAliveAtEnd){$guardSuccess=$false}
 [void]$guardStop.Set()
 if($guardProcess -and !$guardProcess.WaitForExit(5000)){
  $guardActual=Get-CimInstance Win32_Process -Filter "ProcessId=$($guardProcess.Id)"
  if($guardActual.ExecutablePath -ne $guardExe -or $guardActual.CommandLine -notlike "*$guardStopName*"){throw 'Unexpected guard process identity'}
  Stop-Process -Id $guardProcess.Id
 }
 [ordered]@{started=$guardStarted;workEnded=$guardWorkEnded;ended=(Get-Date -Format o);success=$guardSuccess;aliveAtWorkEnd=$guardAliveAtEnd;cpu=5;peerCpu=4;priority='above_normal';processPriorityClass=$(if($High){'High'}else{'Normal'});pauseCount=4096;ttlMilliseconds=$TtlMilliseconds;processId=$(if($guardProcess){$guardProcess.Id}else{$null});helper=(Get-FileHash $guardExe -Algorithm SHA256|Select-Object Path,Hash);script=(Get-FileHash $PSCommandPath -Algorithm SHA256|Select-Object Path,Hash);policy='Own temporary helper only, CPU5 pause loop. Other process affinity/priority and OS settings unchanged. Named stop event and bounded TTL. Main test binary unchanged.'}|ConvertTo-Json -Depth 5|Set-Content "$guardPrefix.json" -Encoding utf8
 $guardStop.Dispose();$guardReady.Dispose()
 if(!$guardAliveAtEnd){throw 'Guard exited before work completed; measurements are not qualified'}
}
