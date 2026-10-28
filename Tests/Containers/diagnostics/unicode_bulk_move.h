#pragma once
#include <emmintrin.h>
// Diagnostic only: no production implementation references this file.
namespace ce_bulk_detail {
CE_NOINLINE inline void move_bytes(void* destination,const void* source,size_t n) noexcept {
 auto d=static_cast<unsigned char*>(destination); auto s=static_cast<const unsigned char*>(source);
 auto a=reinterpret_cast<uintptr_t>(d),b=reinterpret_cast<uintptr_t>(s);
 if((a>b?a-b:b-a)<n) { memmove(d,s,n); return; }
 while(n>=128) {
  auto x0=_mm_loadu_si128(reinterpret_cast<const __m128i*>(s)); auto x1=_mm_loadu_si128(reinterpret_cast<const __m128i*>(s+16));
  auto x2=_mm_loadu_si128(reinterpret_cast<const __m128i*>(s+32)); auto x3=_mm_loadu_si128(reinterpret_cast<const __m128i*>(s+48));
  _mm_storeu_si128(reinterpret_cast<__m128i*>(d),x0); _mm_storeu_si128(reinterpret_cast<__m128i*>(d+16),x1);
  _mm_storeu_si128(reinterpret_cast<__m128i*>(d+32),x2); _mm_storeu_si128(reinterpret_cast<__m128i*>(d+48),x3);
  x0=_mm_loadu_si128(reinterpret_cast<const __m128i*>(s+64)); x1=_mm_loadu_si128(reinterpret_cast<const __m128i*>(s+80));
  x2=_mm_loadu_si128(reinterpret_cast<const __m128i*>(s+96)); x3=_mm_loadu_si128(reinterpret_cast<const __m128i*>(s+112));
  _mm_storeu_si128(reinterpret_cast<__m128i*>(d+64),x0); _mm_storeu_si128(reinterpret_cast<__m128i*>(d+80),x1);
  _mm_storeu_si128(reinterpret_cast<__m128i*>(d+96),x2); _mm_storeu_si128(reinterpret_cast<__m128i*>(d+112),x3);
  d+=128; s+=128; n-=128;
 }
 if(n) memmove(d,s,n);
}
}
