param([switch]$Check)
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$manifest=Get-Content "$PSScriptRoot/manifest.json" -Raw|ConvertFrom-Json
foreach($entry in $manifest){
 $source=Join-Path $PSScriptRoot ($entry.header.Replace('.h','.ixx'))
 $text=[IO.File]::ReadAllText($source)
 $match=[regex]::Match($text,'(?s)// CE_MODULE_BODY_BEGIN\r?\nexport \{\r?\n(.*)\} // export\r?\n// CE_MODULE_BODY_END')
 if(!$match.Success){throw "Missing canonical module body: $source"}
 $body=$match.Groups[1].Value.Replace("`r`n","`n")
 if($entry.header -eq 'memory.h'){
  $a=$body.IndexOf('#if defined(_MSC_VER)');$b=$body.IndexOf('namespace ce {')
  $config="#pragma once`n"+$body.Substring($a,$b-$a)
  $configPath=Join-Path $PSScriptRoot 'module_config.h'
  if($Check){if([IO.File]::ReadAllText($configPath).Replace("`r`n","`n") -cne $config){throw 'Generated module config stale'}}
  else{[IO.File]::WriteAllText($configPath,$config,[Text.UTF8Encoding]::new($false))}
 }
 $body=[regex]::Replace($body,'(?m)^// CE_HEADER_INCLUDE (#include[^\n]*)$','$1')
 $header="#pragma once`n"+$body
 if($entry.newline -eq 'crlf'){$header=$header.Replace("`n","`r`n")}
 $target=Join-Path $repo "include/ce/$($entry.header)"
 if($Check){if([IO.File]::ReadAllText($target) -cne $header){throw "Generated header is stale: $target"}}
 else{[IO.File]::WriteAllText($target,$header,[Text.UTF8Encoding]::new($false))}
}
$all="#pragma once`n"+(@($manifest|ForEach-Object {'#include "'+$_.header+'"'}) -join "`n")+"`n"
$allPath=Join-Path $repo 'include/ce/all.h'
if($Check){if(!(Test-Path $allPath) -or [IO.File]::ReadAllText($allPath) -cne $all){throw 'Generated aggregate API header is stale'}}
else{[IO.File]::WriteAllText($allPath,$all,[Text.UTF8Encoding]::new($false))}
$references=@('<Project>','  <Target Name="ReferenceCreatorContainerModules" Condition="''$(ReflgenTargetsImported)'' == ''true'' and ''@(ReferencedProjectsPublicModules)'' != ''''">','    <ItemGroup>')
foreach($entry in @([pscustomobject]@{header='Creator.Containers.h';partition=''})+@($manifest)){
 $unit=[IO.Path]::GetFileName($entry.header.Replace('.h','.ixx'))
 $name=if($entry.partition){'Creator.Containers:'+$entry.partition}else{'Creator.Containers'}
 $references+=('      <_CreatorReflgenModuleReference Include="@(ReferencedProjectsPublicModules)" Condition="''%(ReferencedProjectsPublicModules.Filename)'' == '''+$unit+'''">')
 $references+=('        <ModuleName>'+$name+'</ModuleName>')
 $references+='      </_CreatorReflgenModuleReference>'
}
$references+=@('    </ItemGroup>','  </Target>','</Project>')
$referenceText=($references -join "`n")+"`n"
$referencePath=Join-Path $PSScriptRoot 'msbuild-references.targets'
if($Check){if(!(Test-Path $referencePath) -or [IO.File]::ReadAllText($referencePath) -cne $referenceText){throw 'Generated native module references are stale'}}
else{[IO.File]::WriteAllText($referencePath,$referenceText,[Text.UTF8Encoding]::new($false))}
"CANONICAL_MODULE_SYNC: $($manifest.Count) implementations, headers $(if($Check){'verified'}else{'generated'})."
