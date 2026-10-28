#pragma once
#include "../../CreatorEngine/Engine/Containers/modules/module_config.h"
#include <immintrin.h>
template<unsigned Blocks> CE_FORCEINLINE void copy2048_reverse_blocks(char* d,const char* s) noexcept {
 constexpr size_t k=(Blocks-1)*128;
 auto a=_mm256_loadu_si256(reinterpret_cast<const __m256i*>(s+k));
 auto b=_mm256_loadu_si256(reinterpret_cast<const __m256i*>(s+k+32));
 auto c=_mm256_loadu_si256(reinterpret_cast<const __m256i*>(s+k+64));
 auto e=_mm256_loadu_si256(reinterpret_cast<const __m256i*>(s+k+96));
 _mm256_storeu_si256(reinterpret_cast<__m256i*>(d+k),a);
 _mm256_storeu_si256(reinterpret_cast<__m256i*>(d+k+32),b);
 _mm256_storeu_si256(reinterpret_cast<__m256i*>(d+k+64),c);
 _mm256_storeu_si256(reinterpret_cast<__m256i*>(d+k+96),e);
 if constexpr(Blocks>1) copy2048_reverse_blocks<Blocks-1>(d,s);
}
CE_NOINLINE inline void copy2048_unrolled(char* d,const char* s) noexcept {
 copy2048_reverse_blocks<16>(d,s);_mm256_zeroupper();
}
