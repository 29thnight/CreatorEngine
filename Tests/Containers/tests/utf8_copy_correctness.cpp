#include <ce/string.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#if defined(_WIN32)
#define NOMINMAX
#include <Windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif
static void require(bool ok,const char* text){if(!ok){std::fprintf(stderr,"FAIL %s\n",text);std::exit(1);}}
struct aligned_region {
 char* p=static_cast<char*>(ce::heap_allocator{}.allocate(32768,4096));
 ~aligned_region(){ce::heap_allocator{}.deallocate(p,32768,4096);}
};
struct allocation_count {size_t allocated=0,freed=0;};
struct counted_allocator {
 allocation_count* state;
 void* allocate(size_t n,size_t a){++state->allocated;return ce::heap_allocator{}.allocate(n,a);}
 void deallocate(void* p,size_t n,size_t a)noexcept{++state->freed;ce::heap_allocator{}.deallocate(p,n,a);}
};
struct offset_allocator {
 void* allocate(size_t n,size_t){return static_cast<char*>(ce::heap_allocator{}.allocate(n+16,16))+8;}
 void deallocate(void* p,size_t n,size_t)noexcept{ce::heap_allocator{}.deallocate(static_cast<char*>(p)-8,n+16,16);}
};
static void pattern(char* p,size_t n,unsigned seed){for(size_t i=0;i<n;++i)p[i]=static_cast<char>((i*131+seed*17)&255);}
int main(){
 aligned_region source,destination;size_t selected=0,fallback=0;
 const bool supported=ce::detail::utf8_copy_avx2_supported();
 for(size_t phase=0;phase<32;++phase)for(size_t dstPhase:{size_t(0),size_t(16)})for(size_t delta:{size_t(0),size_t(32),size_t(48),size_t(80),size_t(240),size_t(256),size_t(512),size_t(2048),size_t(4048)}){
  char* s=source.p+4096+phase;char* d=destination.p+4096+dstPhase+delta;
  pattern(s,2048,unsigned(phase+delta));memset(destination.p,0xa5,32768);
  bool used=ce::detail::try_utf8_copy_2048(d,s,2048);
  require(used==(supported && ce::detail::utf8_copy_2048_candidate(d,s)),"dispatch decision");
  if(!used)memmove(d,s,2048);
  require(memcmp(d,s,2048)==0,"all source/destination phases");
  for(int i=1;i<=16;++i)require(static_cast<unsigned char>(d[-i])==0xa5 && static_cast<unsigned char>(d[2048+i-1])==0xa5,"copy boundaries");
  if(used)++selected;else ++fallback;
  memset(d,0x5a,2048);require(!ce::detail::try_utf8_copy_2048<false>(d,s,2048),"forced fallback dispatch");
  ce::detail::utf8_copy_bytes<false>(d,s,2048);require(memcmp(d,s,2048)==0,"forced fallback content");
 }
 if(supported)require(selected>0,"hardware fast path exercised");
 for(size_t n:{size_t(0),size_t(1),size_t(31),size_t(32),size_t(127),size_t(128),size_t(1023),size_t(2047),size_t(2049),size_t(4096)}){
  auto s=source.p+4096;auto d=destination.p+4144;pattern(s,n,9);memset(d,0xa5,n+16);
  require(!ce::detail::try_utf8_copy_2048(d,s,n),"other lengths retain fallback");
  ce::detail::utf8_copy_bytes(d,s,n);require(!n||memcmp(d,s,n)==0,"fallback lengths");
  for(size_t i=0;i<16;++i)require(static_cast<unsigned char>(d[n+i])==0xa5,"fallback suffix");
 }
 std::array<char,16384> actual{},expected{};
 for(int shift:{-2048,-2047,-128,-32,-16,-1,0,1,16,32,128,2047,2048}){
  pattern(actual.data(),actual.size(),unsigned(shift+2048));expected=actual;
  char* a=actual.data()+4096;char* e=expected.data()+4096;
  if(shift>-2048&&shift<2048)require(!ce::detail::utf8_copy_2048_candidate(a+shift,a),"actual overlap rejects vector kernel");
  memmove(e+shift,e,2048);ce::detail::utf8_copy_bytes(a+shift,a,2048);require(actual==expected,"overlap both directions and identity");
 }
 allocation_count count;counted_allocator policy{&count};
 {ce::basic_utf8_string<counted_allocator> text(policy);text.reserve(4096);auto start=count.allocated;
  size_t low=(reinterpret_cast<uintptr_t>(text.data())-48)&4095;auto s=source.p+4096+low;pattern(s,2048,11);
  for(int i=0;i<10000;++i){text.clear();text.append({s,2048});}
  require(count.allocated==start&&text.size()==2048&&memcmp(text.data(),s,2048)==0&&text.c_str()[2048]==0,"append reuses ownership without allocations");
 }
 require(count.allocated==count.freed,"allocator lifetime balance");
 {ce::basic_utf8_string<offset_allocator> text;text.reserve(2048);auto s=source.p+4096;pattern(s,2048,4);
  require((reinterpret_cast<uintptr_t>(text.data())&15)==8,"custom 8-byte alignment fixture");
  text.append({s,2048});require(memcmp(text.data(),s,2048)==0&&text.c_str()[2048]==0,"unaligned policy fallback");}
 {ce::utf8_string text(ce::utf8_string_view(source.p+4096,2048));auto copy=text;text.append(text.view());require(text.size()==4096&&memcmp(text.data(),copy.data(),2048)==0&&memcmp(text.data()+2048,copy.data(),2048)==0,"self growth");}
#if defined(_WIN32)
 SYSTEM_INFO sys{};GetSystemInfo(&sys);size_t page=sys.dwPageSize;
 auto a=static_cast<char*>(VirtualAlloc(nullptr,page*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));auto b=static_cast<char*>(VirtualAlloc(nullptr,page*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));require(a&&b,"guard allocation");DWORD old{};require(VirtualProtect(a+page,page,PAGE_NOACCESS,&old)&&VirtualProtect(b+page,page,PAGE_NOACCESS,&old),"guard protection");
#else
 size_t page=static_cast<size_t>(sysconf(_SC_PAGESIZE));
 auto a=static_cast<char*>(mmap(nullptr,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));auto b=static_cast<char*>(mmap(nullptr,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));require(a!=MAP_FAILED&&b!=MAP_FAILED,"guard allocation");require(mprotect(a+page,page,PROT_NONE)==0&&mprotect(b+page,page,PROT_NONE)==0,"guard protection");
#endif
 for(size_t i=0;i<32;++i)for(size_t dp:{size_t(0),size_t(16)}){auto s=a+page-2048-i;auto d=b+page-2048-dp;pattern(s,2048,unsigned(i));ce::detail::utf8_copy_bytes(d,s,2048);require(memcmp(d,s,2048)==0,"protected page exact bounds");}
 size_t short_cases=0;
 for(size_t bytes=1;bytes<=64;++bytes){
  for(size_t prior=0;prior<24;++prior)for(size_t offset=0;offset+bytes<=128;++offset){
   ce::utf8_string text; text.resize(128,'x');text.reserve(256);
   for(size_t i=0;i<128;++i)text[i]=char(i+1);
   text.resize(prior);std::string short_expected(text.data(),prior), snapshot(text.data()+offset,bytes);
   short_expected.append(snapshot);text.append({text.data()+offset,bytes});
   require(text.size()==short_expected.size()&&memcmp(text.data(),short_expected.data(),short_expected.size())==0&&text.c_str()[text.size()]==0,"short append overlap snapshot");++short_cases;
  }
  ce::utf8_string text; text.append({a+page-bytes,bytes});
  require(text.size()==bytes&&memcmp(text.data(),a+page-bytes,bytes)==0,"short append protected page");++short_cases;
 }
 std::printf("UTF8_SHORT_APPEND_PASS cases=%zu lengths=1..64 overlap_and_guard_pages=1\n",short_cases);
#if defined(_WIN32)
 VirtualFree(a,0,MEM_RELEASE);VirtualFree(b,0,MEM_RELEASE);
#else
 munmap(a,page*2);munmap(b,page*2);
#endif
 std::printf("UTF8_COPY_CORRECTNESS_PASS avx2=%d selected=%zu fallback=%zu phases=576 guard_pages=64 no_growth_allocations=0\n",int(supported),selected,fallback);
}
