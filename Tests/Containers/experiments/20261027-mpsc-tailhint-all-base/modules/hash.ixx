module;
#include <new>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#if defined(_MSC_VER)
#include <intrin.h>
#endif
#include "module_config.h"

export module Creator.Containers:hash;
export import :string;

// CE_MODULE_BODY_BEGIN
export {
// CE_HEADER_INCLUDE #include "string.h"
namespace ce {
inline uint64_t mix_hash(uint64_t x) noexcept {
  x ^= x >> 30;
  x *= 0xbf58476d1ce4e5b9ULL;
  x ^= x >> 27;
  x *= 0x94d049bb133111ebULL;
  return x ^ (x >> 31);
}
inline uint64_t hash_bytes(utf8_string_view v) noexcept {
  uint64_t h = uint64_t(v.size()) ^ 0x9e3779b97f4a7c15ULL;
  auto p = v.data();
  auto n = v.size();
  while (n >= 16) {
    uint64_t a, b;
    memcpy(&a, p, 8);
    memcpy(&b, p + 8, 8);
    h = mix_hash(a ^ h) ^ mix_hash(b ^ 0x243f6a8885a308d3ULL);
    p += 16;
    n -= 16;
  }
  if (n >= 8) {
    uint64_t a;
    memcpy(&a, p, 8);
    h ^= mix_hash(a);
    p += 8;
    n -= 8;
  }
  uint64_t tail = 0;
  for (size_t i = 0; i < n; ++i)
    tail |= uint64_t(static_cast<unsigned char>(p[i])) << (i * 8);
  return mix_hash(h ^ tail);
}
struct identity_hash {
  uint64_t operator()(uint64_t v) const noexcept { return v; }
};
template <class T> struct hash {
  uint64_t operator()(const T &v) const noexcept {
    return mix_hash(static_cast<uint64_t>(v));
  }
};
template <class T> struct hash<T *> {
  uint64_t operator()(const T *p) const noexcept {
    return mix_hash(reinterpret_cast<uintptr_t>(p));
  }
};
template <> struct hash<float> {
  uint64_t operator()(float v) const noexcept {
    uint32_t bits = 0;
    if (v != 0)
      memcpy(&bits, &v, sizeof(bits));
    return mix_hash(bits);
  }
};
template <> struct hash<double> {
  uint64_t operator()(double v) const noexcept {
    uint64_t bits = 0;
    if (v != 0)
      memcpy(&bits, &v, sizeof(bits));
    return mix_hash(bits);
  }
};
struct string_hash {
  uint64_t operator()(utf8_string_view v) const noexcept { return hash_bytes(v); }
  template <class A>
  uint64_t operator()(const basic_utf8_string<A> &v) const noexcept {
    return hash_bytes(v.view());
  }
};
template <class A> struct hash<basic_utf8_string<A>> : string_hash {};
template <> struct hash<utf8_string_view> : string_hash {};
struct utf16_string_hash {
  uint64_t operator()(string_view v) const noexcept {
    return hash_bytes({reinterpret_cast<const char*>(v.data()),v.size()*sizeof(char16_t)});
  }
  template<class A> uint64_t operator()(const basic_utf16_string<A>& v) const noexcept {
    return operator()(v.view());
  }
};
template<class A> struct hash<basic_utf16_string<A>> : utf16_string_hash {};
template<> struct hash<string_view> : utf16_string_hash {};
template <class K> struct equal_to {
  bool operator()(const K &key, const K &query) const
      noexcept(noexcept(key == query)) {
    return key == query;
  }
};
template <class A> struct equal_to<basic_utf8_string<A>> {
  bool operator()(const basic_utf8_string<A> &key,
                  utf8_string_view query) const noexcept {
    return key.view() == query;
  }
};
template<class A> struct equal_to<basic_utf16_string<A>> {
  bool operator()(const basic_utf16_string<A>& key,string_view query) const noexcept {
    return key.view()==query;
  }
};
} // namespace ce
} // export
// CE_MODULE_BODY_END
