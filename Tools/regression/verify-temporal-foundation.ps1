param(
 [ValidateSet('Debug','Release')][string]$Config='Debug',
 [string]$Work=(Join-Path $PSScriptRoot '../../Artifacts/PR169/phase45-closure-20261011')
)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'ArtifactRetention.ps1')
Assert-ArtifactCapacity $Work
. (Join-Path $PSScriptRoot 'ContractProbeConfiguration.ps1')
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out=Join-Path ([IO.Path]::GetFullPath($Work)) "native-$Config"
[IO.Directory]::CreateDirectory($out)|Out-Null
& 'C:/Program Files/Microsoft Visual Studio/18/Community/Common7/Tools/Launch-VsDevShell.ps1' -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
$flags=@('/nologo','/std:c++latest','/EHsc','/utf-8','/DNOMINMAX','/UNDEBUG')
$flags+=('/I'+(Join-Path $repo 'Engine/Utility_Framework'))
$flags+=if($Config -eq 'Debug'){@('/Od','/MDd','/Zi')}else{@('/O2','/MD')}
$contractProbe = New-ContractProbeConfiguration -Configuration $Config -OutputDirectory $out
$flags += $contractProbe.Arguments
$cases=@(
 @{name='runtime-contract';sources=@('Tools/regression/TemporalRuntimeContractTests.cpp','Engine/RenderEngine/Render/Temporal/TemporalReconstruction.cpp','Engine/RenderEngine/Render/Temporal/TemporalRuntimeControl.cpp');libs=@();defines=@()},
 @{name='development-fault';sources=@('Tools/regression/TemporalFaultControlTests.cpp','Engine/RenderEngine/Render/Temporal/TemporalRuntimeControl.cpp');libs=@();defines=@('/DCE_DEVELOPMENT=1','/DCE_SHIPPING=0')},
 @{name='shipping-fault';sources=@('Tools/regression/TemporalFaultControlTests.cpp','Engine/RenderEngine/Render/Temporal/TemporalRuntimeControl.cpp');libs=@();defines=@('/DCE_DEVELOPMENT=0','/DCE_SHIPPING=1')}
)
Push-Location $out
try {
 foreach($case in $cases){
  $sources=@($case.sources|ForEach-Object {Join-Path $repo $_})
  $exe=Join-Path $out ($case.name+'.exe')
  $link=@()
  if($case.libs.Count){$link=@('/link')+$case.libs}
  & cl @flags @($case.defines) @sources "/Fe:$exe" @link *> ($case.name+'-build.log')
  if($LASTEXITCODE -ne 0){throw "Native compile failed: $($case.name) $Config"}
  & $exe *> ($case.name+'-tests.log')
  if($LASTEXITCODE -ne 0){throw "Native regression failed: $($case.name) $Config"}
 }
 @{passed=$true;configuration=$Config;assertionsEnabled=$true;cases=@($cases.name);
   scope='TR2 jitter and TU0 capability selection; development and shipping fault contracts; no GPU pixel or physical presentation acceptance'}|
  ConvertTo-Json|Set-Content 'verification.json'
}finally{Pop-Location}
"PASS temporal foundation $Config"

