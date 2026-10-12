#Requires -Version 7.0
param(
 [Parameter(Mandatory)][string]$Work,
 [string]$Python='python'
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$workRoot=[IO.Path]::GetFullPath($Work)
. (Join-Path $PSScriptRoot 'ArtifactRetention.ps1')
Assert-ArtifactCapacity $workRoot 85
if(Test-Path -LiteralPath $workRoot){throw 'Use a fresh workload directory; failures and prior verdicts are preserved'}
if(Get-Process CreatorEditor -ErrorAction SilentlyContinue){throw 'Close Editor before isolated workload validation'}
[IO.Directory]::CreateDirectory($workRoot)|Out-Null
@{schema='temporal.base0.storage-start.v1';freeGiB=[IO.DriveInfo]::new([IO.Path]::GetPathRoot($workRoot)).AvailableFreeSpace/1GB;
 estimatedGiB=55;binaryCopies=0;scope='sequential current-product Debug/Release, two processes each'}|
 ConvertTo-Json|Set-Content "$workRoot/storage-start.json" -Encoding utf8
& (Join-Path $PSScriptRoot 'new-temporal-base0-fixture.ps1') -OutputDirectory "$workRoot/PreparedFixture"
foreach($config in @('Debug','Release')){
 & (Join-Path $PSScriptRoot 'verify-tr0-diagnostics.ps1') -Config $config -Work $workRoot `
  *> "$workRoot/$config-native.log"
 if($LASTEXITCODE){throw "$config native diagnostics failed"}
 & (Join-Path $PSScriptRoot 'verify-render-base0.ps1') -Configuration $config `
  -FixtureProject "$workRoot/PreparedFixture" -OutputDirectory "$workRoot/$config" `
  -Scene TR0_Native_Workload.creator -Python $Python -GraphSamples 100 -PerformanceSamples 100 `
  *> "$workRoot/$config-console.log"
 if($LASTEXITCODE){throw "$config BASE-0 failed; preserve the failure and inspect before retry"}
}
& $Python (Join-Path $PSScriptRoot 'verify-temporal-base0-consumers.py') $workRoot *> "$workRoot/consumer-verification.log"
if($LASTEXITCODE){throw 'Actual BASE-0/RG6 consumer verification failed'}
@{schema='temporal.base0.storage-end.v1';freeGiB=[IO.DriveInfo]::new([IO.Path]::GetPathRoot($workRoot)).AvailableFreeSpace/1GB;
 retainedRepresentativeCaptures=2;binaryCopies=0;rawReplayScope='one representative per configuration; ledgers preserve historical verdicts'}|
 ConvertTo-Json|Set-Content "$workRoot/storage-end.json" -Encoding utf8
'TR0_BASE0_DEBUG_RELEASE_OK'
