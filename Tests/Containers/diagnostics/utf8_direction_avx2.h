#pragma once
#include <immintrin.h>
CE_NOINLINE inline void copy_backwards_avx2(char* d,const char* s,size_t n) noexcept {
 while(n>=128) {
  n-=128;
  auto a=_mm256_loadu_si256(reinterpret_cast<const __m256i*>(s+n));
  auto b=_mm256_loadu_si256(reinterpret_cast<const __m256i*>(s+n+32));
  auto c=_mm256_loadu_si256(reinterpret_cast<const __m256i*>(s+n+64));
  auto e=_mm256_loadu_si256(reinterpret_cast<const __m256i*>(s+n+96));
  _mm256_storeu_si256(reinterpret_cast<__m256i*>(d+n),a);
  _mm256_storeu_si256(reinterpret_cast<__m256i*>(d+n+32),b);
  _mm256_storeu_si256(reinterpret_cast<__m256i*>(d+n+64),c);
  _mm256_storeu_si256(reinterpret_cast<__m256i*>(d+n+96),e);
 }
 if(n) memmove(d,s,n);
}
CE_NOINLINE inline void copy_forwards_avx2(char* d,const char* s,size_t n) noexcept {
 while(n>=128) {
  auto a=_mm256_loadu_si256(reinterpret_cast<const __m256i*>(s));
  auto b=_mm256_loadu_si256(reinterpret_cast<const __m256i*>(s+32));
  auto c=_mm256_loadu_si256(reinterpret_cast<const __m256i*>(s+64));
  auto e=_mm256_loadu_si256(reinterpret_cast<const __m256i*>(s+96));
  _mm256_storeu_si256(reinterpret_cast<__m256i*>(d),a);
  _mm256_storeu_si256(reinterpret_cast<__m256i*>(d+32),b);
  _mm256_storeu_si256(reinterpret_cast<__m256i*>(d+64),c);
  _mm256_storeu_si256(reinterpret_cast<__m256i*>(d+96),e);
  d+=128;s+=128;n-=128;
 }
 if(n) memmove(d,s,n);
}
