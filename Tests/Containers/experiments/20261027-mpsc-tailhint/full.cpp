#include "C:/Users/idene/source/repos/CreatorContainers/tests/modules/prelude.h"
import Creator.Containers;
#include "benchmark_support.h"
struct payload { uint64_t producer=0,sequence=0,words[14]{}; };
int main() {
 setup_benchmark(); ce::bounded_mpsc_queue<payload,64> q; payload v;
 for (unsigned i=0;i<64;++i) check(q.try_push(v));
 for (int r=0;r<18;++r) {
  auto t=clock_type::now(); unsigned n=0;
  for (unsigned i=0;i<25000;++i) n += q.try_push(v);
  auto e=clock_type::now(); check(n==0);
  if(r>=3) std::printf("full_latency,%d,%.2f\n",r-3,std::chrono::duration<double,std::nano>(e-t).count()/25000.0);
 }
}