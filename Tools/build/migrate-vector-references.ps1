param([switch]$Apply)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
. "$PSScriptRoot/CppContainerTokens.ps1"
$files=@(& git -C $repo ls-files Engine Editor Lattice Player HeadlessHost Tools/ProfilerViewer Tools/runtime | Where-Object {
 $_ -match '\.(h|hpp|cpp|ixx|inl)$' -and $_ -notlike 'Engine/Containers/*'
})
$changed=@()
foreach($path in $files){
 $full=Join-Path $repo $path
 $probe=[IO.File]::ReadAllText($full)
 if($probe -notmatch '\bstd::vector\b' -and $probe -notmatch '\bstd::erase(?:_if)?\s*\('){continue}
 $source=Read-CppMigrationSource $full
 $before=$source.Text
 if($before -notmatch '\bstd::vector\b' -and $before -notmatch '\bstd::erase(?:_if)?\s*\('){continue}
 $after=Convert-CppVectorTokens $before
 if($path -eq 'Engine/Utility_Framework/Reflection/ReflgenAuthoring.h'){
  $after=$after.Replace('ce::dynamic_array<std::byte> read_bytes() override','std::vector<std::byte> read_bytes() override')
  $after=$after.Replace('ce::dynamic_array<std::byte> bytes(decoded.size());','std::vector<std::byte> bytes(decoded.size());')
 }
 $after=[regex]::Replace($after,'ce::dynamic_array<\s*(profile_event|segment)\s*,\s*capture_allocator<\s*\1\s*>\s*>','ce::dynamic_array<$1, capture_byte_allocator>')
 if($after -eq $before){continue}
 if($path.EndsWith('.ixx')){
  if($after -notmatch '(?:export\s+)?import\s+Creator\.Containers\s*;'){
   $after=[regex]::Replace($after,'(?m)^(export module [^;]+;)','$1'+"`nexport import Creator.Containers;")
  }
 }elseif(!$path.EndsWith('.inl')){
  $relative=[IO.Path]::GetRelativePath((Split-Path $full),(Join-Path $repo 'Engine/Containers/ContainerAPI.h')).Replace('\','/')
  $after='#include "'+$relative+'"'+"`n"+$after
 }
 $changed+=[ordered]@{path=$path;before=(Get-FileHash $full).Hash;applied=[bool]$Apply}
 if($Apply){[IO.File]::WriteAllText($full,$after,$source.Encoding)}
}
$output=Join-Path $repo ('Build/Verification/ContainerMigration/vector-'+$(if($Apply){'applied'}else{'preview'})+'.json')
New-Item -ItemType Directory -Force (Split-Path $output)|Out-Null
$changed|ConvertTo-Json -Depth 3|Set-Content $output -Encoding utf8
"VECTOR_MIGRATION: $($changed.Count) files; applied=$Apply"
