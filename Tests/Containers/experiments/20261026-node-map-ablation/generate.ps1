$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
. "$root/implementation-root.ps1"
$implementation=Get-ContainerImplementationRoot
$path=Join-Path $implementation 'modules/node_hash_map.ixx'
$text=[IO.File]::ReadAllText($path).Replace("`r`n","`n")
$text=$text.Replace('export module Creator.Containers:node_hash_map;','export module Creator.NodeMapProbe;').Replace('export import :dynamic_array;','export import Creator.Containers;').Replace('export import :hash;','')
$text=$text.Replace('namespace ce {','namespace ce::experiment {').Replace('node_hash_map','measured_node_map')
$text=$text.Replace('class Allocator=heap_allocator>','class Allocator=heap_allocator,bool ReuseProbe=false,bool DirectKeyErase=false,bool CountWork=false>')
$anchor='  void release(value_type* node)noexcept {'
if(!$text.Contains($anchor)){throw 'Missing release anchor'}
$probe=@'
  struct insertion_probe {size_t existing;size_t position;};
  template<class Q> insertion_probe locate_insertion(const Q& key,uint64_t code)const {
    if(slots_.empty())return {absent,absent};
    const auto mask=slots_.size()-1;auto index=size_t(code)&mask;size_t first_free=absent;
    for(size_t inspected=0;inspected<slots_.size();++inspected,index=(index+1)&mask){
      const auto& bucket=slots_[index];
      if(!bucket.node){
        if(first_free==absent)first_free=index;
        if(!bucket.hash)return {absent,first_free};
      }else if(bucket.hash==code && equal_(bucket.node->first,key))return {index,absent};
    }
    return {absent,first_free};
  }
