$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'ArtifactRetention.ps1')
$root=Join-Path $PSScriptRoot ('../../Artifacts/PR169/storage-contract-'+[guid]::NewGuid().ToString('N'))
$root=[IO.Path]::GetFullPath($root)
[IO.Directory]::CreateDirectory($root)|Out-Null
function Require($condition,$message){if(!$condition){throw $message}}
function Fixture([string]$name){
 $dir=Join-Path $root $name
 [IO.Directory]::CreateDirectory($dir)|Out-Null
 [IO.File]::WriteAllBytes((Join-Path $dir 'depth.f32'),[byte[]](1,2,3,4))
 [IO.File]::WriteAllText((Join-Path $dir 'oracle.json'),'preserve verdict')
 [IO.File]::WriteAllText((Join-Path $dir 'failure.log'),'preserve failure log')
 return $dir
}
$first=Fixture 'first';$second=Fixture 'second';$pinned=Fixture 'pinned';$third=Fixture 'third'
Complete-ArtifactRetention $first 'sdk-Debug-dlss'|Out-Null
Complete-ArtifactRetention $pinned 'sdk-Debug-dlss' -KeepRawCaptures|Out-Null
Complete-ArtifactRetention $second 'sdk-Debug-dlss'|Out-Null
Require (!(Test-Path "$first/depth.f32")) 'Previous success pixels retained'
Require (Test-Path "$first/oracle.json") 'Historical verdict deleted'
Require (Test-Path "$first/failure.log") 'Historical failure log deleted'
Require ((Get-Content "$first/artifact-retention.json" -Raw|ConvertFrom-Json).rawPixelsRetained -eq $false) 'Pruned success still claims raw replay'
Require (Test-Path "$pinned/depth.f32") 'Pinned pixels deleted'
Require (Test-Path "$second/depth.f32") 'Current representative pixels deleted'
$marker=Get-Content "$second/artifact-retention.json" -Raw|ConvertFrom-Json
$marker.passed=$false;$marker|ConvertTo-Json|Set-Content "$second/artifact-retention.json"
$rejected=$false
try{Complete-ArtifactRetention $third 'sdk-Debug-dlss'|Out-Null}catch{$rejected=$true}
Require $rejected 'Failure marker was pruned as a success'
Require (Test-Path "$second/depth.f32") 'Failed run pixels deleted'
$rejected=$false
try{Assert-ArtifactCapacity $root 100000}catch{$rejected=$true}
Require $rejected 'Insufficient disk capacity accepted'
$index=Join-Path $root '.artifact-retention/sdk-Debug-dlss.json'
@{directories=@($PSScriptRoot)}|ConvertTo-Json|Set-Content $index
$rejected=$false
try{Complete-ArtifactRetention $third 'sdk-Debug-dlss'|Out-Null}catch{$rejected=$true}
Require $rejected 'Out-of-scope registry accepted'
@{passed=$true;checks=10;scope='success rollover, preserved verdict/log/pin/current/failure, free-space and sibling boundary'}|
 ConvertTo-Json|Set-Content "$root/results.json"
Write-Output "PASS artifact retention contracts: $root"
