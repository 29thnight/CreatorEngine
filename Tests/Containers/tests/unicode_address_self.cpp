#include <ce/string.h>
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
 std::puts("case,sample,first_ns,second_ns"); std::string source(2304,'x'); std::array<std::string,8> buffers;
 for(auto& v:buffers) v.reserve(2048);
 for(size_t pair=0;pair<4;++pair) {
  auto& a=buffers[pair*2]; auto& b=buffers[pair*2+1]; const char* src=source.data()+pair*16;
  char name[80]; std::fprintf(stderr,"pair=%zu srcLow12=%zu firstLow12=%zu secondLow12=%zu\n",pair,reinterpret_cast<uintptr_t>(src)&4095,reinterpret_cast<uintptr_t>(a.data())&4095,reinterpret_cast<uintptr_t>(b.data())&4095);
  sprintf_s(name,"same_stl_append_pair_%zu",pair);
  compare(name,[&] { a.clear(); a.append(src,2048); return consume8(a.data(),a.size()); },[&] { b.clear(); b.append(src,2048); return consume8(b.data(),b.size()); });
  char* ap=a.data(); char* bp=b.data(); sprintf_s(name,"same_memmove_pair_%zu",pair);
  compare(name,[&] { memmove(ap,src,2048); return consume8(ap,2048); },[&] { memmove(bp,src,2048); return consume8(bp,2048); });
 }
}
