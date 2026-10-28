param([switch]$Apply)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
. "$PSScriptRoot/CppContainerTokens.ps1"
$files=@(& git -C $repo ls-files Engine Editor Lattice Player HeadlessHost Tools/ProfilerViewer Tools/runtime | Where-Object {
 $_ -match '\.(h|hpp|cpp|ixx|inl)$' -and $_ -notlike 'Engine/Containers/*'
})
$changes=@(foreach($path in $files){
 $full=Join-Path $repo $path
 $source=Read-CppMigrationSource $full
 $pattern=$script:CppContainerProtected+'|(?<array>\bstd::array\b)'
 $after=[regex]::Replace($source.Text,$pattern,[Text.RegularExpressions.MatchEvaluator]{param($match)
  if($match.Groups['array'].Success){return 'ce::fixed_array'}
  return $match.Value
 })
 if($after -eq $source.Text){continue}
 if($path.EndsWith('.ixx')){
  if($after -notmatch '(?:export\s+)?import\s+Creator\.Containers\s*;'){
   $after=[regex]::Replace($after,'(?m)^(export module [^;]+;)','$1'+"`nexport import Creator.Containers;")
  }
 }elseif(!$path.EndsWith('.inl') -and $after -notmatch '#include\s+"[^"\r\n]*Containers/ContainerAPI.h"'){
  $relative=[IO.Path]::GetRelativePath((Split-Path $full),(Join-Path $repo 'Engine/Containers/ContainerAPI.h')).Replace('\','/')
  $after='#include "'+$relative+'"'+"`n"+$after
 }
 $record=[ordered]@{path=$path;before=(Get-FileHash $full).Hash;applied=[bool]$Apply}
 if($Apply){[IO.File]::WriteAllText($full,$after,$source.Encoding)}
 $record
})
$output=Join-Path $repo ('Build/Verification/ContainerMigration/fixed-array-'+$(if($Apply){'applied'}else{'preview'})+'.json')
New-Item -ItemType Directory -Force (Split-Path $output)|Out-Null
ConvertTo-Json -InputObject @($changes) -Depth 3|Set-Content $output -Encoding utf8
"FIXED_ARRAY_MIGRATION: $($changes.Count) files; applied=$Apply"
