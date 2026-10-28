#include <ce/string.h>
#include "../diagnostics/unicode_append.h"
#include "../diagnostics/unicode_direct.h"
#include "../diagnostics/unicode_fixed.h"
#include <string>
#include <string_view>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <array>
#include <Windows.h>
static volatile size_t observed;
CE_NOINLINE static size_t consume16(const char16_t* p,size_t n) noexcept { return n?size_t(p[n/2])+n:0; }
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
 std::puts("case,sample,alternative_ns,baseline_ns");
 std::u16string source(64,u'한'); std::string narrow(64,'x');
 for(size_t n : {size_t(5),size_t(64)}) {
  ce::string_view v(source.data(),n); char name[80];
  sprintf_s(name,"utf16_direct_constructor_%zu",n);
  compare(name,[&] { ce_direct::string t(ce_direct::string_view(source.data(),n)); return consume16(t.data(),t.size()); },[&] { ce::string t(v); return consume16(t.data(),t.size()); });
  sprintf_s(name,"utf16_fixed_constructor_%zu",n);
  compare(name,[&] { ce_fixed::string t(ce_fixed::string_view(source.data(),n)); return consume16(t.data(),t.size()); },[&] { ce::string t(v); return consume16(t.data(),t.size()); });
  ce_append::string a(ce_append::string_view(source.data(),n)); ce::string b(v);
  sprintf_s(name,"utf16_fit_path_%zu",n);
  compare(name,[&] { a.clear(); a.append({source.data(),n}); return consume16(a.data(),a.size()); },[&] { b.clear(); b.append(v); return consume16(b.data(),b.size()); });
  ce_append::utf8_string c({narrow.data(),n}); ce::utf8_string d({narrow.data(),n});
  sprintf_s(name,"utf8_fit_path_%zu",n);
  compare(name,[&] { c.clear(); c.append({narrow.data(),n}); return consume8(c.data(),c.size()); },[&] { d.clear(); d.append({narrow.data(),n}); return consume8(d.data(),d.size()); });
 }
}
