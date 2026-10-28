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

export module Creator.Containers:string_atom;
export import :flat_hash_map;

// CE_MODULE_BODY_BEGIN
export {
// CE_HEADER_INCLUDE #include "flat_hash_map.h"
namespace ce {
template <class Allocator = heap_allocator> class basic_string_atom_table {
  struct blob {
    char *data;
    size_t length;
  };
  dynamic_array<blob, Allocator> strings_;
  basic_flat_hash_map<utf8_string_view, uint32_t, hash<utf8_string_view>,
                      equal_to<utf8_string_view>, Allocator>
      index_;
  uint64_t generation_ = 1;
  CE_NO_UNIQUE_ADDRESS Allocator alloc_;

public:
  struct atom {
    uint32_t index = UINT32_MAX;
    uint64_t generation = 0;
    const basic_string_atom_table *owner = nullptr;
    bool operator==(const atom &) const noexcept = default;
  };
  basic_string_atom_table() = default;
  explicit basic_string_atom_table(const Allocator &a)
      : strings_(a), index_(a), alloc_(a) {}
  basic_string_atom_table(const basic_string_atom_table &) = delete;
  basic_string_atom_table &operator=(const basic_string_atom_table &) = delete;
  ~basic_string_atom_table() { clear(); }
  atom intern(utf8_string_view s) {
    if (!generation_)
      throw invalid_handle{};
    if (auto p = index_.find(s))
      return {*p, generation_, this};
    if (strings_.size() >= UINT32_MAX || s.size() == SIZE_MAX)
      throw std::bad_alloc();
    auto copy = static_cast<char *>(alloc_.allocate(s.size() + 1, 1));
    if (s.size())
      memcpy(copy, s.data(), s.size());
    copy[s.size()] = 0;
    auto id = uint32_t(strings_.size());
    try {
      strings_.push_back(blob{copy, s.size()});
    } catch (...) {
      alloc_.deallocate(copy, s.size() + 1, 1);
      throw;
    }
    try {
      index_.try_emplace(utf8_string_view(copy, s.size()), id);
    } catch (...) {
      strings_.pop_back();
      alloc_.deallocate(copy, s.size() + 1, 1);
      throw;
    }
    return {id, generation_, this};
  }
  bool resolve(atom a, utf8_string_view &out) const noexcept {
    if (a.owner != this || a.generation != generation_ ||
        a.index >= strings_.size())
      return false;
    auto b = strings_[a.index];
    out = {b.data, b.length};
    return true;
  }
  utf8_string_view at(atom a) const {
    utf8_string_view out;
    if (!resolve(a, out))
      throw invalid_handle{};
    return out;
  }
  size_t size() const noexcept { return strings_.size(); }
  void clear() noexcept {
    index_.clear();
    for (auto b : strings_)
      alloc_.deallocate(b.data, b.length + 1, 1);
    strings_.clear();
    if (generation_)
      generation_ = generation_ == UINT64_MAX ? 0 : generation_ + 1;
  }
};
using string_atom_table = basic_string_atom_table<>;
using string_atom = string_atom_table::atom;
} // namespace ce
} // export
// CE_MODULE_BODY_END
