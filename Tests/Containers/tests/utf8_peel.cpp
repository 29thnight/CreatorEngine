#include <ce/string.h>
#include "../diagnostics/utf8_peel.h"
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


int main() {
 if(!SetProcessAffinityMask(GetCurrentProcess(),DWORD_PTR(1)<<4) || !SetPriorityClass(GetCurrentProcess(),ABOVE_NORMAL_PRIORITY_CLASS)) return 3;
 std::puts("case,sample,peel_ns,original_ns");std::string source(2048,'x');std::array<ce_peel::utf8_string,4> buffers;
 for(auto& v:buffers) v.reserve(2048);
 for(size_t i=0;i<4;++i) {
  auto& v=buffers[i];ce_peel::utf8_string_view input(source.data(),2048);char name[64];
  std::fprintf(stderr,"buffer=%zu srcLow12=%zu dstLow12=%zu\n",i,reinterpret_cast<uintptr_t>(source.data())&4095,reinterpret_cast<uintptr_t>(v.data())&4095);
  sprintf_s(name,"first_%zu",i);compare(name,[&] {v.clear();v.append_first(input);return consume8(v.data(),v.size());},[&] {v.clear();v.append(input);return consume8(v.data(),v.size());});
  sprintf_s(name,"last_%zu",i);compare(name,[&] {v.clear();v.append_last(input);return consume8(v.data(),v.size());},[&] {v.clear();v.append(input);return consume8(v.data(),v.size());});
 }
}