'@
$text=$text.Replace($anchor,$probe+"`n"+$anchor)
$old='    const auto code=hash_(key);auto existing=locate(key,code);'
if(!$text.Contains($old)){throw 'Missing insertion anchor'}
$text=$text.Replace($old,@'
    const auto code=hash_(key);size_t existing,position=absent;
    if constexpr(ReuseProbe){const auto probe=locate_insertion(key,code);existing=probe.existing;position=probe.position;}
    else existing=locate(key,code);
'@)
$old='    const auto index=insert_index(code);auto& bucket=slots_[index];if(bucket.hash && !bucket.node)--erased_;'
if(!$text.Contains($old)){throw 'Missing commit anchor'}
$text=$text.Replace($old,'    const auto index=(ReuseProbe && !capacity)?position:insert_index(code);auto& bucket=slots_[index];if(bucket.hash && !bucket.node)--erased_;')
$old='  template<class Q> size_t erase(const Q& key){auto found=find(key);if(found==end())return 0;erase(found);return 1;}'
if(!$text.Contains($old)){throw 'Missing key erase anchor'}
$text=$text.Replace($old,@'
  template<class Q> size_t erase(const Q& key){
    auto found=find(key);if(found==end())return 0;
    if constexpr(DirectKeyErase){auto& bucket=slots_[found.index_];auto* node=bucket.node;bucket={nullptr,1};--size_;++erased_;release(node);}
    else erase(found);
    return 1;
  }
'@)
$text=$text.Replace('namespace ce::experiment {',@'
namespace ce::experiment {
struct probe_statistics {uint64_t locate_slots=0,insert_slots=0,iterator_checks=0,node_allocations=0,node_frees=0,index_rebuilds=0;};
'@)
$text=$text.Replace('  using key_type=Key;','  static inline probe_statistics stats{};'+"`n"+'  using key_type=Key;')
$text=$text.Replace('      const auto& bucket=slots_[index];','      if constexpr(CountWork)++stats.locate_slots;'+"`n"+'      const auto& bucket=slots_[index];')
$text=$text.Replace('    while(slots_[index].node)index=(index+1)&mask;',@'
    for(;;){if constexpr(CountWork)++stats.insert_slots;if(!slots_[index].node)break;index=(index+1)&mask;}
'@)
$text=$text.Replace('    void skip(){while(owner_ && index_<owner_->slots_.size() && !owner_->slots_[index_].node)++index_;}',@'
    void skip(){while(owner_ && index_<owner_->slots_.size()){if constexpr(CountWork)++stats.iterator_checks;if(owner_->slots_[index_].node)break;++index_;}}
'@)
$text=$text.Replace('    auto* memory=alloc_.allocate(',"    if constexpr(CountWork)++stats.node_allocations;`n"+'    auto* memory=alloc_.allocate(')
$text=$text.Replace('    node->~value_type();',"    if constexpr(CountWork)++stats.node_frees;`n"+'    node->~value_type();')
$text=$text.Replace('    dynamic_array<slot,Allocator> fresh(alloc_);fresh.resize(capacity,slot{});',"    if constexpr(CountWork)++stats.index_rebuilds;`n"+'    dynamic_array<slot,Allocator> fresh(alloc_);fresh.resize(capacity,slot{});')
[IO.File]::WriteAllText("$PSScriptRoot/Creator.NodeMapProbe.ixx",$text,[Text.UTF8Encoding]::new($false))
$test=[IO.File]::ReadAllText("$root/tests/node_map_migration.cpp")
$test=[regex]::Replace($test,'#include <ce/[^>]+>','// Named module supplies API')
$test=$test.Replace('ce::node_hash_map','probe_map')
$test=$test.Replace('int main(){',@'
int main(){
    {
        map_type tomb;tomb.try_emplace(1,10);tomb.try_emplace(2,20);tomb.try_emplace(3,30);
        auto* stable=&tomb.at(3);tomb.erase(1);
        auto duplicate=tomb.try_emplace(3,999);
        check(!duplicate.second && tomb.size()==2 && tomb.at(3)==30);
        tomb.try_emplace(4,40);check(tomb.at(4)==40 && stable==&tomb.at(3));
    }
'@)
$prefix=@'
#include "../../tests/modules/prelude.h"
import Creator.NodeMapProbe;
template<class K,class V,class H=ce::hash<K>,class E=ce::equal_to<K>,class A=ce::heap_allocator>
using probe_map=ce::experiment::measured_node_map<K,V,H,E,A,PROBE_REUSE,PROBE_ERASE>;
'@
[IO.File]::WriteAllText("$PSScriptRoot/correctness.cpp",$prefix+"`n"+$test,[Text.UTF8Encoding]::new($false))
$bench=[IO.File]::ReadAllText("$root/tests/node_map_benchmark.cpp")
$bench=[regex]::Replace($bench,'#include <ce/[^>]+>','// Named module supplies API')
$start=$bench.IndexOf('int main(int argc,char** argv){')
if($start -lt 0){throw 'Missing benchmark entry'}
$bench=$bench.Substring(0,$start).Replace('if(sample%2)','if((sample+(reverse_sides?1:0))%2)')+@'
int main(int argc,char** argv){
 if(argc<2 || argc>3)return 3;
 reverse_sides=argc==3 && std::string_view(argv[2])=="--reverse";
 const std::string_view variant=argv[1];
 std::printf("ablation_variant,%.*s\n",int(variant.size()),variant.data());
 using namespace ce;
 if(variant=="canonical")return run<node_hash_map<uint64_t,uint64_t>>();
 if(variant=="baseline")return run<experiment::measured_node_map<uint64_t,uint64_t>>();
 if(variant=="reuse")return run<experiment::measured_node_map<uint64_t,uint64_t,hash<uint64_t>,equal_to<uint64_t>,heap_allocator,true,false>>();
 if(variant=="erase")return run<experiment::measured_node_map<uint64_t,uint64_t,hash<uint64_t>,equal_to<uint64_t>,heap_allocator,false,true>>();
 if(variant=="both")return run<experiment::measured_node_map<uint64_t,uint64_t,hash<uint64_t>,equal_to<uint64_t>,heap_allocator,true,true>>();
 return 3;
}
'@
[IO.File]::WriteAllText("$PSScriptRoot/benchmark.cpp","#include `"../../tests/modules/prelude.h`"`nimport Creator.NodeMapProbe;`nstatic bool reverse_sides=false;`n"+$bench,[Text.UTF8Encoding]::new($false))
Get-FileHash $path,"$PSScriptRoot/Creator.NodeMapProbe.ixx","$PSScriptRoot/benchmark.cpp","$PSScriptRoot/correctness.cpp",$PSCommandPath|ConvertTo-Json|Set-Content "$PSScriptRoot/generated-inputs.json" -Encoding utf8
