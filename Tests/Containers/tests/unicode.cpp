#include <ce/string.h>
#include <ce/node_hash_map.h>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <algorithm>
#include <vector>
#include <cstdint>
#include <cstring>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif
static void check(bool b,const char* why) { if(!b) { std::fprintf(stderr,"FAIL: %s\n",why); std::exit(1); } }
struct allocation_state { size_t calls=0; bool fail=false; };
struct measured_allocator {
  allocation_state* state;
  void* allocate(size_t n,size_t alignment) { ++state->calls; if(state->fail) throw std::bad_alloc(); return ce::heap_allocator{}.allocate(n,alignment); }
  void deallocate(void* p,size_t n,size_t alignment) noexcept { ce::heap_allocator{}.deallocate(p,n,alignment); }
};
static void correctness() {
  static_assert(sizeof(ce::string::value_type)==2);
  ce::string s(u"한글 \U0001F600"), original(s);
  ce::utf8_string bytes;
  check(bool(ce::to_utf8(s.view(),bytes)),"encode");
  ce::string decoded;
  check(bool(ce::to_utf16(bytes.view(),decoded)) && decoded==s,"Korean supplementary roundtrip");
  check(s.size()==5,"code units");
  check(ce::string_view(u"\u0100").compare(u"\u00ff")>0,"numeric UTF16 compare");
  for(uint32_t cp=0;cp<=0x10ffff;++cp) {
    if(cp>=0xd800 && cp<=0xdfff) continue;
    char16_t units[2]; size_t n=1;
    if(cp<=0xffff) units[0]=static_cast<char16_t>(cp);
    else { uint32_t x=cp-0x10000; units[0]=static_cast<char16_t>(0xd800+(x>>10)); units[1]=static_cast<char16_t>(0xdc00+(x&1023)); n=2; }
    check(bool(ce::to_utf8({units,n},bytes)),"scalar encode");
    check(bool(ce::to_utf16(bytes.view(),decoded)) && decoded.view()==ce::string_view(units,n),"all scalar roundtrip");
  }
  const char16_t bad16[][3]={{0xd800,0,0},{0xdc00,0,0},{0xd800,u'a',0},{0xdbff,0xd800,0}};
  bytes.assign("unchanged");
  for(auto& bad:bad16) { auto r=ce::to_utf8(ce::string_view(bad),bytes); check(!r && bytes.view()==ce::utf8_string_view("unchanged"),"invalid UTF16 transaction"); }
  const char* bad8[]={"\xc0\x80","\xe0\x80\x80","\xed\xa0\x80","\xf4\x90\x80\x80","\xf5\x80\x80\x80","\xe2\x82","\x80","\xc2x"};
  decoded.assign(u"보존");
  for(auto bad:bad8) { auto r=ce::to_utf16(ce::utf8_string_view(bad),decoded); check(!r && decoded.view()==ce::string_view(u"보존"),"invalid UTF8 transaction"); }
  ce::string mutation; std::u16string reference; uint32_t seed=12345;
  for(size_t k=0;k<20000;++k) {
    seed=seed*1664525+1013904223; char16_t c=static_cast<char16_t>(seed>>16);
    switch(seed%5) {
    case 0: mutation.push_back(c); reference.push_back(c); break;
    case 1: { size_t n=seed%128; mutation.resize(n,c); reference.resize(n,c); break; }
    case 2: if(!reference.empty()) { size_t i=seed%reference.size(); mutation.erase(i,3); reference.erase(i,3); } break;
    case 3: if(reference.size()<256) { mutation.append(mutation.view()); reference+=std::u16string(reference); } break;
    case 4: if(!reference.empty()) { size_t i=seed%reference.size(); mutation.assign(mutation.view().substr(i)); reference=reference.substr(i); } break;
    }
    check(mutation.size()==reference.size() && (!reference.size() || std::memcmp(mutation.data(),reference.data(),reference.size()*2)==0),"mutation differential");
    check(mutation.c_str()[mutation.size()]==0,"terminator");
    ce::string copy(mutation); ce::string moved(static_cast<ce::string&&>(copy)); check(moved==mutation && copy.empty(),"copy move");
  }
  ce::utf8_conversion_cache cache;
  check(bool(cache.update(original.view())),"cache update"); auto stable=cache.c_str();
  for(int i=0;i<10000;++i) check(bool(cache.update(original.view())) && stable==cache.c_str(),"cache hit stable storage");
  ce::utf16_input_state input;
  check(bool(input.sync(original.view(),false)),"input sync");
  ce::utf8_string edit; check(bool(ce::to_utf8(u"한글 입력 \U0001F680",edit)),"edit encode");
  input.buffer().assign(edit.view()); check(bool(input.commit(s)) && s.view()==ce::string_view(u"한글 입력 \U0001F680"),"edit commit");
  check(bool(input.sync(u"외부 변경",true)) && input.conflict,"active external edit conflict");
  auto before=s; check(bool(input.commit(s)) && s==before,"conflict preserves output");
  check(bool(input.sync(s.view(),false)) && !input.conflict,"external revert clears conflict");
  check(bool(input.sync(u"외부 변경",false)) && !input.conflict,"conflict resync");
  char16_t nul[]={u'a',0,u'b'}; check(!input.sync({nul,3},false),"InputText embedded NUL reject");
  ce::string alias16(u"ab"); alias16.reserve(32); alias16.append({alias16.data(),alias16.size()+1});
  check(alias16.size()==5 && alias16[2]==u'a' && alias16[4]==0 && alias16.c_str()[5]==0,"overlapping append includes terminator UTF16");
  ce::utf8_string alias8("ab"); alias8.reserve(32); alias8.append({alias8.data(),alias8.size()+1});
  check(alias8.size()==5 && alias8[2]=='a' && alias8[4]==0 && alias8.c_str()[5]==0,"overlapping append includes terminator UTF8");
  ce::node_hash_map<ce::string,int> names;
  names.try_emplace(ce::string(u"\uD55C\uAE00"),7);
  auto found=names.find(ce::string_view(u"\uD55C\uAE00"));
  check(found!=names.end() && found->second==7 && names.find(ce::string_view(u"missing"))==names.end(),"UTF16 heterogeneous lookup without key construction");
  allocation_state allocation;
  measured_allocator allocator{&allocation};
  ce::basic_utf8_conversion_cache<measured_allocator> measuredCache(allocator);
  ce::string longText(1024,u'한');
  check(bool(measuredCache.update(longText.view())),"cache initial");
  allocation.calls=0;
  for(int i=0;i<10000;++i) check(bool(measuredCache.update(longText.view())),"cache repeated");
  check(allocation.calls==0,"cache hit zero allocations");
  longText[0]=u'글'; allocation.calls=0;
  check(bool(measuredCache.update(longText.view())) && allocation.calls==0,"cache same capacity change zero allocations");
  ce::basic_utf8_string<measured_allocator> failBytes(ce::utf8_string_view("preserved"),allocator);
  allocation.fail=true; bool threw=false;
  try { (void)ce::to_utf8(longText.view(),failBytes); } catch(const std::bad_alloc&) { threw=true; }
  check(threw && failBytes.view()==ce::utf8_string_view("preserved"),"allocation failure transaction");
  allocation.fail=false;
  ce::string overlap(u"한글");
  ce::utf8_string encoded; check(bool(ce::to_utf8(overlap.view(),encoded)),"overlap setup");
  overlap.reserve(128); std::memcpy(overlap.data(),encoded.data(),encoded.size());
  check(bool(ce::to_utf16({reinterpret_cast<const char*>(overlap.data()),encoded.size()},overlap)) && overlap.view()==ce::string_view(u"한글"),"overlapping decode");
  ce::utf8_string alias; alias.reserve(128); char16_t units[]={u'한',u'글'}; std::memcpy(alias.data(),units,sizeof(units));
  check(bool(ce::to_utf8({reinterpret_cast<const char16_t*>(alias.data()),2},alias)) && alias.view()==encoded.view(),"overlapping encode");
  std::puts("UNICODE_CORRECTNESS_PASS scalars=1112064 mutations=20000 surrogate=strict input=transactional");
}
static volatile size_t sink;
#if defined(_MSC_VER)
__declspec(noinline)
#else
__attribute__((noinline))
#endif
static size_t consume(const char16_t* p,size_t n) noexcept { return n?size_t(p[n/2])+n:0; }
template<class F> static double measure(F f) {
  using clock=std::chrono::steady_clock;
  size_t count=0, sum=0; auto start=clock::now();
  do { for(int i=0;i<256;++i) sum+=f(); count+=256; } while(clock::now()-start<std::chrono::milliseconds(100));
  sink=sum; return std::chrono::duration<double,std::nano>(clock::now()-start).count()/static_cast<double>(count);
}
static void benchmark() {
#ifdef _WIN32
  SetProcessAffinityMask(GetCurrentProcess(),DWORD_PTR(1)<<4);
  SetPriorityClass(GetCurrentProcess(),ABOVE_NORMAL_PRIORITY_CLASS);
#endif
  std::puts("case,sample,ce_ns,stl_ns,ratio");
  std::u16string large(2048,u'한');
  for(int scenario=0;scenario<6;++scenario) {
    size_t n=scenario%3==0?5:scenario%3==1?64:2048;
    ce::string_view v(large.data(),n); std::u16string_view sv(large.data(),n);
    ce::string own(v); std::u16string standard(sv);
    auto custom=[&]() -> size_t { if(scenario<3) { ce::string t(v); return consume(t.data(),t.size()); } own.clear(); own.append(v); return consume(own.data(),own.size()); };
    auto stl=[&]() -> size_t { if(scenario<3) { std::u16string t(sv); return consume(t.data(),t.size()); } standard.clear(); standard.append(sv); return consume(standard.data(),standard.size()); };
    for(int i=0;i<5;++i) { measure(custom); measure(stl); }
    for(int i=0;i<15;++i) { double a,b; if(i%2) { b=measure(stl); a=measure(custom); } else { a=measure(custom); b=measure(stl); } std::printf("%s_%zu,%d,%.6f,%.6f,%.6f\n",scenario<3?"construct":"reuse_append",n,i,a,b,a/b); }
  }
}
int main(int argc,char**) { if(argc>1) benchmark(); else correctness(); }
