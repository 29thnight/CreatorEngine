#pragma once
#include "../../CreatorEngine/Engine/Containers/modules/module_config.h"
#include <immintrin.h>
template<unsigned Blocks> CE_FORCEINLINE void copy2048_reverse_blocks(char* d,const char* s) noexcept {
 constexpr size_t k=(Blocks-1)*128;
 auto a=_mm256_loadu_si256(reinterpret_cast<const __m256i*>(s+k));
 auto b=_mm256_loadu_si256(reinterpret_cast<const __m256i*>(s+k+32));
 auto c=_mm256_loadu_si256(reinterpret_cast<const __m256i*>(s+k+64));
 auto e=_mm256_loadu_si256(reinterpret_cast<const __m256i*>(s+k+96));
 _mm256_store_si256(reinterpret_cast<__m256i*>(d+k),a);
 _mm256_store_si256(reinterpret_cast<__m256i*>(d+k+32),b);
 _mm256_store_si256(reinterpret_cast<__m256i*>(d+k+64),c);
 _mm256_store_si256(reinterpret_cast<__m256i*>(d+k+96),e);
 if constexpr(Blocks>1) copy2048_reverse_blocks<Blocks-1>(d,s);
}

CE_NOINLINE inline void copy2048_unrolled_aligned(char* d,const char* s) noexcept {
 if((reinterpret_cast<uintptr_t>(d)&31)==0) copy2048_reverse_blocks<16>(d,s);
 else {
  auto tail=_mm_loadu_si128(reinterpret_cast<const __m128i*>(s+2032));_mm_storeu_si128(reinterpret_cast<__m128i*>(d+2032),tail);
  auto a=_mm256_loadu_si256(reinterpret_cast<const __m256i*>(s+1936));
  auto b=_mm256_loadu_si256(reinterpret_cast<const __m256i*>(s+1968));
  auto c=_mm256_loadu_si256(reinterpret_cast<const __m256i*>(s+2000));
  _mm256_store_si256(reinterpret_cast<__m256i*>(d+1936),a);
  _mm256_store_si256(reinterpret_cast<__m256i*>(d+1968),b);
  _mm256_store_si256(reinterpret_cast<__m256i*>(d+2000),c);
  copy2048_reverse_blocks<15>(d+16,s+16);
  auto head=_mm_loadu_si128(reinterpret_cast<const __m128i*>(s));_mm_storeu_si128(reinterpret_cast<__m128i*>(d),head);
 }
 _mm256_zeroupper();
}
