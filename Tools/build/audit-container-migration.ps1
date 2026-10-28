param([string]$Output='', [switch]$Verify, [switch]$IncludeUntracked,
 [string[]]$Scope=@('Engine','Editor','Lattice','Player','HeadlessHost'))
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
. "$PSScriptRoot/CppContainerTokens.ps1"
if(!$Output){$Output=Join-Path $repo 'Build/Verification/ContainerMigration/inventory.json'}
$pattern='\bstd::(?:(?<pmr>pmr)::)?(?<kind>'+$script:CppContainerKinds+')\b'
$gitOptions=if($IncludeUntracked){@('--cached','--others','--exclude-standard')}else{@()}
$files=@(& git -C $repo -c core.quotepath=false ls-files @gitOptions -- $Scope | Where-Object {
 $_ -match '\.(h|hpp|cpp|ixx|inl)$' -and $_ -notlike 'Engine/Containers/*' -and (Test-Path -LiteralPath (Join-Path $repo $_) -PathType Leaf)
})
if($LASTEXITCODE){throw 'Source enumeration failed'}
$rawCount=0
$occurrences=@(foreach($file in $files){
 $source=Read-CppMigrationSource (Join-Path $repo $file)
 $text=$source.Text
 $rawCount+=[regex]::Matches($text,$pattern).Count
 $line=1;$next=$text.IndexOf("`n");$lineStart=0
 foreach($match in Get-CppContainerReferences $text){
  while($next -ge 0 -and $next -lt $match.Index){++$line;$lineStart=$next+1;$next=$text.IndexOf("`n",$lineStart)}
  $lineEnd=if($next -ge 0){$next}else{$text.Length}
  [ordered]@{path=$file;line=$line;kind=$match.Groups['kind'].Value;pmr=$match.Groups['pmr'].Success;text=$text.Substring($lineStart,$lineEnd-$lineStart).Trim()}
 }
})
$summary=@($occurrences|Group-Object {$_.kind}|Sort-Object Name|ForEach-Object {[ordered]@{kind=$_.Name;occurrences=$_.Count}})
$report=[ordered]@{created=(Get-Date -Format o);scope=$Scope;
 policy='Container and callable storage code references, including signatures and static calls. Literals/comments excluded lexically. Excludes ThirdParty, Tools, generated build output and canonical implementation.';
 includesUntracked=[bool]$IncludeUntracked;complete=($occurrences.Count -eq 0);sourceFiles=$files.Count;candidateFiles=@($occurrences.path|Sort-Object -Unique).Count;
 occurrences=$occurrences.Count;rawReferences=$rawCount;summary=$summary;locations=$occurrences}
New-Item -ItemType Directory -Force (Split-Path ([IO.Path]::GetFullPath($Output)))|Out-Null
$report|ConvertTo-Json -Depth 6|Set-Content $Output -Encoding utf8
$summary|ForEach-Object {"$($_.kind): $($_.occurrences)"}
"CONTAINER_MIGRATION_INVENTORY: $($occurrences.Count) references; $($report.candidateFiles) candidate files; $Output"
if($Verify -and !$report.complete){throw 'Container migration is incomplete; inspect the inventory'}
