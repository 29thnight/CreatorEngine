[CmdletBinding(SupportsShouldProcess, ConfirmImpact='Medium')]
param()
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$artifactRoot=[IO.Path]::GetFullPath((Join-Path $repo 'Artifacts/PR169'))
$relativeRoots=@(
 'closure-dlss-oracle-mutations',
 'closure-sdk-oracle-mutations',
 'sdk-quality-matrix/mutations-xess-ultra',
 'sdk-quality-matrix/mutations-fsr-ultra',
 'sdk-quality-matrix/oracle-regressions',
 'sdk-quality-matrix/oracle-final-regressions'
)
$files=@(foreach($relative in $relativeRoots){
 $target=[IO.Path]::GetFullPath((Join-Path $artifactRoot $relative))
 if(!$target.StartsWith($artifactRoot+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){
  throw "Target outside PR169 artifacts: $target"
 }
 if(!(Test-Path -LiteralPath $target)){continue}
 # Never follow junctions/symlinks into another location.
 $cursor=Get-Item -LiteralPath $target
 while($cursor){
  if($cursor.Attributes -band [IO.FileAttributes]::ReparsePoint){throw "Reparse path: $($cursor.FullName)"}
  if($cursor.FullName -eq $repo){break}
  $cursor=$cursor.Parent
 }
 $children=@(Get-ChildItem -LiteralPath $target -Recurse -Force)
 if($children|Where-Object {$_.Attributes -band [IO.FileAttributes]::ReparsePoint}){throw "Reparse child under $target"}
 foreach($file in $children){
  if(!$file.PSIsContainer -and $file.Extension -eq '.f32'){
   if(!$file.FullName.StartsWith($target+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){
    throw "File outside selected duplicate folder: $($file.FullName)"
   }
   $file
  }
 }
})
$bytes=($files|Measure-Object Length -Sum).Sum
Write-Output ('Duplicate pixels: {0} files / {1:N2} GiB' -f $files.Count,($bytes/1GB))
if(!$WhatIfPreference){
 $files|Select-Object FullName,Length|ConvertTo-Json -Depth 3|
  Set-Content -LiteralPath (Join-Path $artifactRoot 'storage-cleanup-20261011-pixels.json') -Encoding utf8
}
$removed=0L
foreach($file in $files){
 if($PSCmdlet.ShouldProcess($file.FullName,'Delete duplicate mutation pixel payload')){
  Remove-Item -LiteralPath $file.FullName -Force
  $removed+=$file.Length
 }
}
Write-Output ('Removed {0:N2} GiB; C: free {1:N2} GiB' -f ($removed/1GB),((Get-PSDrive C).Free/1GB))
