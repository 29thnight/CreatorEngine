param([Parameter(Mandatory)][string]$Tag,[Parameter(Mandatory)][ValidateSet('integer','memory')][string]$Mode,
      [Parameter(Mandatory)][int]$Cpu,[Parameter(Mandatory)][scriptblock]$Work)
$ErrorActionPreference='Stop'
$root=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$prefix=Join-Path $root "results/$Tag"
if(Test-Path "$prefix-load.json"){throw 'Use a new load tag'}
$exe=Join-Path $PSScriptRoot 'build/Release/variance_load.exe'
$token=[guid]::NewGuid().ToString('N')
$readyName="Local\CreatorVarianceReady$token"
$stopName="Local\CreatorVarianceStop$token"
$ready=[Threading.EventWaitHandle]::new($false,[Threading.EventResetMode]::ManualReset,$readyName)
$stop=[Threading.EventWaitHandle]::new($false,[Threading.EventResetMode]::ManualReset,$stopName)
$process=$null
$success=$false
try {
    $process=Start-Process $exe -ArgumentList @($Mode,"$Cpu",$readyName,$stopName,'300000') -WindowStyle Hidden -PassThru -RedirectStandardOutput "$prefix-load.log" -RedirectStandardError "$prefix-load.err"
    if(!$ready.WaitOne(5000)){throw 'Own diagnostic load did not become ready'}
    & $Work
    $success=$true
} finally {
    [void]$stop.Set()
    if($process -and !$process.WaitForExit(5000)){
        $identity=Get-CimInstance Win32_Process -Filter "ProcessId=$($process.Id)"
        if($identity.ExecutablePath -ne $exe -or $identity.CommandLine -notlike "*$stopName*"){throw 'Unexpected load identity'}
        Stop-Process -Id $process.Id
        $success=$false
    }
    [ordered]@{success=$success;mode=$Mode;cpu=$Cpu;ttlMs=300000;processId=$(if($process){$process.Id}else{$null});helper=(Get-FileHash $exe);script=(Get-FileHash $PSCommandPath)}|ConvertTo-Json -Depth 5|Set-Content "$prefix-load.json" -Encoding utf8
    $ready.Dispose();$stop.Dispose()
}
