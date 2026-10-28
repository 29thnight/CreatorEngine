#include <ce/node_hash_map.h>
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
 M reused;if(operation){reused.reserve(count);for(auto key:keys)reused.emplace(key,key+7);}
 uint64_t result=0;
 const auto start=std::chrono::steady_clock::now();
 for(size_t round=0;round<rounds;++round){
  if(operation==0){M map;map.reserve(count);for(auto key:keys)map.emplace(key,key+7);result+=map.size();}
  else if(operation==1){for(auto key:keys){auto found=reused.find(key);if(found!=reused.end())result+=found->second;}}
  else if(operation==2){for(auto key:keys)result+=reused.find(key|1)!=reused.end();}
  else {for(auto key:keys){result+=reused.erase(key);reused.emplace(key,key+7);}}
 }
 result_sink=result;
 return std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count()/double(rounds*count);
}
template<class M>static size_t calibrate(int operation,size_t size){
 size_t rounds=1;
 while(measure<M>(operation,size,rounds)*double(rounds*size)<30000000.0 && rounds<(size_t{1}<<24))rounds*=2;
 return rounds;
}
template<class C>static int run(){
 using S=std::unordered_map<uint64_t,uint64_t>;
 bool accepted=true;
 for(size_t size:{size_t{64},size_t{4096},size_t{65536}})for(int operation=0;operation<4;++operation){
  auto cr=calibrate<C>(operation,size),sr=calibrate<S>(operation,size);
  std::array<double,15> custom{},standard{};
  for(size_t sample=0;sample<15;++sample){
   if(sample%2){standard[sample]=measure<S>(operation,size,sr);custom[sample]=measure<C>(operation,size,cr);}
   else{custom[sample]=measure<C>(operation,size,cr);standard[sample]=measure<S>(operation,size,sr);}
   std::printf("node_map_raw,%zu,%d,%zu,%.6f,%.6f\n",size,operation,sample,custom[sample],standard[sample]);
  }
  std::sort(custom.begin(),custom.end());std::sort(standard.begin(),standard.end());
  double ratio=custom[7]/standard[7],cs=custom[13]/custom[1],ss=standard[13]/standard[1];
  bool pass=ratio<=1.10 && cs<=1.30 && ss<=1.30;accepted&=pass;
  std::printf("node_map_summary,%zu,%d,%.6f,%.6f,%.6f,%s\n",size,operation,ratio,cs,ss,pass?"PASS":"FAIL");std::fflush(stdout);
 }
 return accepted?0:2;
}
int main(int argc,char** argv){
 const bool identity=argc>1 && std::string_view(argv[1])=="--identity-control";
 std::printf("node_map_control,%s\n",identity?"identity":"default");
 if(identity)return run<ce::node_hash_map<uint64_t,uint64_t,ce::identity_hash>>();
 return run<ce::node_hash_map<uint64_t,uint64_t>>();
}
