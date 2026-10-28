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

struct prefix32_allocator {
 void* allocate(size_t n,size_t a) {
  if(n<1024)return ce::heap_allocator{}.allocate(n,a);
  if(n>SIZE_MAX-32)throw std::bad_alloc();
  void* raw=ce::heap_allocator{}.allocate(n+32,alignof(void*));
  auto q=(reinterpret_cast<uintptr_t>(raw)+sizeof(void*)+31)&~uintptr_t(31);
  void* p=reinterpret_cast<void*>(q);memcpy(static_cast<char*>(p)-sizeof(void*),&raw,sizeof raw);return p;
 }
 void deallocate(void* p,size_t n,size_t a) noexcept {
  if(n<1024){ce::heap_allocator{}.deallocate(p,n,a);return;}
  void* raw;memcpy(&raw,static_cast<char*>(p)-sizeof(void*),sizeof raw);ce::heap_allocator{}.deallocate(raw,n+32,alignof(void*));
 }
};
int main() {
 if(!SetProcessAffinityMask(GetCurrentProcess(),DWORD_PTR(1)<<4) || !SetPriorityClass(GetCurrentProcess(),ABOVE_NORMAL_PRIORITY_CLASS)) return 3;
 std::puts("case,sample,aligned_ns,reference_ns");std::string source(2048,'x');
 std::array<ce::utf8_string,4> old;
 std::array<ce::basic_utf8_string<prefix32_allocator>,4> aligned;
 std::array<std::string,4> stl;
 for(size_t i=0;i<4;++i){old[i].reserve(2048);aligned[i].reserve(2048);stl[i].reserve(2048);}
 ce::utf8_string_view input(source.data(),2048);
 for(size_t i=0;i<4;++i){char name[64];auto& a=aligned[i];auto& b=old[i];auto& c=stl[i];
  std::fprintf(stderr,"buffer=%zu srcLow12=%zu oldLow12=%zu alignedLow12=%zu stlLow12=%zu\n",i,reinterpret_cast<uintptr_t>(source.data())&4095,reinterpret_cast<uintptr_t>(b.data())&4095,reinterpret_cast<uintptr_t>(a.data())&4095,reinterpret_cast<uintptr_t>(c.data())&4095);
  sprintf_s(name,"aligned_vs_old_%zu",i);compare(name,[&]{a.clear();a.append(input);return consume8(a.data(),a.size());},[&]{b.clear();b.append(input);return consume8(b.data(),b.size());});
  sprintf_s(name,"aligned_vs_stl_%zu",i);compare(name,[&]{a.clear();a.append(input);return consume8(a.data(),a.size());},[&]{c.clear();c.append(source);return consume8(c.data(),c.size());});
 }
 for(size_t n:{size_t(64),size_t(2048)}){ce::utf8_string_view v(source.data(),n);std::string_view sv(source.data(),n);char name[64];sprintf_s(name,"aligned_ctor_stl_%zu",n);
  compare(name,[&]{ce::basic_utf8_string<prefix32_allocator> a(v);return consume8(a.data(),a.size());},[&]{std::string a(sv);return consume8(a.data(),a.size());});
 }
}
