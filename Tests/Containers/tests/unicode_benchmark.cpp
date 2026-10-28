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
struct standard_cache {
  std::u16string source; std::string bytes; bool initialized=false;
  size_t update(ce::string_view input) {
    if(!initialized || ce::string_view(source.data(),source.size())!=input) {
      size_t count; if(!ce::detail::utf8_length(input,count)) std::abort();
      source.reserve(input.size()); bytes.reserve(count); source.assign(input.data(),input.size());
      if(!ce::to_utf8({source.data(),source.size()},bytes)) std::abort(); initialized=true;
    }
    return consume8(bytes.data(),bytes.size());
  }
};
int main(int argc,char** argv) {
  if(argc>1) reverse_order=std::string_view(argv[1])=="--reverse";
  if(!SetProcessAffinityMask(GetCurrentProcess(),DWORD_PTR(1)<<4) || !SetPriorityClass(GetCurrentProcess(),ABOVE_NORMAL_PRIORITY_CLASS)) return 3;
  std::fprintf(stderr,"MSVC_FULL_VER=%ld UTF16_unit_bytes=%zu grouped=1 samples=15 batch_ms=100 warmup_s=1 cpu=4\n",long(_MSC_FULL_VER),sizeof(char16_t));
  std::puts("case,sample,ce_ns,stl_ns");
  std::u16string source(2048,u'한'); std::string narrow(2048,'x');
  for(size_t n : {size_t(5),size_t(64),size_t(2048)}) {
    ce::string_view v(source.data(),n); std::u16string_view sv(source.data(),n); char name[64];
    sprintf_s(name,"utf16_construct_%zu",n);
    compare(name,[&] { ce::string t(v); return consume16(t.data(),t.size()); },[&] { std::u16string t(sv); return consume16(t.data(),t.size()); });
    ce::string own(v); std::u16string standard(sv);
    std::fprintf(stderr,"utf16 n=%zu sourceLow12=%zu ceLow12=%zu stlLow12=%zu ceCap=%zu stlCap=%zu\n",n,reinterpret_cast<uintptr_t>(v.data())&4095,reinterpret_cast<uintptr_t>(own.data())&4095,reinterpret_cast<uintptr_t>(standard.data())&4095,own.capacity(),standard.capacity());
    sprintf_s(name,"utf16_reuse_append_%zu",n);
    compare(name,[&] { own.clear(); own.append(v); return consume16(own.data(),own.size()); },[&] { standard.clear(); standard.append(sv); return consume16(standard.data(),standard.size()); });
    ce::utf8_string_view nv(narrow.data(),n); std::string_view nsv(narrow.data(),n);
    sprintf_s(name,"utf8_construct_%zu",n);
    compare(name,[&] { ce::utf8_string t(nv); return consume8(t.data(),t.size()); },[&] { std::string t(nsv); return consume8(t.data(),t.size()); });
    ce::utf8_string no(nv); std::string ns(nsv);
    sprintf_s(name,"utf8_reuse_append_%zu",n);
    compare(name,[&] { no.clear(); no.append(nv); return consume8(no.data(),no.size()); },[&] { ns.clear(); ns.append(nsv); return consume8(ns.data(),ns.size()); });
  }
  for(bool ascii : {true,false}) {
    ce::string input(256,ascii?u'x':u'한'); input.append(u"\U0001F600");
    ce::utf8_string encoded; if(!ce::to_utf8(input.view(),encoded)) return 4;
    ce::utf8_string a; std::string b; a.reserve(encoded.size()); b.reserve(encoded.size());
    compare(ascii?"encode_ascii_emoji":"encode_korean_emoji",[&] { if(!ce::to_utf8(input.view(),a)) std::abort(); return consume8(a.data(),a.size()); },[&] { if(!ce::to_utf8(input.view(),b)) std::abort(); return consume8(b.data(),b.size()); });
    ce::string c; std::u16string d; c.reserve(input.size()); d.reserve(input.size());
    compare(ascii?"decode_ascii_emoji":"decode_korean_emoji",[&] { if(!ce::to_utf16(encoded.view(),c)) std::abort(); return consume16(c.data(),c.size()); },[&] { if(!ce::to_utf16(encoded.view(),d)) std::abort(); return consume16(d.data(),d.size()); });
  }
  ce::string input(256,u'한'); ce::utf8_conversion_cache cache; standard_cache baseline;
  if(!cache.update(input.view())) return 4; (void)baseline.update(input.view());
  compare("cache_hit_256",[&] { if(!cache.update(input.view())) std::abort(); return consume8(cache.text().data(),cache.text().size()); },[&] { return baseline.update(input.view()); });
  compare("cache_change_256",[&] { input[0]=input[0]==u'한'?u'글':u'한'; if(!cache.update(input.view())) std::abort(); return consume8(cache.text().data(),cache.text().size()); },[&] { input[0]=input[0]==u'한'?u'글':u'한'; return baseline.update(input.view()); });
  ce::utf16_input_state edit; ce::string result; if(!edit.sync(input.view(),false)) return 4;
  std::string edit8(edit.buffer().data(),edit.buffer().size()); std::u16string committed,output;
  compare("input_commit_256",[&] { if(!edit.commit(result)) std::abort(); return consume16(result.data(),result.size()); },[&] {
    ce::utf8_string_view v(edit8.data(),edit8.size()); size_t n; if(!ce::detail::utf16_length(v,n)) std::abort();
    for(size_t i=0;i<v.size();++i) if(!v[i]) std::abort();
    committed.reserve(n); output.reserve(n); if(!ce::to_utf16(v,output)) std::abort(); committed.assign(output);
    return consume16(output.data(),output.size());
  });
}
