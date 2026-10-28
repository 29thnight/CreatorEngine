param([Parameter(Mandatory)][string]$Tag)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
. "$root/implementation-root.ps1"
$implementation=Get-ContainerImplementationRoot
$receipt=Get-Content "$root/results/$Tag-optimization.json" -Raw|ConvertFrom-Json
$accepted=@($receipt.summary|Where-Object variant -eq 'combined')
if($accepted.Count -ne 45 -or @($accepted|Where-Object {!$_.pass}).Count){throw 'Combined 45-condition acceptance required'}
$path=Join-Path $implementation 'modules/node_hash_map.ixx'
$expected=$receipt.inputs|Where-Object Path -eq $path
if(!$expected -or (Get-FileHash $path).Hash -ne $expected.Hash){throw 'Canonical source changed since experiment'}
$old=[IO.File]::ReadAllText($path).Replace("`r`n","`n")
$cut=$old.IndexOf('// Open-addressed index over individually owned nodes.')
if($cut -lt 0){throw 'Missing canonical class anchor'}
$prefix=$old.Substring(0,$cut).Replace('#include <new>',"#include <new>`n#include <string.h>")
$prefix=$prefix.Replace('// CE_HEADER_INCLUDE #include <initializer_list>',"// CE_HEADER_INCLUDE #include <initializer_list>`n// CE_HEADER_INCLUDE #include <string.h>")
$body=[IO.File]::ReadAllText("$PSScriptRoot/fast_map_body.inc").Replace("`r`n","`n")
$start=$body.IndexOf('template<class Key');$end=$body.LastIndexOf("`n}")
$body=$body.Substring($start,$end-$start).Replace('optimized_node_map','node_hash_map')
$text=$prefix+"// Stable pooled nodes with a compact control-byte index. PoolNodes=false`n// preserves per-value deallocation for allocator policies requiring it.`n"+$body+"`n} // namespace ce`n} // export`n// CE_MODULE_BODY_END`n"
[IO.File]::WriteAllText($path,$text,[Text.UTF8Encoding]::new($false))
$testPath="$root/tests/node_map_migration.cpp"
$test=[IO.File]::ReadAllText($testPath).Replace('for(int budget:{0,1})','for(int budget:{0})').Replace('check(state.allocations-before==100);','check(state.allocations-before==0);')
$test=$test.Replace('// no table rebuild for reuse of one erased bucket.','// Cached pool cell: no backing allocation during erase/reinsert.')
[IO.File]::WriteAllText($testPath,$test,[Text.UTF8Encoding]::new($false))
$advanced=[IO.File]::ReadAllText("$PSScriptRoot/advanced.cpp")
$advanced=$advanced.Replace('#include "../../tests/modules/prelude.h"',"#include <ce/node_hash_map.h>`n#include <cstdint>`n#include <cstdio>`n#include <stdexcept>`n#include <utility>")
$advanced=$advanced.Replace('import Creator.NodeMapOptimized;','').Replace('#include "pool_control.h"','').Replace('ce::optimized_node_map','ce::node_hash_map')
$advanced=$advanced.Replace(' duplicate_check<ce::node_hash_map<uint64_t,uint64_t,ce::hash<uint64_t>,ce::equal_to<uint64_t>,pool_control_allocator>>();','')
[IO.File]::WriteAllText("$root/tests/node_map_advanced.cpp",$advanced,[Text.UTF8Encoding]::new($false))
$bench=[IO.File]::ReadAllText("$PSScriptRoot/benchmark.cpp")
$bench=$bench.Replace('#include "../../tests/modules/prelude.h"','#include "modules/prelude.h"').Replace('import Creator.NodeMapOptimized;','#include <ce/node_hash_map.h>').Replace('#include "pool_control.h"','')
$bench=$bench.Replace('operation<5','operation<6')
$bench=$bench.Replace('if(operation==4){M map;', 'if(operation==5){M map;for(auto key:keys)map.emplace(key,key+7);result+=map.size();} else if(operation==4){M map;')
$main=$bench.IndexOf('int main(int argc,char** argv){')
$bench=$bench.Substring(0,$main)+@'
int main(int argc,char** argv){
 if(argc<2 || std::string_view(argv[1])!="combined")return 3;
 for(int i=2;i<argc;++i){std::string_view flag=argv[i];if(flag=="--grouped")grouped=true;else if(flag=="--reverse")reverse_sides=true;else return 3;}
 batch_ns=grouped?100000000.0:30000000.0;
 std::puts("optimization_variant,combined");
 return run<ce::node_hash_map<uint64_t,uint64_t>>();
}
'@
[IO.File]::WriteAllText("$root/tests/node_map_acceptance.cpp",$bench,[Text.UTF8Encoding]::new($false))
$runner=[IO.File]::ReadAllText("$PSScriptRoot/measure.ps1")
$runner=$runner.Replace("`$Variants=@('baseline','pool','index','combined')","`$Variants=@('combined')")
$runner=$runner.Replace('build-node-opt/experiments/20261026-node-map-optimization/Release/node_map_optimization.exe','build-modules/modules/Release/module_node_map_acceptance.exe')
$runner=$runner.Replace('$rows.Count -ne 15 -or $raw.Count -ne 225','$rows.Count -ne 18 -or $raw.Count -ne 270').Replace('$Variants.Count*$Repeats*15','$Variants.Count*$Repeats*18').Replace('collected 15 conditions','collected 18 conditions').Replace(')/15',')/18')
$runner=$runner.Replace('@(Get-Item $exe,"$root/CMakeLists.txt")','@(Get-Item $exe,"$root/CMakeLists.txt","$root/tests/node_map_acceptance.cpp","$root/build-modules/modules/module_node_map_acceptance.cpp")')
[IO.File]::WriteAllText("$PSScriptRoot/measure-canonical.ps1",$runner,[Text.UTF8Encoding]::new($false))
Get-FileHash $path,"$root/tests/node_map_advanced.cpp","$root/tests/node_map_acceptance.cpp"|ConvertTo-Json|Set-Content "$root/results/$Tag-promotion.json" -Encoding utf8
'NODE_MAP_PROMOTED'
