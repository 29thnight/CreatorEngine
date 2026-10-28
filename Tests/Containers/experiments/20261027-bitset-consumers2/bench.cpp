#include <bitset>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <Windows.h>
import Creator.Containers;
using clock_type=std::chrono::steady_clock;
static volatile uint64_t sink;
template<class B> size_t seen8(uint64_t seed) { B b;size_t n=0;for(size_t i=0;i<8;++i){auto p=(seed>>(i*3))&7;n+=b.test(p);b.set(p);}return n+b.test(seed&7); }
template<class B> size_t count64(uint64_t seed) { B b(seed);return b.count(); }
template<class B> size_t slots65536(uint64_t seed) { B b;size_t n=0;for(size_t i=0;i<32;++i)b.set((seed+i*997)&65535);for(size_t i=0;i<32;++i)n+=b.test((seed+i*997)&65535);return n; }
template<class F> double measure(F f) {uint64_t state=123,total=0,n=0;const auto begin=clock_type::now();do{for(int i=0;i<256;++i){state=state*6364136223846793005ULL+1;total+=f(state);}n+=256;}while(clock_type::now()-begin<std::chrono::milliseconds(100));sink=total;return std::chrono::duration<double,std::nano>(clock_type::now()-begin).count()/double(n);}
template<class A,class B> void compare(const char* name,A a,B b,bool reverse){std::array<double,15>x{},y{};auto collect=[](auto f,auto& out){auto t=clock_type::now();do{measure(f);}while(clock_type::now()-t<std::chrono::seconds(1));for(auto& v:out)v=measure(f);};if(reverse){collect(b,y);collect(a,x);}else{collect(a,x);collect(b,y);}for(int i=0;i<15;++i)std::printf("%s,%d,%.9f,%.9f\n",name,i,x[i],y[i]);std::fflush(stdout);}
int main(int argc,char**){if(!SetProcessAffinityMask(GetCurrentProcess(),DWORD_PTR(1)<<4)||!SetPriorityClass(GetCurrentProcess(),ABOVE_NORMAL_PRIORITY_CLASS))return 2;std::puts("case,sample,ce_ns,stl_ns");bool rev=argc>1;compare("seen8",seen8<ce::fixed_bitset<8>>,seen8<std::bitset<8>>,rev);compare("count64",count64<ce::fixed_bitset<64>>,count64<std::bitset<64>>,rev);compare("slots65536",slots65536<ce::fixed_bitset<65536>>,slots65536<std::bitset<65536>>,rev);}
