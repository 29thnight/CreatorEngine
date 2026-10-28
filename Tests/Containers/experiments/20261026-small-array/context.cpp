#define NOMINMAX
#include <Windows.h>
#include <Psapi.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include "context.h"
namespace {
PDH_HQUERY query{};
PDH_HCOUNTER counters[5]{};
PROCESS_MEMORY_COUNTERS_EX before{};
std::atomic<bool> stopping=false;
std::thread burner;
volatile uint64_t burn_sink=0;
void ensure(bool ok){if(!ok)std::abort();}
struct cleanup {~cleanup(){stopping=true;if(burner.joinable()){SetThreadPriority(burner.native_handle(),THREAD_PRIORITY_HIGHEST);burner.join();}if(query)PdhCloseQuery(query);}} finish;
bool initialized=false;
void init(){
 if(initialized)return;initialized=true;
 auto cpu=GetCurrentProcessorNumber();DWORD_PTR bit=DWORD_PTR(1)<<cpu,sibling=0,other=0;
 SYSTEM_LOGICAL_PROCESSOR_INFORMATION info[256];DWORD bytes=sizeof(info);
 ensure(GetLogicalProcessorInformation(info,&bytes)!=0);
 for(size_t i=0;i<bytes/sizeof(info[0]);++i)if(info[i].Relationship==RelationProcessorCore){
  auto mask=info[i].ProcessorMask;
  if(mask&bit)sibling=mask&~bit;
  else if(!other)other=mask&(~mask+1);
 }
 sibling=sibling&(~sibling+1);unsigned sibling_cpu=0;
 while(sibling && !(sibling&(DWORD_PTR(1)<<sibling_cpu)))++sibling_cpu;
 ensure(PdhOpenQueryW(nullptr,0,&query)==ERROR_SUCCESS);
 wchar_t paths[5][160]{};
 swprintf_s(paths[0],L"\\Processor(%u)\\%% Processor Time",sibling_cpu);
 swprintf_s(paths[1],L"\\Processor(%u)\\%% Processor Time",cpu);
 swprintf_s(paths[2],L"\\Processor Information(0,%u)\\%% Processor Performance",cpu);
 swprintf_s(paths[3],L"\\Processor(%u)\\%% DPC Time",cpu);
 swprintf_s(paths[4],L"\\Processor(%u)\\%% Interrupt Time",cpu);
 for(unsigned i=0;i<5;++i){auto status=PdhAddEnglishCounterW(query,paths[i],0,&counters[i]);std::printf("context_counter,%u,status,%lu\n",i,(unsigned long)status);if(status)counters[i]=nullptr;}
 char kind[16]{};GetEnvironmentVariableA("CE_ARRAY_BURN",kind,sizeof(kind));
 DWORD_PTR target=0;
 if(kind[0]=='1')target=sibling;
 if(kind[0]=='2')target=other;
 std::printf("context_topology,core,%u,sibling,%u,burn_mask,%llu\n",cpu,sibling_cpu,(unsigned long long)target);
 if(target)burner=std::thread([target]{
  ensure(SetThreadAffinityMask(GetCurrentThread(),target)!=0);
  uint64_t x=123;
  while(!stopping.load(std::memory_order_relaxed)){
   for(unsigned i=0;i<64;++i){x^=x<<13;x^=x>>7;x^=x<<17;}
   burn_sink=x;
  }
 });
}
double value(unsigned i){
 if(!counters[i])return -1;
 PDH_FMT_COUNTERVALUE v{};
 auto status=PdhGetFormattedCounterValue(counters[i],PDH_FMT_DOUBLE|PDH_FMT_NOCAP100,nullptr,&v);
 if(status || (v.CStatus!=PDH_CSTATUS_VALID_DATA && v.CStatus!=PDH_CSTATUS_NEW_DATA))return -1;
 return v.doubleValue;
}
}
void context_begin(){
 init();PdhCollectQueryData(query);
 before.cb=sizeof(before);ensure(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&before),sizeof(before))!=0);
}
array_context context_end(){
 PROCESS_MEMORY_COUNTERS_EX after{};after.cb=sizeof(after);
 ensure(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&after),sizeof(after))!=0);
 PdhCollectQueryData(query);
 array_context result;result.page_faults=after.PageFaultCount-before.PageFaultCount;
 result.working_set_delta=int64_t(after.WorkingSetSize)-int64_t(before.WorkingSetSize);
 result.private_delta=int64_t(after.PrivateUsage)-int64_t(before.PrivateUsage);
 result.sibling_busy=value(0);result.core_busy=value(1);result.performance=value(2);result.dpc=value(3);result.interrupt=value(4);return result;
}
void context_print(const char*name,size_t n,size_t sample,const char*side,const array_context&v){
 std::printf("context_diag,%s,%zu,%zu,%s,%lu,%lld,%lld,%.3f,%.3f,%.3f,%.3f,%.3f\n",name,n,sample,side,(unsigned long)v.page_faults,(long long)v.working_set_delta,(long long)v.private_delta,v.sibling_busy,v.core_busy,v.performance,v.dpc,v.interrupt);
}
