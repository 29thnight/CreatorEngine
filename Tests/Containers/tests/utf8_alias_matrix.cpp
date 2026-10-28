#include <ce/string.h>
#include <intrin.h>
#include "../diagnostics/utf8_direction_avx2.h"
#include <string>
#include <string_view>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <array>
#include <Windows.h>
static volatile size_t observed;
CE_NOINLINE static size_t consume8(const char* p,size_t n) noexcept { return n?size_t(static_cast<unsigned char>(p[n/2]))+n:0; }
static bool reverse_order=false;
template<class F> static double measure(F work) {
  using clock=std::chrono::steady_clock; size_t count=0,total=0; auto start=clock::now();
  do { for(int k=0;k<256;++k) total+=work(); count+=256; } while(clock::now()-start<std::chrono::milliseconds(100));
  observed=total; return std::chrono::duration<double,std::nano>(clock::now()-start).count()/double(count);
}
template<class A,class B> static void compare(const char* name,A custom,B standard) {
  std::array<double,15> a{},b{};
  auto collect=[](auto work,auto& samples) {
    HEAP_OPTIMIZE_RESOURCES_INFORMATION info{HEAP_OPTIMIZE_RESOURCES_CURRENT_VERSION,0};
    if(!HeapSetInformation(GetProcessHeap(),HeapOptimizeResources,&info,sizeof(info))) std::abort();
    auto start=std::chrono::steady_clock::now(); do { measure(work); } while(std::chrono::steady_clock::now()-start<std::chrono::seconds(1));
    for(auto& sample:samples) sample=measure(work);
  };
  if(reverse_order) { collect(standard,b); collect(custom,a); } else { collect(custom,a); collect(standard,b); }
  for(size_t i=0;i<a.size();++i) std::printf("%s,%zu,%.9f,%.9f\n",name,i,a[i],b[i]);
  std::fflush(stdout);
}


CE_NOINLINE static void raw_move(char* d,const char* s,size_t n) { memmove(d,s,n); }

int main() {
 if(!SetProcessAffinityMask(GetCurrentProcess(),DWORD_PTR(1)<<4) || !SetPriorityClass(GetCurrentProcess(),ABOVE_NORMAL_PRIORITY_CLASS)) return 3;
 std::puts("case,sample,alternative_ns,memmove_ns");
 auto a=static_cast<char*>(VirtualAlloc(nullptr,16384,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
 auto b=static_cast<char*>(VirtualAlloc(nullptr,16384,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));if(!a||!b)return 4;
 memset(a,'x',16384);memset(b,0,16384);auto s=a+512;
 for(size_t delta : {size_t(0),size_t(16),size_t(48),size_t(80),size_t(240),size_t(256),size_t(512),size_t(2048),size_t(4048)}) {
  auto d=b+512+delta;char name[64];
  sprintf_s(name,"back_delta_%zu",delta);compare(name,[&] {copy_backwards_avx2(d,s,2048);return consume8(d,2048);},[&] {raw_move(d,s,2048);return consume8(d,2048);});
  sprintf_s(name,"forward_delta_%zu",delta);compare(name,[&] {copy_forwards_avx2(d,s,2048);return consume8(d,2048);},[&] {raw_move(d,s,2048);return consume8(d,2048);});
 }
 VirtualFree(a,0,MEM_RELEASE);VirtualFree(b,0,MEM_RELEASE);
}
