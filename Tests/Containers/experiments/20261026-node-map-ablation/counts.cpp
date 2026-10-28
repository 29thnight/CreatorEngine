#include "../../tests/modules/prelude.h"
import Creator.NodeMapProbe;
template<bool Reuse,bool Erase>void collect(const char* variant,size_t count){
 using M=ce::experiment::measured_node_map<uint64_t,uint64_t,ce::hash<uint64_t>,ce::equal_to<uint64_t>,ce::heap_allocator,Reuse,Erase,true>;
 std::vector<uint64_t> keys(count);uint64_t random=7919;
 for(auto& key:keys){random^=random<<13;random^=random>>7;random^=random<<17;key=random&~uint64_t{1};}
 M map;map.reserve(count);M::stats={};
 auto print=[&](int operation){auto s=M::stats;std::printf("counts,%s,%zu,%d,%llu,%llu,%llu,%llu,%llu,%llu\n",variant,count,operation,
  (unsigned long long)s.locate_slots,(unsigned long long)s.insert_slots,(unsigned long long)s.iterator_checks,
  (unsigned long long)s.node_allocations,(unsigned long long)s.node_frees,(unsigned long long)s.index_rebuilds);M::stats={};};
 for(auto key:keys)map.try_emplace(key,key+7);print(0);
 uint64_t result=0;
 for(auto key:keys){auto found=map.find(key);if(found!=map.end())result+=found->second;}print(1);
 for(auto key:keys)result+=map.find(key|1)!=map.end();print(2);
 for(auto key:keys){result+=map.erase(key);map.try_emplace(key,key+7);}print(3);
 if(result==0)std::abort();
}
int main(){
 std::puts("kind,variant,size,operation,locateSlots,insertSlots,iteratorChecks,nodeAllocations,nodeFrees,indexRebuilds");
 for(size_t n:{size_t{64},size_t{4096},size_t{65536}}){collect<false,false>("baseline",n);collect<true,false>("reuse",n);collect<false,true>("erase",n);collect<true,true>("both",n);}
}
