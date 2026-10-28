param([string]$Tag='20261026-modules-linux')
$ErrorActionPreference='Stop';$repo=Split-Path $PSScriptRoot -Parent
$out=Join-Path $repo 'results';$log=Join-Path $out "$Tag.txt"
if(Test-Path $log){throw 'Use a new validation tag'}
$linuxRoot=(& wsl.exe -d Ubuntu -- wslpath -a ($repo.Replace('\','/'))|Out-String).Trim()
$build="/tmp/$Tag"
& wsl.exe -d Ubuntu -- mkdir -p $build
if($LASTEXITCODE -ne 0){throw 'WSL directory failed'}
$manifest=Get-Content "$PSScriptRoot/manifest.json" -Raw|ConvertFrom-Json
$done=@{};$objects=@();$pending=@($manifest)
function Clang([string[]]$Arguments){
 & wsl.exe -d Ubuntu -- clang++ @Arguments 2>&1|Tee-Object $log -Append
 if($LASTEXITCODE -ne 0){throw "Clang failed: $Arguments"}
}
while($pending.Count){
 $progress=$false
 foreach($unit in @($pending)){
  $hostFile=Join-Path $PSScriptRoot ($unit.header.Replace('.h','.ixx'))
  $text=Get-Content $hostFile -Raw
  $deps=@([regex]::Matches($text,'export import :([a-z_0-9]+);')|ForEach-Object {$_.Groups[1].Value})
  if(@($deps|Where-Object {!$done.ContainsKey($_)}).Count){continue}
  $flags=@($done.Keys|ForEach-Object {"-fmodule-file=Creator.Containers:$($_)=$($done[$_])"})
  $pcm="$build/$($unit.partition).pcm";$obj="$build/$($unit.partition).o"
  Clang (@('-std=c++23','-pthread','--precompile','-x','c++-module',"$linuxRoot/modules/$($unit.header.Replace('.h','.ixx'))",'-o',$pcm)+$flags)
  Clang (@('-std=c++23','-pthread','-c',$pcm,'-o',$obj)+$flags)
  $objects+=$obj;$done[$unit.partition]=$pcm
  $pending=@($pending|Where-Object {$_.partition -ne $unit.partition});$progress=$true
 }
 if(!$progress){throw 'Module dependency cycle'}
}
$flags=@($done.Keys|ForEach-Object {"-fmodule-file=Creator.Containers:$($_)=$($done[$_])"})
Clang (@('-std=c++23','-pthread','--precompile','-x','c++-module',"$linuxRoot/modules/Creator.Containers.ixx",'-o',"$build/Creator.Containers.pcm")+$flags)
Clang (@('-std=c++23','-pthread','-c',"$build/Creator.Containers.pcm",'-o',"$build/Creator.Containers.o")+$flags)
$objects+="$build/Creator.Containers.o";$flags+="-fmodule-file=Creator.Containers=$build/Creator.Containers.pcm"
$generated=Join-Path $repo 'build-modules/linux-generated';New-Item -ItemType Directory -Path $generated -Force|Out-Null
foreach($pair in @(@('main.cpp','--test'),@('library.cpp',''),@('bounded_mpsc.cpp',''),@('concurrent.cpp','--test'),@('lock_free.cpp','--test'),@('allocation_alignment.cpp',''))){
 $name=[IO.Path]::GetFileNameWithoutExtension($pair[0]);$text=Get-Content "$repo/tests/$($pair[0])" -Raw
 $text=[regex]::Replace($text,'#include <ce/[^>]+>','// API from named module')
 [IO.File]::WriteAllText("$generated/$name.cpp","#include `"prelude.h`"`nimport Creator.Containers;`n"+$text,[Text.UTF8Encoding]::new($false))
 Clang (@('-std=c++23','-pthread',"-I$linuxRoot/tests/modules","-I$linuxRoot/tests",'-DCE_USE_FAA=1',"$linuxRoot/build-modules/linux-generated/$name.cpp")+$flags+$objects+@('-o',"$build/$name"))
 & wsl.exe -d Ubuntu -- "$build/$name" @($pair[1]|Where-Object {$_}) 2>&1|Tee-Object $log -Append
 if($LASTEXITCODE -ne 0){throw "Linux module test failed: $name"}
}
Clang (@('-std=c++23','-pthread',"$linuxRoot/tests/modules/import_only.cpp")+$flags+$objects+@('-o',"$build/import_only"))
& wsl.exe -d Ubuntu -- "$build/import_only"
if($LASTEXITCODE -ne 0){throw 'Import-only failed'}
"LINUX_NAMED_MODULES_PASS: 22 partitions + primary, 7 executions, compiler direct dependency mapping (clang-scan-deps unavailable)."|Tee-Object $log -Append
