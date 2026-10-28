#include <ce/string.h>
#include <string>
#include <string_view>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <array>
#include <Windows.h>
static volatile size_t observed;
CE_NOINLINE static size_t consume16(const char16_t* p,size_t n) noexcept { return n?size_t(p[n/2])+n:0; }
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
 std::puts("case,sample,ce_ns,stl_ns"); std::u16string source(2304,u'한');
 std::array<ce::string,4> a; std::array<std::u16string,4> b;
 for(size_t k=0;k<4;++k) { a[k].reserve(2048); b[k].reserve(2048); }
 for(size_t k=0;k<4;++k) {
  auto& custom=a[k]; auto& standard=b[k]; const char16_t* src=source.data()+k*16;
  char name[80]; std::fprintf(stderr,"pair=%zu srcLow12=%zu customLow12=%zu stlLow12=%zu\n",k,reinterpret_cast<uintptr_t>(src)&4095,reinterpret_cast<uintptr_t>(custom.data())&4095,reinterpret_cast<uintptr_t>(standard.data())&4095);
  sprintf_s(name,"container_pair_%zu",k);
  compare(name,[&] { custom.clear(); custom.append({src,2048}); return consume16(custom.data(),custom.size()); },[&] { standard.clear(); standard.append(src,2048); return consume16(standard.data(),standard.size()); });
  char16_t* cp=custom.data(); char16_t* sp=standard.data(); sprintf_s(name,"memmove_pair_%zu",k);
  compare(name,[&] { memmove(cp,src,4096); return consume16(cp,2048); },[&] { memmove(sp,src,4096); return consume16(sp,2048); });
 }
}
