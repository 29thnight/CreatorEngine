#include <ce/string.h>
#include "../diagnostics/unicode_bulk.h"
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
 std::puts("case,sample,alternative_ns,baseline_ns"); std::u16string source(2048,u'한'); std::string narrow(2048,'x');
 ce_bulk::string a(ce_bulk::string_view(source.data(),source.size())); ce::string b(ce::string_view(source.data(),source.size())); std::u16string standard(source);
 compare("utf16_bulk_vs_original_2048",[&] { a.clear(); a.append({source.data(),source.size()}); return consume16(a.data(),a.size()); },[&] { b.clear(); b.append({source.data(),source.size()}); return consume16(b.data(),b.size()); });
 compare("utf16_bulk_vs_stl_2048",[&] { a.clear(); a.append({source.data(),source.size()}); return consume16(a.data(),a.size()); },[&] { standard.clear(); standard.append(source); return consume16(standard.data(),standard.size()); });
 ce_bulk::utf8_string x(ce_bulk::utf8_string_view(narrow.data(),narrow.size())); ce::utf8_string y({narrow.data(),narrow.size()}); std::string z(narrow);
 compare("utf8_bulk_vs_original_2048",[&] { x.clear(); x.append({narrow.data(),narrow.size()}); return consume8(x.data(),x.size()); },[&] { y.clear(); y.append({narrow.data(),narrow.size()}); return consume8(y.data(),y.size()); });
 compare("utf8_bulk_vs_stl_2048",[&] { x.clear(); x.append({narrow.data(),narrow.size()}); return consume8(x.data(),x.size()); },[&] { z.clear(); z.append(narrow); return consume8(z.data(),z.size()); });
}
