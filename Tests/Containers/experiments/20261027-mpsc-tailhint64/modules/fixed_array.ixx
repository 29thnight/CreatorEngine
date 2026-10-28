module;
#include <stddef.h>
#include <compare>
#include <utility>
export module Creator.Containers:fixed_array;
export import :memory;

// CE_MODULE_BODY_BEGIN
export {
// CE_HEADER_INCLUDE #include "memory.h"
// CE_HEADER_INCLUDE #include <compare>
// CE_HEADER_INCLUDE #include <utility>
namespace ce {
template<class T> struct array_value { using type=T; };
template<class T> struct array_value<const T> { using type=T; };
template<class T> struct array_value<volatile T> { using type=T; };
template<class T> struct array_value<const volatile T> { using type=T; };
namespace detail {
template<class T> constexpr auto array_compare(const T& left,const T& right) {
  const auto fallback=[&]{
    if(left<right)return std::weak_ordering::less;
    if(right<left)return std::weak_ordering::greater;
    return std::weak_ordering::equivalent;
  };
  if constexpr(requires{left<=>right;}) {
    using order=decltype(left<=>right);
    if constexpr(same_type<order,std::strong_ordering> || same_type<order,std::weak_ordering> || same_type<order,std::partial_ordering>)
      return left<=>right;
    else return fallback();
  } else return fallback();
}
}
template<class T> class reverse_pointer_iterator {
  T* base_ = nullptr;
public:
  using value_type = typename array_value<T>::type;
  using difference_type = ptrdiff_t;
  using pointer = T*;
  using reference = T&;
  constexpr reverse_pointer_iterator() = default;
  constexpr explicit reverse_pointer_iterator(T* p) : base_(p) {}
  template<class U> requires(detail::same_type<T,const U>)
  constexpr reverse_pointer_iterator(reverse_pointer_iterator<U> other) : base_(other.base()) {}
  constexpr T* base() const { return base_; }
  constexpr T& operator*() const { return base_[-1]; }
  constexpr T* operator->() const { return base_ - 1; }
  constexpr T& operator[](difference_type n) const { return *(*this+n); }
  constexpr reverse_pointer_iterator& operator++() { --base_; return *this; }
  constexpr reverse_pointer_iterator operator++(int) { auto old=*this; ++*this; return old; }
  constexpr reverse_pointer_iterator& operator--() { ++base_; return *this; }
  constexpr reverse_pointer_iterator operator--(int) { auto old=*this; --*this; return old; }
  constexpr reverse_pointer_iterator& operator+=(difference_type n) { if(n)base_-=n; return *this; }
  constexpr reverse_pointer_iterator& operator-=(difference_type n) { if(n)base_+=n; return *this; }
  friend constexpr reverse_pointer_iterator operator+(reverse_pointer_iterator it,difference_type n) { return it+=n; }
  friend constexpr reverse_pointer_iterator operator+(difference_type n,reverse_pointer_iterator it) { return it+=n; }
  friend constexpr reverse_pointer_iterator operator-(reverse_pointer_iterator it,difference_type n) { return it-=n; }
  friend constexpr difference_type operator-(reverse_pointer_iterator a,reverse_pointer_iterator b) { return a.base_==b.base_?0:b.base_-a.base_; }
  friend constexpr bool operator==(reverse_pointer_iterator a,reverse_pointer_iterator b) { return a.base_==b.base_; }
  friend constexpr bool operator<(reverse_pointer_iterator a,reverse_pointer_iterator b) { return a.base_>b.base_; }
  friend constexpr bool operator>(reverse_pointer_iterator a,reverse_pointer_iterator b) { return b<a; }
  friend constexpr bool operator<=(reverse_pointer_iterator a,reverse_pointer_iterator b) { return !(b<a); }
  friend constexpr bool operator>=(reverse_pointer_iterator a,reverse_pointer_iterator b) { return !(a<b); }
};

template<class T,size_t N> struct fixed_array {
  T values[N];
  using value_type=T; using size_type=size_t; using difference_type=ptrdiff_t;
  using reference=T&; using const_reference=const T&;
  using pointer=T*; using const_pointer=const T*;
  using iterator=T*; using const_iterator=const T*;
  using reverse_iterator=reverse_pointer_iterator<T>;
  using const_reverse_iterator=reverse_pointer_iterator<const T>;
  constexpr T* data() noexcept { return values; }
  constexpr const T* data() const noexcept { return values; }
  constexpr T* begin() noexcept { return data(); }
  constexpr const T* begin() const noexcept { return data(); }
  constexpr const T* cbegin() const noexcept { return data(); }
  constexpr T* end() noexcept { return data()+N; }
  constexpr const T* end() const noexcept { return data()+N; }
  constexpr const T* cend() const noexcept { return end(); }
  constexpr reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }
  constexpr const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }
  constexpr const_reverse_iterator crbegin() const noexcept { return rbegin(); }
  constexpr reverse_iterator rend() noexcept { return reverse_iterator(begin()); }
  constexpr const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }
  constexpr const_reverse_iterator crend() const noexcept { return rend(); }
  static constexpr size_t size() noexcept { return N; }
  static constexpr size_t max_size() noexcept { return N; }
  static constexpr bool empty() noexcept { return N==0; }
  constexpr T& operator[](size_t n) noexcept { return values[n]; }
  constexpr const T& operator[](size_t n) const noexcept { return values[n]; }
  constexpr T& at(size_t n) { if(n>=N)throw bounds_error{};return values[n]; }
  constexpr const T& at(size_t n) const { if(n>=N)throw bounds_error{};return values[n]; }
  constexpr T& front() noexcept { return values[0]; }
  constexpr const T& front() const noexcept { return values[0]; }
  constexpr T& back() noexcept { return values[N-1]; }
  constexpr const T& back() const noexcept { return values[N-1]; }
  constexpr void fill(const T& value) { for(auto& item:values)item=value; }
  constexpr void swap(fixed_array& other) noexcept(detail::nothrow_move<T> && __is_nothrow_assignable(T&,T&&)) {
    for(size_t i=0;i<N;++i){T temp(static_cast<T&&>(values[i]));values[i]=static_cast<T&&>(other[i]);other[i]=static_cast<T&&>(temp);}
  }
  constexpr bool operator==(const fixed_array& other) const {
    for(size_t i=0;i<N;++i)if(!(values[i]==other[i]))return false;
    return true;
  }
  constexpr auto operator<=>(const fixed_array& other) const requires requires(const T& a){a<a;} {
    using order=decltype(detail::array_compare(values[0],other[0]));
    for(size_t i=0;i<N;++i){auto compared=detail::array_compare(values[i],other[i]);if(compared!=0)return compared;}
    return order::equivalent;
  }
};
template<class T> struct fixed_array<T,0> {
  using value_type=T; using size_type=size_t; using difference_type=ptrdiff_t;
  using reference=T&; using const_reference=const T&; using pointer=T*; using const_pointer=const T*;
  using iterator=T*; using const_iterator=const T*;
  using reverse_iterator=reverse_pointer_iterator<T>;
  using const_reverse_iterator=reverse_pointer_iterator<const T>;
  constexpr T* data() noexcept { return nullptr; }
  constexpr const T* data() const noexcept { return nullptr; }
  constexpr T* begin() noexcept { return nullptr; }
  constexpr const T* begin() const noexcept { return nullptr; }
  constexpr const T* cbegin() const noexcept { return nullptr; }
  constexpr T* end() noexcept { return nullptr; }
  constexpr const T* end() const noexcept { return nullptr; }
  constexpr const T* cend() const noexcept { return nullptr; }
  constexpr reverse_iterator rbegin() noexcept { return reverse_iterator{}; }
  constexpr const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator{}; }
  constexpr const_reverse_iterator crbegin() const noexcept { return rbegin(); }
  constexpr reverse_iterator rend() noexcept { return reverse_iterator{}; }
  constexpr const_reverse_iterator rend() const noexcept { return const_reverse_iterator{}; }
  constexpr const_reverse_iterator crend() const noexcept { return rend(); }
  static constexpr size_t size() noexcept { return 0; }
  static constexpr size_t max_size() noexcept { return 0; }
  static constexpr bool empty() noexcept { return true; }
  constexpr T& operator[](size_t) noexcept { return *data(); }
  constexpr const T& operator[](size_t) const noexcept { return *data(); }
  constexpr T& at(size_t) { throw bounds_error{}; }
  constexpr const T& at(size_t) const { throw bounds_error{}; }
  constexpr T& front() noexcept { return *data(); }
  constexpr const T& front() const noexcept { return *data(); }
  constexpr T& back() noexcept { return *data(); }
  constexpr const T& back() const noexcept { return *data(); }
  constexpr void fill(const T&) noexcept {}
  constexpr void swap(fixed_array&) noexcept {}
  constexpr bool operator==(const fixed_array&) const noexcept { return true; }
  constexpr auto operator<=>(const fixed_array& other) const requires requires(const T& a){a<a;} {
    using order=decltype(detail::array_compare(*data(),*other.data()));return order::equivalent;
  }
};
template<class T,class... U> requires ((detail::same_type<T,U>) && ...)
fixed_array(T,U...)->fixed_array<T,1+sizeof...(U)>;

