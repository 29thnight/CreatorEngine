#include "../../tests/modules/prelude.h"
import Creator.NodeMapOptimized;
#include "pool_control.h"
static void check(bool value){if(!value)throw std::runtime_error("optimized map advanced regression");}
struct ledger{int budget=-1;size_t live=0,allocations=0,frees=0,bytes=0;};
struct tracked {
 ledger* state;
 void* allocate(size_t n,size_t a){if(state->budget==0)throw std::bad_alloc{};if(state->budget>0)--state->budget;auto p=ce::heap_allocator{}.allocate(n,a);++state->live;++state->allocations;state->bytes+=n;return p;}
 void deallocate(void* p,size_t n,size_t a)noexcept{--state->live;++state->frees;ce::heap_allocator{}.deallocate(p,n,a);}
};
struct alignas(128) value {
 static inline int live=0;static inline bool fail=false;int n;
 explicit value(int x):n(x){if(fail)throw 9;++live;}
 value(const value& other):value(other.n){}
 ~value()noexcept{--live;}
};
struct throwing_hash {static inline int budget=-1;uint64_t operator()(int x)const{if(budget==0)throw 11;if(budget>0)--budget;return ce::hash<int>{}(x);}};
template<class M>static void duplicate_check(){M m;m.try_emplace(1,10);m.try_emplace(2,20);auto* p=&m.at(2);m.erase(1);auto result=m.try_emplace(2,999);check(!result.second && m.at(2)==20);m.reserve(1024);check(p==&m.at(2));}
int main(){
 using M=ce::optimized_node_map<int,value,ce::hash<int>,ce::equal_to<int>,tracked>;
 ledger state;
 for(int budget=0;budget<2;++budget){M m(0,{}, {},tracked{&state});state.budget=budget;bool failed=false;try{m.try_emplace(1,1);}catch(const std::bad_alloc&){failed=true;}state.budget=-1;check(failed && m.empty() && m.bucket_count()==0 && state.live==0);}
 {
  M m(0,{}, {},tracked{&state});value::fail=true;bool failed=false;try{m.try_emplace(1,1);}catch(int){failed=true;}value::fail=false;
  check(failed && m.empty() && m.bucket_count()==0 && state.live==0 && value::live==0);
  m.try_emplace(1,1);m.try_emplace(2,2);auto* stable=&m.at(2);check(uintptr_t(stable)%128==0);m.erase(1);
  const auto allocated=state.live,buckets=m.bucket_count();value::fail=true;failed=false;try{m.try_emplace(3,3);}catch(int){failed=true;}value::fail=false;
  check(failed && m.size()==1 && m.bucket_count()==buckets && state.live==allocated && &m.at(2)==stable && value::live==1);
  m.try_emplace(3,3);check(state.live==allocated && m.at(3).n==3);
  for(int i=4;i<200;++i)m.try_emplace(i,i);check(stable==&m.at(2));
  auto copy=m;check(copy.size()==m.size() && copy.at(2).n==2);auto moved=std::move(copy);check(copy.empty() && moved.size()==m.size());
  m.clear();check(m.empty());m.try_emplace(100,100);check(m.at(100).n==100);
 }
 check(state.live==0 && value::live==0);
 {
  ce::optimized_node_map<int,int,throwing_hash,ce::equal_to<int>,tracked> m(0,{}, {},tracked{&state});for(int i=0;i<64;++i)m.try_emplace(i,i);
  auto* stable=&m.at(3);auto buckets=m.bucket_count(),live=state.live;throwing_hash::budget=2;bool failed=false;
  try{m.rehash(1024);}catch(int){failed=true;}throwing_hash::budget=-1;
  check(failed && m.size()==64 && m.bucket_count()==buckets && state.live==live && &m.at(3)==stable);for(int i=0;i<64;++i)check(m.at(i)==i);
 }
 check(state.live==0);
 {
  ce::optimized_node_map<int,int,ce::hash<int>,ce::equal_to<int>,tracked> m(0,{}, {},tracked{&state});m.reserve(128);
  for(int i=0;i<64;++i)m.try_emplace(i,i);const auto live=state.live,buckets=m.bucket_count();state.budget=0;bool failed=false;
  try{m.try_emplace(64,64);}catch(const std::bad_alloc&){failed=true;}state.budget=-1;
  check(failed && m.size()==64 && m.bucket_count()==buckets && state.live==live);m.try_emplace(64,64);check(m.at(64)==64);
 }
 check(state.live==0);
 {ce::optimized_node_map<int,int> m;m.max_load_factor(0.001f);m.try_emplace(1,1);check(m.load_factor()<=m.max_load_factor());}
 duplicate_check<ce::optimized_node_map<uint64_t,uint64_t>>();
 duplicate_check<ce::node_hash_map<uint64_t,uint64_t,ce::hash<uint64_t>,ce::equal_to<uint64_t>,pool_control_allocator>>();
 std::puts("NODE_MAP_ADVANCED_PASS");
}
