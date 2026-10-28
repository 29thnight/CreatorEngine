#pragma once
#include <emmintrin.h>
CE_NOINLINE inline void copy_backwards(char* d,const char* s,size_t n) noexcept {
 while(n>=64) {
  n-=64;
  auto a=_mm_loadu_si128(reinterpret_cast<const __m128i*>(s+n));
  auto b=_mm_loadu_si128(reinterpret_cast<const __m128i*>(s+n+16));
  auto c=_mm_loadu_si128(reinterpret_cast<const __m128i*>(s+n+32));
  auto e=_mm_loadu_si128(reinterpret_cast<const __m128i*>(s+n+48));
  _mm_storeu_si128(reinterpret_cast<__m128i*>(d+n),a);
  _mm_storeu_si128(reinterpret_cast<__m128i*>(d+n+16),b);
  _mm_storeu_si128(reinterpret_cast<__m128i*>(d+n+32),c);
  _mm_storeu_si128(reinterpret_cast<__m128i*>(d+n+48),e);
 }
 if(n) memmove(d,s,n);
}
CE_NOINLINE inline void copy_forwards(char* d,const char* s,size_t n) noexcept {
 while(n>=64) {
  auto a=_mm_loadu_si128(reinterpret_cast<const __m128i*>(s));
  auto b=_mm_loadu_si128(reinterpret_cast<const __m128i*>(s+16));
  auto c=_mm_loadu_si128(reinterpret_cast<const __m128i*>(s+32));
  auto e=_mm_loadu_si128(reinterpret_cast<const __m128i*>(s+48));
  _mm_storeu_si128(reinterpret_cast<__m128i*>(d),a);
  _mm_storeu_si128(reinterpret_cast<__m128i*>(d+16),b);
  _mm_storeu_si128(reinterpret_cast<__m128i*>(d+32),c);
  _mm_storeu_si128(reinterpret_cast<__m128i*>(d+48),e);
  d+=64;s+=64;n-=64;
 }
 if(n) memmove(d,s,n);
}
