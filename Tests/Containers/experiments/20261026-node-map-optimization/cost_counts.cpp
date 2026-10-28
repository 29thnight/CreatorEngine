#include "../../tests/modules/prelude.h"
import Creator.NodeMapOptimized;
struct counter_allocator {
 static inline size_t allocations=0,frees=0,bytes=0;
 void* allocate(size_t n,size_t a){++allocations;bytes+=n;return ce::heap_allocator{}.allocate(n,a);}
 void deallocate(void* p,size_t n,size_t a)noexcept{++frees;ce::heap_allocator{}.deallocate(p,n,a);}
};
template<class M>void collect(const char* variant,size_t n){
 counter_allocator::allocations=counter_allocator::frees=counter_allocator::bytes=0;
 size_t index_bytes=0;
 {
  M map;map.reserve(n);index_bytes=counter_allocator::bytes;
  for(size_t i=0;i<n;++i)map.try_emplace(i,i+1);
  auto before=counter_allocator::allocations;
  for(size_t i=0;i<n;++i){map.erase(i);map.try_emplace(i,i+1);}
  std::printf("cost,%s,%zu,%zu,%zu,%zu,%zu,%zu\n",variant,n,index_bytes,before,counter_allocator::allocations-before,counter_allocator::frees,counter_allocator::bytes);
 }
 if(counter_allocator::allocations!=counter_allocator::frees)std::abort();
}
int main(){
 std::puts("kind,variant,size,indexRequestedBytes,allocationsAfterBuild,churnNewAllocations,freesDuringChurn,totalRequestedBytes");
 for(size_t n:{size_t{64},size_t{4096},size_t{65536}}){
  collect<ce::node_hash_map<uint64_t,uint64_t,ce::hash<uint64_t>,ce::equal_to<uint64_t>,counter_allocator>>("baseline",n);
  collect<ce::optimized_node_map<uint64_t,uint64_t,ce::hash<uint64_t>,ce::equal_to<uint64_t>,counter_allocator,false>>("index",n);
  collect<ce::optimized_node_map<uint64_t,uint64_t,ce::hash<uint64_t>,ce::equal_to<uint64_t>,counter_allocator,true>>("combined",n);
 }
}
