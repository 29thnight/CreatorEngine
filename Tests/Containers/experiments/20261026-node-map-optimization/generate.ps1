$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
. "$root/implementation-root.ps1"
$canonical=Join-Path (Get-ContainerImplementationRoot) 'modules/node_hash_map.ixx'
if((Get-FileHash $canonical).Hash -ne (Get-FileHash "$PSScriptRoot/baseline-node-map.ixx").Hash){throw 'Historical baseline changed. Replay with CE_IMPLEMENTATION_ROOT pointing to the frozen pre-promotion snapshot; canonical tests use Creator.Containers directly.'}
$body=[IO.File]::ReadAllText("$PSScriptRoot/fast_map_body.inc")
$prefix="module;`n#include <new>`n#include <stddef.h>`n#include <stdint.h>`n#include <string.h>`n#include <initializer_list>`n#include `"module_config.h`"`nexport module Creator.NodeMapOptimized;`nexport import Creator.Containers;`nexport {`n"
[IO.File]::WriteAllText("$PSScriptRoot/Creator.NodeMapOptimized.ixx",$prefix+$body+"`n}`n",[Text.UTF8Encoding]::new($false))
$test=[IO.File]::ReadAllText("$PSScriptRoot/baseline-tests.cpp")
$test=[regex]::Replace($test,'#include <ce/[^>]+>','// Named module')
$test=$test.Replace('ce::node_hash_map','probe_map')
$test=$test.Replace('for(int budget:{0,1})','for(int budget:{0,1})')
$test=$test.Replace('check(state.allocations-before==100);','check(state.allocations-before==(OPT_POOL?0:100));')
# A cached pool cell makes this insertion allocation-free after its index grows.
$test=$test.Replace('for(int budget:{0,1}){','for(int budget=0;budget<(OPT_POOL?1:2);++budget){')
$prefix=@'
#include "../../tests/modules/prelude.h"
import Creator.NodeMapOptimized;
template<class K,class V,class H=ce::hash<K>,class E=ce::equal_to<K>,class A=ce::heap_allocator>
using probe_map=ce::optimized_node_map<K,V,H,E,A,OPT_POOL>;
'@
[IO.File]::WriteAllText("$PSScriptRoot/correctness.cpp",$prefix+"`n"+$test,[Text.UTF8Encoding]::new($false))
$bench=[IO.File]::ReadAllText("$root/tests/node_map_benchmark.cpp")
$bench=[regex]::Replace($bench,'#include <ce/[^>]+>','// Named module')
$start=$bench.IndexOf('int main(int argc,char** argv){')
$bench=$bench.Substring(0,$start)
$bench=$bench.Replace('  std::array<double,15> custom{},standard{};', '  std::printf("node_map_rounds,%zu,%d,%zu,%zu\n",size,operation,cr,sr);'+"`n"+'  std::array<double,15> custom{},standard{};')
$bench=$bench.Replace('M reused;if(operation)','M reused;if(operation>0 && operation<4)')
$bench=$bench.Replace('if(operation==0){M map;', 'if(operation==4){M map;map.reserve(count);result+=map.bucket_count();} else if(operation==0){M map;')
$bench=$bench.Replace('30000000.0','batch_ns').Replace('operation<4','operation<5')
# Keep the prefilled lookup fixture restricted to operations 1..3.
$bench=$bench.Replace('if(operation>0 && operation<5)','if(operation>0 && operation<4)')
$sampleStart=$bench.IndexOf('  for(size_t sample=0;sample<15;++sample){')
$sampleEnd=$bench.IndexOf('  std::sort(custom.begin()', $sampleStart)
if($sampleStart -lt 0 -or $sampleEnd -lt 0){throw 'Missing sample loop'}
$samples=@'
  auto a=[&]{return measure<C>(operation,size,cr);};
  auto b=[&]{return measure<S>(operation,size,sr);};
  if(grouped){
    auto collect=[&](auto& work,auto& output){
      HEAP_OPTIMIZE_RESOURCES_INFORMATION info{HEAP_OPTIMIZE_RESOURCES_CURRENT_VERSION,0};
      if(!HeapSetInformation(GetProcessHeap(),HeapOptimizeResources,&info,sizeof(info)))std::abort();
      const auto start=std::chrono::steady_clock::now();
      do{work();}while(std::chrono::steady_clock::now()-start<std::chrono::seconds(1));
      for(size_t sample=0;sample<15;++sample)output[sample]=work();
    };
    if(reverse_sides){collect(b,standard);collect(a,custom);}else{collect(a,custom);collect(b,standard);}
  }else{
    for(size_t sample=0;sample<15;++sample){
      if((sample+(reverse_sides?1:0))%2){standard[sample]=b();custom[sample]=a();}
      else{custom[sample]=a();standard[sample]=b();}
    }
  }
  for(size_t sample=0;sample<15;++sample)std::printf("node_map_raw,%zu,%d,%zu,%.6f,%.6f\n",size,operation,sample,custom[sample],standard[sample]);
'@
$bench=$bench.Substring(0,$sampleStart)+$samples+"`n"+$bench.Substring($sampleEnd)
$bench+=@'
int main(int argc,char** argv){
 if(argc<2)return 3;std::string_view variant=argv[1];
 for(int i=2;i<argc;++i){std::string_view flag=argv[i];if(flag=="--grouped")grouped=true;else if(flag=="--reverse")reverse_sides=true;else return 3;}
 batch_ns=grouped?100000000.0:30000000.0;
 std::printf("optimization_variant,%.*s\n",int(variant.size()),variant.data());
 using namespace ce;
 if(variant=="baseline")return run<node_hash_map<uint64_t,uint64_t>>();
 if(variant=="pool")return run<node_hash_map<uint64_t,uint64_t,hash<uint64_t>,equal_to<uint64_t>,pool_control_allocator>>();
 if(variant=="index")return run<optimized_node_map<uint64_t,uint64_t,hash<uint64_t>,equal_to<uint64_t>,heap_allocator,false>>();
 if(variant=="combined")return run<optimized_node_map<uint64_t,uint64_t>>();
 return 3;
}
'@
[IO.File]::WriteAllText("$PSScriptRoot/benchmark.cpp","#include `"../../tests/modules/prelude.h`"`nimport Creator.NodeMapOptimized;`n#include `"pool_control.h`"`nstatic bool grouped=false,reverse_sides=false;static double batch_ns=30000000.0;`n"+$bench,[Text.UTF8Encoding]::new($false))
