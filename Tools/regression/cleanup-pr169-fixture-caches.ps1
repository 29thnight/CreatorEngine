[CmdletBinding()]
param([switch]$Apply)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../Artifacts/PR169'))
$targets=@('live/Project/Library/TextureAssetGenerations','live/PlayerStage/.package-input')
$files=@(foreach($relative in $targets){
 $target=[IO.Path]::GetFullPath((Join-Path $root $relative))
 if(!$target.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Cache outside artifact root'}
 if(!(Test-Path -LiteralPath $target)){continue}
 $parent=Get-Item -LiteralPath $target
 while($parent){
  if($parent.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Reparse cache parent'}
  $parent=$parent.Parent
 }
 $children=@(Get-ChildItem -LiteralPath $target -Recurse -Force)
 if($children|Where-Object {$_.Attributes -band [IO.FileAttributes]::ReparsePoint}){throw 'Reparse cache child'}
 foreach($file in $children){
  if(!$file.PSIsContainer -and $file.Extension -notin @('.json','.jsonl','.log','.ceprof','.dmp')){
   if(!$file.FullName.StartsWith($target+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Copy outside cache target'}
   $file
  }
 }
})
$before=(Get-PSDrive C).Free
@{schema='artifact.fixture-cache-cleanup.v1';targets=$targets;files=@($files|Select-Object FullName,Length);
 note='Only private imported texture cache and transient package-input copies; original Dynamic_CPP and packaged GameAssets.pak retained.'}|
 ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $root 'fixture-cache-cleanup-20261011.json') -Encoding utf8
if($Apply){
 foreach($file in $files){Remove-Item -LiteralPath $file.FullName -Force -ErrorAction Stop}
 $after=(Get-PSDrive C).Free
 @{completed=$true;removedFiles=$files.Count;actualRecoveredGiB=($after-$before)/1GB;freeGiB=$after/1GB}|
  ConvertTo-Json|Set-Content -LiteralPath (Join-Path $root 'fixture-cache-cleanup-20261011.result.json') -Encoding utf8
 Write-Output ('Fixture copies pruned: actual recovery {0:N2} GiB; free {1:N2} GiB' -f (($after-$before)/1GB),($after/1GB))
}else{Write-Output ('Planned private-cache files: '+$files.Count)}
