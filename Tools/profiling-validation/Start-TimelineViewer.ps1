param([int]$Port=8765)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$url="http://127.0.0.1:$Port/Tools/profiling-validation/viewer/index.html"
$ready=$false
try { $ready=(Invoke-WebRequest $url -TimeoutSec 2).Content.Contains('CreatorEngine 공용 CPU GPU ETW 타임라인') } catch {}
if(!$ready){
 $out=Join-Path $repo 'Artifacts/profiling-server'
 New-Item -ItemType Directory -Force $out | Out-Null
 $python=(Get-Command python).Source
 $process=Start-Process -FilePath $python -ArgumentList @('-m','http.server',"$Port",'--bind','127.0.0.1','--directory',$repo) -WindowStyle Hidden -PassThru -RedirectStandardOutput "$out/server.out" -RedirectStandardError "$out/server.err"
 @{pid=$process.Id;port=$Port;root=$repo;url=$url}|ConvertTo-Json|Set-Content "$out/server.json"
 for($try=0;$try -lt 30;$try++){
  try {if((Invoke-WebRequest $url -TimeoutSec 1).StatusCode -eq 200){$ready=$true;break}}catch{}
  Start-Sleep -Milliseconds 100
 }
 if(!$ready){throw 'Local profiling viewer did not start'}
}
$url
