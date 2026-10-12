# Shared storage controls. Metadata, logs and .ceprof records are never pruned.
function Assert-ArtifactCapacity([string]$Directory,[double]$MinimumFreeGiB=30) {
 $path=[IO.Path]::GetFullPath($Directory)
 $drive=[IO.DriveInfo]::new([IO.Path]::GetPathRoot($path))
 if($drive.AvailableFreeSpace -lt $MinimumFreeGiB*1GB){
  throw ('Artifact capture refused: {0:N2} GiB free; require {1} GiB.' -f ($drive.AvailableFreeSpace/1GB),$MinimumFreeGiB)
 }
}

function Remove-ArtifactPixels([string]$Directory,[string[]]$KeepDirectories=@()) {
 $root=[IO.Path]::GetFullPath($Directory).TrimEnd('\','/')
 if(!(Test-Path -LiteralPath $root)){throw "Artifact directory missing: $root"}
 $cursor=Get-Item -LiteralPath $root
 while($cursor){
  if($cursor.Attributes -band [IO.FileAttributes]::ReparsePoint){throw "Reparse parent: $($cursor.FullName)"}
  $cursor=$cursor.Parent
 }
 $kept=@($KeepDirectories|ForEach-Object {[IO.Path]::GetFullPath($_).TrimEnd('\','/')})
 $children=@(Get-ChildItem -LiteralPath $root -Recurse -Force)
 if($children|Where-Object {$_.Attributes -band [IO.FileAttributes]::ReparsePoint}){throw 'Reparse artifact child found'}
 $files=@($children|Where-Object {
  $file=$_
  !$file.PSIsContainer -and $file.Extension -eq '.f32' -and !($kept|Where-Object {
   $file.FullName.StartsWith($_+'\',[StringComparison]::OrdinalIgnoreCase)
  })
 })
 foreach($file in $files){
  if(!$file.FullName.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Pixel outside artifact root'}
 }
 $before=[IO.DriveInfo]::new([IO.Path]::GetPathRoot($root)).AvailableFreeSpace
 $ledger=Join-Path $root ('pixel-retention-'+[guid]::NewGuid().ToString('N')+'.json')
 @{schema='artifact.pixel-retention.v1';state='planned';replayable=$false;keptDirectories=$kept;
   files=@($files|Select-Object FullName,Length);note='Only redundant raw pixels selected; metadata/logs/profiles retained. Logical sizes include hardlinks.'}|
  ConvertTo-Json -Depth 8|Set-Content -LiteralPath $ledger -Encoding utf8
 foreach($file in $files){Remove-Item -LiteralPath $file.FullName -Force -ErrorAction Stop}
 $after=[IO.DriveInfo]::new([IO.Path]::GetPathRoot($root)).AvailableFreeSpace
 @{schema='artifact.pixel-retention-result.v1';state='completed';ledger=$ledger;removedFiles=$files.Count;
  actualRecoveredGiB=($after-$before)/1GB;freeGiB=$after/1GB;pixelReplayAvailable=$false}|
  ConvertTo-Json|Set-Content -LiteralPath ($ledger+'.result.json') -Encoding utf8
 Write-Output ('Pruned {0} raw pixel files; actual recovery {1:N2} GiB; free {2:N2} GiB' -f $files.Count,(($after-$before)/1GB),($after/1GB))
}

function Complete-ArtifactRetention([string]$Directory,[string]$Family,[switch]$KeepRawCaptures) {
 if($Family -notmatch '^[A-Za-z0-9_-]+$'){throw 'Invalid retention family'}
 $run=[IO.Path]::GetFullPath($Directory).TrimEnd('\','/')
 $parent=Split-Path $run
 $registry=Join-Path $parent '.artifact-retention'
 [IO.Directory]::CreateDirectory($registry)|Out-Null
 $index=Join-Path $registry ($Family+'.json')
 if(!$KeepRawCaptures -and (Test-Path -LiteralPath $index)){
  $previous=Get-Content -LiteralPath $index -Raw|ConvertFrom-Json
  foreach($old in @($previous.directories)){
   $old=[IO.Path]::GetFullPath($old).TrimEnd('\','/')
   # Only a previously registered sibling success may be pruned.
   if((Split-Path $old) -ne $parent){throw 'Retention index points outside sibling runs'}
   if($old -ne $run -and (Test-Path -LiteralPath $old)) {
    $marker=Join-Path $old 'artifact-retention.json'
    if(!(Test-Path -LiteralPath $marker)){throw 'Previous run lacks success retention marker'}
    $metadata=Get-Content -LiteralPath $marker -Raw|ConvertFrom-Json
    if($metadata.passed -isnot [bool] -or !$metadata.passed -or $metadata.family -ne $Family -or
       $metadata.pinned -isnot [bool] -or $metadata.pinned){throw 'Previous run is not an unpinned success of this family'}
    Remove-ArtifactPixels $old|Write-Output
    $metadata.rawPixelsRetained=$false
    $metadata|ConvertTo-Json|Set-Content -LiteralPath $marker -Encoding utf8
   }
  }
 }
 if(!$KeepRawCaptures){
  @{schema='artifact.success-retention.v1';family=$Family;directories=@($run);retainedSuccessfulRuns=1}|
   ConvertTo-Json|Set-Content -LiteralPath $index -Encoding utf8
 }
 @{schema='artifact.run-retention.v1';passed=$true;family=$Family;rawPixelsRetained=$true;
  pinned=[bool]$KeepRawCaptures;policy='one latest successful raw run per family; failures untouched; logs/metadata/profiles preserved'}|
  ConvertTo-Json|Set-Content -LiteralPath (Join-Path $run 'artifact-retention.json') -Encoding utf8
}
