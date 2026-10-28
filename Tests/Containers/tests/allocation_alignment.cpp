#include <ce/memory.h>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
int main() {
  ce::heap_allocator allocator;
  for (size_t alignment :
       {size_t(8), size_t(16), size_t(32), size_t(64), size_t(128)}) {
    for (size_t n = 1; n < 257; ++n) {
      auto p = allocator.allocate(n, alignment);
      if (uintptr_t(p) % alignment)
        std::abort();
      memset(p, 0x5a, n);
      auto q = allocator.reallocate(p, n, n + 271, alignment);
      if (uintptr_t(q) % alignment)
        std::abort();
      for (size_t i = 0; i < n; ++i)
        if (static_cast<unsigned char *>(q)[i] != 0x5a)
          std::abort();
      allocator.deallocate(q, n + 271, alignment);
    }
  }
  for(size_t alignment:{size_t(1),size_t(8),size_t(16),size_t(32),size_t(64)}){
    void* pointer=nullptr;size_t previous=0;
    for(size_t bytes:{size_t(1),size_t(4095),size_t(4096),size_t(8193),size_t(32),size_t(65536),size_t(4096),size_t(4095)}){
      auto* next=static_cast<unsigned char*>(allocator.reallocate(pointer,previous,bytes,alignment));
      if(reinterpret_cast<uintptr_t>(next)%alignment)std::abort();
      if(bytes>=4096 && alignment<=16 && reinterpret_cast<uintptr_t>(next)%32)std::abort();
      for(size_t i=0;i<(previous<bytes?previous:bytes);++i)if(next[i]!=static_cast<unsigned char>(i*31))std::abort();
      for(size_t i=0;i<bytes;++i)next[i]=static_cast<unsigned char>(i*31);
      pointer=next;previous=bytes;
    }
    if(alignment<=16){
      bool failed=false;
      try{(void)allocator.reallocate(pointer,previous,SIZE_MAX,alignment);}catch(const std::bad_alloc&){failed=true;}
      if(!failed)std::abort();
    }
    for(size_t i=0;i<previous;++i)if(static_cast<unsigned char*>(pointer)[i]!=static_cast<unsigned char>(i*31))std::abort();
    if(allocator.reallocate(pointer,previous,0,alignment)!=nullptr)std::abort();
  }
  std::puts("ALLOCATION_ALIGNMENT_PASS");
}
