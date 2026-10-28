#include "../../tests/modules/prelude.h"
import Creator.NodeMapOptimized;
#include "pool_control.h"
static bool grouped=false,reverse_sides=false;static double batch_ns=30000000.0;
// Named module
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string_view>
#include <unordered_map>
#include <vector>

static volatile uint64_t result_sink;
template<class M>static double measure(int operation,size_t count,size_t rounds){
 std::vector<uint64_t> keys(count);
 uint64_t random=7919;for(auto& key:keys){random^=random<<13;random^=random>>7;random^=random<<17;key=random&~uint64_t{1};}
 M reused;if(operation>0 && operation<4){reused.reserve(count);for(auto key:keys)reused.emplace(key,key+7);}
 uint64_t result=0;
 const auto start=std::chrono::steady_clock::now();
 for(size_t round=0;round<rounds;++round){
  if(operation==4){M map;map.reserve(count);result+=map.bucket_count();} else if(operation==0){M map;map.reserve(count);for(auto key:keys)map.emplace(key,key+7);result+=map.size();}
  else if(operation==1){for(auto key:keys){auto found=reused.find(key);if(found!=reused.end())result+=found->second;}}
  else if(operation==2){for(auto key:keys)result+=reused.find(key|1)!=reused.end();}
  else {for(auto key:keys){result+=reused.erase(key);reused.emplace(key,key+7);}}
 }
 result_sink=result;
 return std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count()/double(rounds*count);
}
template<class M>static size_t calibrate(int operation,size_t size){
 size_t rounds=1;
 while(measure<M>(operation,size,rounds)*double(rounds*size)<batch_ns && rounds<(size_t{1}<<24))rounds*=2;
 return rounds;
}
template<class C>static int run(){
 using S=std::unordered_map<uint64_t,uint64_t>;
 bool accepted=true;
 for(size_t size:{size_t{64},size_t{4096},size_t{65536}})for(int operation=0;operation<5;++operation){
  auto cr=calibrate<C>(operation,size),sr=calibrate<S>(operation,size);
  std::printf("node_map_rounds,%zu,%d,%zu,%zu\n",size,operation,cr,sr);
  std::array<double,15> custom{},standard{};
  auto a=[&]{return measure<C>(operation,size,cr);};
  auto b=[&]{return measure<S>(operation,size,sr);};
  if(grouped){
    auto collect=[&](auto& work,auto& output){
      HEAP_OPTIMIZE_RESOURCES_INFORMATION info{HEAP_OPTIMIZE_RESOURCES_CURRENT_VERSION,0};
      if(!HeapSetInformation(GetProcessHeap(),HeapOptimizeResources,&info,sizeof(info)))std::abort();
      const auto start=std::chrono::steady_clock::now();
      do{work();}while(std::chrono::steady_clock::now()-start<std::chrono::seconds(1));
      for(size_t sample=0;sample<15;++sample)output[sample]=work();
    };
    if(reverse_sides){collect(b,standard);collect(a,custom);}else{collect(a,custom);collect(b,standard);}
  }else{
    for(size_t sample=0;sample<15;++sample){
      if((sample+(reverse_sides?1:0))%2){standard[sample]=b();custom[sample]=a();}
      else{custom[sample]=a();standard[sample]=b();}
    }
  }
  for(size_t sample=0;sample<15;++sample)std::printf("node_map_raw,%zu,%d,%zu,%.6f,%.6f\n",size,operation,sample,custom[sample],standard[sample]);
  std::sort(custom.begin(),custom.end());std::sort(standard.begin(),standard.end());
  double ratio=custom[7]/standard[7],cs=custom[13]/custom[1],ss=standard[13]/standard[1];
  bool pass=ratio<=1.10 && cs<=1.30 && ss<=1.30;accepted&=pass;
  std::printf("node_map_summary,%zu,%d,%.6f,%.6f,%.6f,%s\n",size,operation,ratio,cs,ss,pass?"PASS":"FAIL");std::fflush(stdout);
 }
 return accepted?0:2;
}
int main(int argc,char** argv){
 if(argc<2)return 3;std::string_view variant=argv[1];
 for(int i=2;i<argc;++i){std::string_view flag=argv[i];if(flag=="--grouped")grouped=true;else if(flag=="--reverse")reverse_sides=true;else return 3;}
 batch_ns=grouped?100000000.0:30000000.0;
 std::printf("optimization_variant,%.*s\n",int(variant.size()),variant.data());
 using namespace ce;
 if(variant=="baseline")return run<node_hash_map<uint64_t,uint64_t>>();
 if(variant=="pool")return run<node_hash_map<uint64_t,uint64_t,hash<uint64_t>,equal_to<uint64_t>,pool_control_allocator>>();
 if(variant=="index")return run<optimized_node_map<uint64_t,uint64_t,hash<uint64_t>,equal_to<uint64_t>,heap_allocator,false>>();
 if(variant=="combined")return run<optimized_node_map<uint64_t,uint64_t>>();
 return 3;
}