namespace detail {
template<bool Move,class T,size_t N,size_t... I>
constexpr auto to_fixed_array(T(&source)[N],std::index_sequence<I...>) {
  using value=typename array_value<T>::type;
  if constexpr(Move)return fixed_array<value,N>{{static_cast<T&&>(source[I])...}};
  else return fixed_array<value,N>{{source[I]...}};
}
}
template<class T,size_t N> constexpr auto to_array(T(&source)[N]) {
  return detail::to_fixed_array<false>(source,std::make_index_sequence<N>{});
}
template<class T,size_t N> constexpr auto to_array(T(&&source)[N]) {
  return detail::to_fixed_array<true>(source,std::make_index_sequence<N>{});
}
template<size_t I,class T,size_t N> constexpr T& get(fixed_array<T,N>& a) noexcept { static_assert(I<N);return a[I]; }
template<size_t I,class T,size_t N> constexpr const T& get(const fixed_array<T,N>& a) noexcept { static_assert(I<N);return a[I]; }
template<size_t I,class T,size_t N> constexpr T&& get(fixed_array<T,N>&& a) noexcept { static_assert(I<N);return static_cast<T&&>(a[I]); }
template<size_t I,class T,size_t N> constexpr const T&& get(const fixed_array<T,N>&& a) noexcept { static_assert(I<N);return static_cast<const T&&>(a[I]); }
} // namespace ce
namespace std {
// Make the primary protocol names visible to import-only Clang consumers.
using ::std::tuple_size;
using ::std::tuple_element;
template<class T,size_t N> struct tuple_size<ce::fixed_array<T,N>> : integral_constant<size_t,N> {};
template<size_t I,class T,size_t N> struct tuple_element<I,ce::fixed_array<T,N>> {
  static_assert(I<N);using type=T;
};
}
} // export
// CE_MODULE_BODY_END
