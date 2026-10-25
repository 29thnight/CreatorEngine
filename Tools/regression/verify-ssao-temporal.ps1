[CmdletBinding()]
param([ValidateSet('Debug','Release')][string]$Configuration='Debug',[Parameter(Mandatory)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out=[IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $out -Force|Out-Null
$scenario=Join-Path $out 'scenario.txt'
@('dx12.ssao','quit')|Set-Content -LiteralPath $scenario -Encoding utf8
$results=Join-Path $out 'results.jsonl'
$exe=Join-Path $repo "Bin/x64-$Configuration/Editor/CreatorEditor.exe"
$start=[Diagnostics.ProcessStartInfo]::new($exe)
$start.WorkingDirectory=Split-Path $exe
$start.UseShellExecute=$false
$start.CreateNoWindow=$true
$start.RedirectStandardOutput=$true
$start.RedirectStandardError=$true
foreach($a in @('--commandlet-script',$scenario,'--result-file',$results)){$start.ArgumentList.Add($a)}
$start.Environment['CREATOR_DX12_VALIDATION']='gpu'
$p=[Diagnostics.Process]::Start($start)
$stdout=$p.StandardOutput.ReadToEndAsync();$stderr=$p.StandardError.ReadToEndAsync()
if(!$p.WaitForExit(600000)){$p.Kill();throw 'SSAO native GPU gate timed out.'}
$stdout.Result|Set-Content "$out/editor.out" -Encoding utf8
$stderr.Result|Set-Content "$out/editor.err" -Encoding utf8
$rows=@(Get-Content $results|ConvertFrom-Json)
$test=$rows|Where-Object command -eq 'dx12.ssao'|Select-Object -Last 1
$success=$p.ExitCode -eq 0 -and $test.status -eq 'succeeded' -and $test.data.passed
@{passed=$success;exitCode=$p.ExitCode;validation='gpu';test=$test;runtimeSha256=(Get-FileHash (Join-Path (Split-Path $exe) 'CreatorEditor.runtime.dll')).Hash}|ConvertTo-Json -Depth 12|Set-Content "$out/result.json" -Encoding utf8
if(!$success){throw "SSAO native regression failed: $out"}
"SSAO_TEMPORAL_GPU_OK $out"
