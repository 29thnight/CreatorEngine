#include "benchmark_support.h"
#include <ce/detail/atomic_u64.h>
#include <ce/string.h>
#include <atomic>
int main() {
  bench("u32_load_store", 1024, [] {
    ce::detail::atomic_u32 a; uint64_t sum=0;
    for(unsigned i=0;i<1024;++i){a.store(i);sum+=a.load();}
    return sum;
  }, [] {
    std::atomic<uint32_t> a{0}; uint64_t sum=0;
    for(unsigned i=0;i<1024;++i){a.store(i,std::memory_order_release);sum+=a.load(std::memory_order_acquire);}
    return sum;
  });
  bench("u64_fetch_load", 1024, [] {
    ce::detail::atomic_u64 a; uint64_t sum=0;
    for(unsigned i=0;i<1024;++i){sum+=a.fetch_add(1);sum+=a.load();}
    return sum;
  }, [] {
    std::atomic<uint64_t> a{0}; uint64_t sum=0;
    for(unsigned i=0;i<1024;++i){sum+=a.fetch_add(1);sum+=a.load();}
    return sum;
  });
  bench("needle_construct", 1024, [] {
    uint64_t sum=0;
    for(unsigned i=0;i<1024;++i){_ReadWriteBarrier();ce::utf8_string_view v("yx");sum+=v.size();}
    return sum;
  }, [] {
    uint64_t sum=0;
    for(unsigned i=0;i<1024;++i){_ReadWriteBarrier();std::string_view v("yx");sum+=v.size();}
    return sum;
  });
}
