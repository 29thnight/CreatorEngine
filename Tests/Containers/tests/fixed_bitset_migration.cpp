#include <ce/fixed_bitset.h>
#include <bitset>
#include <cstdint>
#include <cstdio>
#include <stdexcept>

static void check(bool value){if(!value)throw std::runtime_error("fixed bitset regression");}
template<size_t N>static void compare(){
 ce::fixed_bitset<N> custom;std::bitset<N> standard;
 uint64_t random=371;
 for(size_t operation=0;operation<10000;++operation){
  random^=random<<13;random^=random>>7;random^=random<<17;
  const auto index=N?size_t(random%N):0;
  switch((random>>16)%9){
  case 0:if constexpr(N){custom.set(index);standard.set(index);}break;
  case 1:if constexpr(N){custom.reset(index);standard.reset(index);}break;
  case 2:if constexpr(N){custom.flip(index);standard.flip(index);}break;
  case 3:{auto shift=size_t(random%(N+66));custom<<=shift;standard<<=shift;break;}
  case 4:{auto shift=size_t(random%(N+66));custom>>=shift;standard>>=shift;break;}
  case 5:custom.flip();standard.flip();break;
  case 6:custom.set();standard.set();break;
  case 7:{ce::fixed_bitset<N> a(random);std::bitset<N> b(random);custom^=a;standard^=b;break;}
  default:if constexpr(N){custom[index]=!custom[index];standard[index]=!standard[index];}break;
  }
  check(custom.count()==standard.count() && custom.all()==standard.all() && custom.any()==standard.any() && custom.none()==standard.none());
  for(size_t bit=0;bit<N;++bit)check(custom[bit]==standard[bit]);
 }
 bool rejected=false;try{custom.test(N);}catch(const ce::bounds_error&){rejected=true;}check(rejected);
}
constexpr auto sample=[] {ce::fixed_bitset<65> value;value.set(64).set(0);return value<<1;}();
static_assert(sample.count()==1 && sample.test(1));
int main(){compare<0>();compare<1>();compare<8>();compare<63>();compare<64>();compare<65>();compare<127>();compare<128>();compare<257>();std::puts("FIXED_BITSET_CORRECTNESS_PASS");}
