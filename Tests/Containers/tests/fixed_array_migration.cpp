#include <ce/all.h>
#include <array>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <type_traits>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <time.h>
#endif

static volatile uint64_t sink;
static double last_cpu_ns;
static double last_cycles;
static uint64_t thread_cycles(){
#if defined(_WIN32)
 ULONG64 cycles=0;if(!QueryThreadCycleTime(GetCurrentThread(),&cycles))std::abort();return cycles;
#else
 return 0;
#endif
}
static double thread_cpu_ns(){
#if defined(_WIN32)
 FILETIME created,exited,kernel,user;
 if(!GetThreadTimes(GetCurrentThread(),&created,&exited,&kernel,&user))std::abort();
 auto ticks=[](FILETIME value){return (uint64_t(value.dwHighDateTime)<<32)|value.dwLowDateTime;};
 return double(ticks(kernel)+ticks(user))*100.;
#else
 timespec value{};if(clock_gettime(CLOCK_THREAD_CPUTIME_ID,&value))std::abort();
 return double(value.tv_sec)*1e9+double(value.tv_nsec);
#endif
}
#if defined(_MSC_VER)
__declspec(noinline)
#else
__attribute__((noinline))
#endif
static uint64_t consume(const uint64_t* p,size_t count) {
 uint64_t result=0;for(size_t i=0;i<count;++i)result+=p[i];return result;
}
template<class A> static double measure(int operation,size_t rounds) {
 A input{},output{};
 for(size_t i=0;i<input.size();++i)input[i]=i*17+3;
 uint64_t sum=0;
 const auto cpu_start=thread_cpu_ns();
 const auto cycles_start=thread_cycles();
 auto start=std::chrono::steady_clock::now();
 for(size_t round=0;round<rounds;++round){
  input[0]=round;
  if(operation==0)output=input;
  else if(operation==1)output.fill(round);
  else std::transform(input.begin(),input.end(),output.begin(),[](auto x){return x*3+1;});
  sum+=consume(output.data(),output.size());
 }
 const auto elapsed=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count();
 last_cycles=double(thread_cycles()-cycles_start)/double(rounds);
 last_cpu_ns=(thread_cpu_ns()-cpu_start)/double(rounds);
 sink=sum;
 return elapsed/double(rounds);
}
template<class A> static size_t calibrate(int op){
#if defined(_WIN32)
 char buffer[32]{};const auto length=GetEnvironmentVariableA("CE_FIXED_BATCH_NS",buffer,sizeof(buffer));
 if(length>=sizeof(buffer))std::abort();const char* setting=length?buffer:nullptr;
#else
 const auto* setting=std::getenv("CE_FIXED_BATCH_NS");
#endif
 const double batch=setting?std::strtod(setting,nullptr):30000000.0;
 size_t rounds=64;while(measure<A>(op,rounds)*double(rounds)<batch && rounds<(size_t{1}<<27))rounds*=2;
 return rounds;
}
template<size_t N> static bool run(){
 using C=ce::fixed_array<uint64_t,N>;using S=std::array<uint64_t,N>;
 static_assert(sizeof(C)==sizeof(S));static_assert(alignof(C)==alignof(S));
 static_assert(std::is_trivially_copyable_v<C> && std::is_aggregate_v<C>);
 bool passed=true;
 for(int op=0;op<3;++op){
  auto cr=calibrate<C>(op),sr=calibrate<S>(op);
  std::array<double,15> custom{},standard{},custom_cpu{},standard_cpu{},custom_cycles{},standard_cycles{};
  for(size_t sample=0;sample<15;++sample){
   if(sample%2){standard[sample]=measure<S>(op,sr);standard_cpu[sample]=last_cpu_ns;standard_cycles[sample]=last_cycles;custom[sample]=measure<C>(op,cr);custom_cpu[sample]=last_cpu_ns;custom_cycles[sample]=last_cycles;}
   else{custom[sample]=measure<C>(op,cr);custom_cpu[sample]=last_cpu_ns;custom_cycles[sample]=last_cycles;standard[sample]=measure<S>(op,sr);standard_cpu[sample]=last_cpu_ns;standard_cycles[sample]=last_cycles;}
   std::printf("fixed_raw,%zu,%d,%zu,%.6f,%.6f\n",N,op,sample,custom[sample],standard[sample]);
   std::printf("fixed_cpu_raw,%zu,%d,%zu,%.6f,%.6f,%zu,%zu,%.6f,%.6f\n",N,op,sample,custom_cpu[sample],standard_cpu[sample],cr,sr,custom_cycles[sample],standard_cycles[sample]);
  }
  std::sort(custom.begin(),custom.end());std::sort(standard.begin(),standard.end());
  double ratio=custom[7]/standard[7],cs=custom[13]/custom[1],ss=standard[13]/standard[1];
  bool pass=ratio<=1.10 && cs<=1.30 && ss<=1.30;passed&=pass;
  std::printf("fixed_summary,%zu,%d,%.6f,%.6f,%.6f,%s\n",N,op,ratio,cs,ss,pass?"PASS":"FAIL");
 }
 return passed;
}
int main(){bool pass=run<8>();pass=run<64>()&&pass;pass=run<4096>()&&pass;return pass?0:2;}
