#include <vector>
#include <string>
#include <chrono>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <Windows.h>
import Creator.Containers;
using clock_type=std::chrono::steady_clock;
static volatile uint64_t sink;
__declspec(noinline) static uint64_t consume(const char* p,size_t n){return (unsigned char)p[n/2]+n;}
template<class F> double measure(F f){uint64_t total=0,n=0;auto start=clock_type::now();do{for(int i=0;i<64;++i)total+=f();n+=64;}while(clock_type::now()-start<std::chrono::milliseconds(100));sink=total;return std::chrono::duration<double,std::nano>(clock_type::now()-start).count()/n;}
template<class A,class B> void compare(const char* name,size_t n,A a,B b,bool reverse){std::array<double,15>x{},y{};auto collect=[](auto f,auto& out){auto t=clock_type::now();do{measure(f);}while(clock_type::now()-t<std::chrono::seconds(1));for(auto&v:out)v=measure(f);};if(reverse){collect(b,y);collect(a,x);}else{collect(a,x);collect(b,y);}for(int i=0;i<15;++i)std::printf("%s,%zu,%d,%.6f,%.6f\n",name,n,i,x[i],y[i]);fflush(stdout);}
int main(int argc,char**){if(!SetProcessAffinityMask(GetCurrentProcess(),DWORD_PTR(1)<<4)||!SetPriorityClass(GetCurrentProcess(),ABOVE_NORMAL_PRIORITY_CLASS))return 2;bool rev=argc>1;std::puts("case,n,sample,ce_ns,stl_ns");std::string src(8,'x');ce::utf8_string text({src.data(),src.size()});compare("string_copy",8,[&]{ce::utf8_string a(text);return consume(a.data(),a.size());},[&]{std::string a(src);return consume(a.data(),a.size());},rev);compare("string_view",8,[&]{ce::utf8_string a({src.data(),src.size()});return consume(a.data(),a.size());},[&]{std::string a(src.data(),src.size());return consume(a.data(),a.size());},rev);for(size_t n:{size_t(64),size_t(4096),size_t(65536)}){std::vector<uint64_t> keys(n);for(size_t i=0;i<n;++i)keys[i]=i*31;compare("reserve_destroy",n,[&]{ce::dynamic_array<uint64_t>a;a.reserve(n);return a.capacity();},[&]{std::vector<uint64_t>a;a.reserve(n);return a.capacity();},rev);ce::dynamic_array<uint64_t>a;a.reserve(n);std::vector<uint64_t>b;b.reserve(n);compare("clear_fill",n,[&]{a.clear();for(auto v:keys)a.push_back(v);return a[n/2];},[&]{b.clear();for(auto v:keys)b.push_back(v);return b[n/2];},rev);compare("sum",n,[&]{uint64_t s=0;for(size_t i=0;i<n;++i)s+=a[i];return s;},[&]{uint64_t s=0;for(auto v:b)s+=v;return s;},rev);}}
