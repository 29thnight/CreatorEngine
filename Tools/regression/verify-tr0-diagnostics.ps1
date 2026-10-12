param(
 [ValidateSet('Debug','Release')][string]$Config='Debug',
 [string]$Work=(Join-Path $PSScriptRoot '../../Artifacts/PR169/tr0-surfaces-20261011')
)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'ArtifactRetention.ps1')
Assert-ArtifactCapacity $Work
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out=Join-Path ([IO.Path]::GetFullPath($Work)) "native-$Config"
[IO.Directory]::CreateDirectory($out)|Out-Null
& 'C:/Program Files/Microsoft Visual Studio/18/Community/Common7/Tools/Launch-VsDevShell.ps1' -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
$flags=@('/nologo','/std:c++latest','/EHsc','/utf-8','/DNOMINMAX','/UNDEBUG')
$flags+=('/I'+(Join-Path $repo 'Engine/Utility_Framework'))
$flags+=if($Config -eq 'Debug'){@('/Od','/MDd','/Zi')}else{@('/O2','/MD')}
$cases=@(
 @{name='display-diagnostics';sources=@('Tools/regression/ProfilerRenderingDiagnosticsTests.cpp','Engine/EngineDiagnostics/ProfilerRenderingDiagnostics.cpp');libs=@()},
 @{name='temporal-provenance';sources=@('Tools/regression/TemporalMeasurementProvenanceTests.cpp','Engine/RenderEngine/Render/Temporal/TemporalRuntimeControl.cpp');libs=@()},
 @{name='profiler-measurement';sources=@('Tools/regression/ProfilerRenderMeasurementTests.cpp');libs=@((Join-Path $repo "Build/Lib/x64-$Config/EngineDiagnostics.lib"),'Psapi.lib')}
)
Push-Location $out
try {
 foreach($case in $cases){
  $sources=@($case.sources|ForEach-Object {Join-Path $repo $_})
  $exe=Join-Path $out ($case.name+'.exe')
  $link=@()
  if($case.libs.Count){$link=@('/link')+$case.libs}
  & cl @flags @sources "/Fe:$exe" @link *> ($case.name+'-build.log')
  if($LASTEXITCODE -ne 0){throw "Native compile failed: $($case.name) $Config"}
  & $exe *> ($case.name+'-tests.log')
  if($LASTEXITCODE -ne 0){throw "Native regression failed: $($case.name) $Config"}
 }
 @{passed=$true;configuration=$Config;assertionsEnabled=$true;cases=@($cases.name);
   scope='diagnostics codec and temporal provenance/display/exclusion contracts; no physical presentation acceptance'}|
  ConvertTo-Json|Set-Content 'verification.json'
}finally{Pop-Location}
"PASS TR0 native diagnostics $Config"
