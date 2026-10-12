[CmdletBinding(SupportsShouldProcess,ConfirmImpact='Medium')]
param()
$ErrorActionPreference='Stop'
$roots=@(
 'C:\Users\idene\source\repos\CreatorEngine-PR169\Build\Obj',
 'C:\Users\idene\source\repos\CreatorEngine-PR169\Build\Lib',
 'C:\Users\idene\source\repos\CreatorEngine\Build\Obj',
 'C:\Users\idene\source\repos\CreatorEngine\Build\Lib'
)
$active=Get-Process -Name MSBuild,cl,link -ErrorAction SilentlyContinue
if($active){throw 'Build is active; wait for it to finish before cleanup.'}
$extensions=@('.obj','.lib','.pdb','.ilk','.pch','.ipch','.idb','.res','.exp','.iobj','.ipdb')
$files=@(foreach($root in $roots){
 if(!(Test-Path -LiteralPath $root)){continue}
 $resolved=(Resolve-Path -LiteralPath $root).Path
 if($resolved -ne $root){throw "Unexpected resolved root: $resolved"}
 $cursor=Get-Item -LiteralPath $root
 while($cursor){
  if($cursor.Attributes -band [IO.FileAttributes]::ReparsePoint){throw "Reparse parent: $($cursor.FullName)"}
  $cursor=$cursor.Parent
 }
 $children=@(Get-ChildItem -LiteralPath $root -Recurse -Force)
 if($children|Where-Object {$_.Attributes -band [IO.FileAttributes]::ReparsePoint}){throw "Reparse child under $root"}
 foreach($file in $children){
  if(!$file.PSIsContainer -and $extensions -contains $file.Extension.ToLowerInvariant()){
   if(!$file.FullName.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Outside cache root'}
   $file
  }
 }
})
$before=(Get-PSDrive C).Free
Write-Output ('Selected compiler files: {0}; logical size {1:N2} GiB (not a physical-space guarantee)' -f $files.Count,(($files|Measure-Object Length -Sum).Sum/1GB))
if(!$WhatIfPreference){
 $files|Select-Object FullName,Length|ConvertTo-Json -Depth 3|
  Set-Content -LiteralPath (Join-Path $PSScriptRoot '../../Artifacts/PR169/storage-cleanup-build-cache-20261011.json') -Encoding utf8
}
foreach($file in $files){
 if($PSCmdlet.ShouldProcess($file.FullName,'Delete regenerable compiler cache')){
  Remove-Item -LiteralPath $file.FullName -Force
 }
}
$after=(Get-PSDrive C).Free
Write-Output ('Actual space recovered {0:N2} GiB; C: free {1:N2} GiB' -f (($after-$before)/1GB),($after/1GB))
