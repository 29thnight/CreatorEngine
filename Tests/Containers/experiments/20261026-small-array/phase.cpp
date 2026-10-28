#include "../../tests/benchmark_support.h"
#include <ce/dynamic_array.h>
#include <Psapi.h>
#include <memory>
extern void array_escape(void*);
struct trace_state {uint64_t calls=0,bytes=0;double alloc_ns=0,free_ns=0;};
template<class T> struct traced_allocator {
 using value_type=T;trace_state* state;
 explicit traced_allocator(trace_state& s)noexcept:state(&s){}
 template<class U> traced_allocator(const traced_allocator<U>&o)noexcept:state(o.state){}
 T* allocate(size_t n){auto t=clock_type::now();auto p=std::allocator<T>{}.allocate(n);state->alloc_ns+=std::chrono::duration<double,std::nano>(clock_type::now()-t).count();++state->calls;state->bytes+=n*sizeof(T);return p;}
 void deallocate(T*p,size_t n)noexcept{auto t=clock_type::now();std::allocator<T>{}.deallocate(p,n);state->free_ns+=std::chrono::duration<double,std::nano>(clock_type::now()-t).count();}
 template<class U> bool operator==(const traced_allocator<U>&o)const noexcept{return state==o.state;}
};
static DWORD faults(){PROCESS_MEMORY_COUNTERS p{};check(GetProcessMemoryInfo(GetCurrentProcess(),&p,sizeof(p))!=0);return p.PageFaultCount;}
template<class F> void run(const char*name,F work,trace_state*state=nullptr){
 size_t rounds=1;
 auto batch=[&]{uint64_t sum=0;for(size_t i=0;i<rounds;++i){sum+=work();std::atomic_signal_fence(std::memory_order_seq_cst);}return sum;};
 while(measure(batch)<100000000.0)rounds*=2;
 auto warm=clock_type::now();do{measure(batch);}while(clock_type::now()-warm<std::chrono::seconds(1));
 for(unsigned sample=0;sample<15;++sample){
  if(state)*state={};auto pf=faults();auto ns=measure(batch);auto delta=faults()-pf;
  trace_state s=state?*state:trace_state{};
  std::printf("phase,%s,%u,%zu,%.0f,%.0f,%lu,%.0f,%.0f,%.0f,%.0f,%.0f\n",name,sample,rounds,ns/double(rounds),ns,(unsigned long)delta,double(s.calls)/double(rounds),double(s.bytes)/double(rounds),s.alloc_ns/double(rounds),s.free_ns/double(rounds),double(last_diagnostic.cycles));
 }
}
int main(){
 setup_benchmark();uint64_t keys[64],seed=42,gold=0;
 for(auto&v:keys){seed^=seed<<13;seed^=seed>>7;seed^=seed<<17;v=seed;gold+=v;}gold*=16;
 size_t caps[64]{},count=0;{std::vector<uint64_t>a;for(auto v:keys){auto old=a.capacity();a.push_back(v);if(a.capacity()!=old)caps[count++]=a.capacity();}}
 uint64_t cap_bytes=0;std::printf("phase_capacity");for(size_t i=0;i<count;++i){std::printf(",%zu",caps[i]);cap_bytes+=caps[i]*sizeof(uint64_t);}std::puts("");
 trace_state state;
 auto stl=[&]{uint64_t sum=0;for(unsigned r=0;r<16;++r){std::vector<uint64_t>a;for(auto v:keys)a.push_back(v);for(auto v:a)sum+=v;}return sum;};
 auto reserved=[&]{uint64_t sum=0;for(unsigned r=0;r<16;++r){std::vector<uint64_t>a;a.reserve(64);for(auto v:keys)a.push_back(v);for(auto v:a)sum+=v;}return sum;};
 auto custom=[&]{uint64_t sum=0;for(unsigned r=0;r<16;++r){ce::dynamic_array<uint64_t>a;for(auto v:keys)a.push_back(v);for(auto v:a)sum+=v;}return sum;};
 auto traced=[&]{uint64_t sum=0;for(unsigned r=0;r<16;++r){std::vector<uint64_t,traced_allocator<uint64_t>>a{traced_allocator<uint64_t>{state}};for(auto v:keys)a.push_back(v);for(auto v:a)sum+=v;}return sum;};
 auto allocation=[&]{uint64_t sum=0;for(unsigned r=0;r<16;++r){uint64_t*old=nullptr;size_t old_cap=0;for(size_t i=0;i<count;++i){auto p=std::allocator<uint64_t>{}.allocate(caps[i]);array_escape(p);if(old)std::allocator<uint64_t>{}.deallocate(old,old_cap);old=p;old_cap=caps[i];sum+=caps[i];}std::allocator<uint64_t>{}.deallocate(old,old_cap);}return sum;};
 auto fill=[&]{uint64_t sum=0;for(unsigned r=0;r<16;++r){uint64_t a[64];for(size_t i=0;i<64;++i)a[i]=keys[i];array_escape(a);for(auto v:a)sum+=v;}return sum;};
 check(stl()==gold&&reserved()==gold&&custom()==gold&&fill()==gold&&traced()==gold);
 check(state.calls==count*16&&state.bytes==cap_bytes*16);
 run("stl_growth",stl);run("stl_reserved",reserved);run("custom_growth",custom);run("alloc_free_sequence",allocation);run("fill_sum_existing",fill);run("stl_traced",traced,&state);
}
