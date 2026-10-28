#define NOMINMAX
#include <Windows.h>
#include <intrin.h>
#include <cstdio>
#include <cstdlib>
int main(int argc,char**argv){
 if(argc!=5)return 2;
 unsigned cpu=unsigned(std::strtoul(argv[1],nullptr,10));
 auto ttl=std::strtoull(argv[4],nullptr,10);
 if(cpu>=sizeof(DWORD_PTR)*8 || ttl>3600000 || !ttl)return 3;
 HANDLE ready=OpenEventA(EVENT_MODIFY_STATE,FALSE,argv[2]);
 HANDLE stop=OpenEventA(SYNCHRONIZE,FALSE,argv[3]);
 if(!ready||!stop)return 4;
 auto affinity=DWORD_PTR(1)<<cpu;
 if(!SetThreadAffinityMask(GetCurrentThread(),affinity) || !SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_ABOVE_NORMAL))return 5;
 std::printf("smt_guard,pid,%lu,cpu,%u,affinity,%llu,thread_priority,%d,pause_per_poll,4096,ttl_ms,%llu\n",GetCurrentProcessId(),cpu,(unsigned long long)affinity,GetThreadPriority(GetCurrentThread()),ttl);
 std::fflush(stdout);SetEvent(ready);
 auto start=GetTickCount64();
 while(WaitForSingleObject(stop,0)==WAIT_TIMEOUT && GetTickCount64()-start<ttl){
  for(unsigned i=0;i<4096;++i)_mm_pause();
 }
 std::puts("smt_guard,stopped");CloseHandle(stop);CloseHandle(ready);return 0;
}
