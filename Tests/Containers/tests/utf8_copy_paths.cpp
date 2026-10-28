#include <ce/string.h>
#include <intrin.h>
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
CE_NOINLINE static void raw_copy(char* d,const char* s,size_t n) { memcpy(d,s,n); }
CE_NOINLINE static void raw_rep(char* d,const char* s,size_t n) { __movsb(reinterpret_cast<unsigned char*>(d),reinterpret_cast<const unsigned char*>(s),n); }
CE_NOINLINE static void reverse_blocks(char* d,const char* s,size_t n) {
 while(n>=256) {n-=256; memcpy(d+n,s+n,256);} if(n) memcpy(d,s,n);
}
int main() {
 if(!SetProcessAffinityMask(GetCurrentProcess(),DWORD_PTR(1)<<4) || !SetPriorityClass(GetCurrentProcess(),ABOVE_NORMAL_PRIORITY_CLASS)) return 3;
 std::puts("case,sample,alternative_ns,memmove_ns"); std::string source(2048,'x'); std::array<std::string,4> buffers;
 for(auto& v:buffers) v.resize(2048);
 for(size_t i=0;i<4;++i) {
  char* d=buffers[i].data();const char* s=source.data();char label[64];
  std::fprintf(stderr,"buffer=%zu srcLow12=%zu dstLow12=%zu\n",i,reinterpret_cast<uintptr_t>(s)&4095,reinterpret_cast<uintptr_t>(d)&4095);
  sprintf_s(label,"memcpy_%zu",i);compare(label,[&] {raw_copy(d,s,2048);return consume8(d,2048);},[&] {raw_move(d,s,2048);return consume8(d,2048);});
  sprintf_s(label,"rep_%zu",i);compare(label,[&] {raw_rep(d,s,2048);return consume8(d,2048);},[&] {raw_move(d,s,2048);return consume8(d,2048);});
  sprintf_s(label,"reverse_%zu",i);compare(label,[&] {reverse_blocks(d,s,2048);return consume8(d,2048);},[&] {raw_move(d,s,2048);return consume8(d,2048);});
 }
}
