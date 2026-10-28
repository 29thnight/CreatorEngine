module;
#include <stddef.h>
#include <stdint.h>
#include <new>
#include <initializer_list>
#include "module_config.h"
export module Creator.Containers:node_hash_map;
export import :dynamic_array;
export import :hash;

// CE_MODULE_BODY_BEGIN
export {
// CE_HEADER_INCLUDE #include "dynamic_array.h"
// CE_HEADER_INCLUDE #include "hash.h"
// CE_HEADER_INCLUDE #include <initializer_list>
namespace ce {
template<class Key,class Value> struct key_value {
  const Key first;
  Value second;
  template<class K,class... A> requires (__is_constructible(Key,K&&) && __is_constructible(Value,A&&...))
  key_value(K&& key,A&&... args)
    :first(static_cast<K&&>(key)),second(static_cast<A&&>(args)...){}
};

// Open-addressed index over individually owned nodes. Rehash never moves keys
// or values: references survive insertion just as in a node-based standard map.
template<class Key,class Value,class Hash=hash<Key>,class Equal=equal_to<Key>,class Allocator=heap_allocator>
class node_hash_map {
  static_assert(detail::nothrow_policy<Hash> && detail::nothrow_policy<Equal> && detail::nothrow_policy<Allocator>);
  static_assert(detail::nothrow_destroy<Key> && detail::nothrow_destroy<Value>);
public:
  using key_type=Key;using mapped_type=Value;using value_type=key_value<Key,Value>;
  using size_type=size_t;using hasher=Hash;using key_equal=Equal;using allocator_type=Allocator;
private:
  struct slot { value_type* node=nullptr;uint64_t hash=0; }; // null/0: empty; null/1: erased.
  CE_NO_UNIQUE_ADDRESS Allocator alloc_;
  CE_NO_UNIQUE_ADDRESS Hash hash_;
  CE_NO_UNIQUE_ADDRESS Equal equal_;
  dynamic_array<slot,Allocator> slots_;
  size_t size_=0,erased_=0;
  float max_load_=0.75f;
  static constexpr size_t absent=SIZE_MAX;
  size_t threshold(size_t capacity)const noexcept {
    const auto factor=max_load_<0.875f?max_load_:0.875f;
    const auto result=static_cast<size_t>(static_cast<double>(capacity)*factor);
    return result<capacity?result:capacity-1;
  }
  template<class Q> size_t locate(const Q& key,uint64_t code)const {
    if(slots_.empty())return absent;
    const auto mask=slots_.size()-1;
    auto index=size_t(code)&mask;
    for(size_t inspected=0;inspected<slots_.size();++inspected,index=(index+1)&mask){
      const auto& bucket=slots_[index];
      if(!bucket.node){if(!bucket.hash)return absent;}
      else if(bucket.hash==code && equal_(bucket.node->first,key))return index;
    }
    return absent;
  }
  void release(value_type* node)noexcept {
    node->~value_type();alloc_.deallocate(node,sizeof(value_type),alignof(value_type));
  }
  size_t prepared_capacity()const {
    if(size_>=max_size())throw std::bad_alloc{};
    size_t capacity=slots_.empty()?8:slots_.size();
    while(size_+1>threshold(capacity)){
      if(capacity>slots_.max_size()/2)throw std::bad_alloc{};
      capacity*=2;
    }
    if(capacity!=slots_.size() ||
       (erased_>capacity/8 && size_+erased_+1>=capacity-capacity/8))return capacity;
    return 0;
  }
  dynamic_array<slot,Allocator> prepare_slots(size_t capacity)const {
    dynamic_array<slot,Allocator> fresh(alloc_);fresh.resize(capacity,slot{});
    const auto mask=capacity-1;
    for(const auto& old:slots_)if(old.node){auto index=size_t(old.hash)&mask;while(fresh[index].node)index=(index+1)&mask;fresh[index]=old;}
    return fresh;
  }
  size_t insert_index(uint64_t code)const noexcept {
    const auto mask=slots_.size()-1;auto index=size_t(code)&mask;
    while(slots_[index].node)index=(index+1)&mask;
    return index;
  }
public:
  template<bool Constant> class basic_iterator {
    friend class node_hash_map;
    template<bool>friend class basic_iterator;
    using map_pointer=decltype([]{if constexpr(Constant)return static_cast<const node_hash_map*>(nullptr);else return static_cast<node_hash_map*>(nullptr);}());
    map_pointer owner_=nullptr;size_t index_=0;
    basic_iterator(map_pointer owner,size_t index):owner_(owner),index_(index){skip();}
    void skip(){while(owner_ && index_<owner_->slots_.size() && !owner_->slots_[index_].node)++index_;}
  public:
    using value_type=key_value<Key,Value>;using difference_type=ptrdiff_t;
    using pointer=decltype([]{if constexpr(Constant)return static_cast<const value_type*>(nullptr);else return static_cast<value_type*>(nullptr);}());
    using reference=decltype(*static_cast<pointer>(nullptr));
    basic_iterator()=default;
    template<bool C=Constant> requires(C)
    basic_iterator(const basic_iterator<false>& other):owner_(other.owner_),index_(other.index_){}
    reference operator*()const{return *owner_->slots_[index_].node;}
    pointer operator->()const{return owner_->slots_[index_].node;}
    basic_iterator& operator++(){++index_;skip();return *this;}
    basic_iterator operator++(int){auto before=*this;++*this;return before;}
    template<bool C> bool operator==(const basic_iterator<C>& other)const{return owner_==other.owner_ && index_==other.index_;}
  };
  using iterator=basic_iterator<false>;using const_iterator=basic_iterator<true>;
  struct insertion_result {iterator first;bool second;};
  node_hash_map():slots_(alloc_){}
  explicit node_hash_map(const Allocator& alloc):alloc_(alloc),slots_(alloc_){}
  explicit node_hash_map(size_t buckets,const Hash& hash=Hash{},const Equal& equal=Equal{},const Allocator& alloc=Allocator{})
    :alloc_(alloc),hash_(hash),equal_(equal),slots_(alloc_){if(buckets)rehash(buckets);}
  node_hash_map(std::initializer_list<value_type> values,size_t buckets=0,const Hash& hash=Hash{},const Equal& equal=Equal{},const Allocator& alloc=Allocator{})
    :node_hash_map(buckets,hash,equal,alloc){insert(values.begin(),values.end());}
  template<class It>requires requires(It i){*i;++i;i!=i;}
  node_hash_map(It first,It last,size_t buckets=0,const Hash& hash=Hash{},const Equal& equal=Equal{},const Allocator& alloc=Allocator{})
    :node_hash_map(buckets,hash,equal,alloc){insert(first,last);}
  node_hash_map(const node_hash_map& other) requires(detail::copyable<Key> && detail::copyable<Value>)
    :node_hash_map(0,other.hash_,other.equal_,other.alloc_){
    max_load_=other.max_load_;reserve(other.size_);insert(other.begin(),other.end());
  }
  node_hash_map(node_hash_map&& other)noexcept
    :alloc_(other.alloc_),hash_(other.hash_),equal_(other.equal_),slots_(static_cast<decltype(slots_)&&>(other.slots_)),
      size_(other.size_),erased_(other.erased_),max_load_(other.max_load_){other.size_=other.erased_=0;}
  ~node_hash_map(){clear();}
  node_hash_map& operator=(const node_hash_map& other) requires(detail::copyable<Key> && detail::copyable<Value>) {if(this!=&other){node_hash_map fresh(other);swap(fresh);}return *this;}
  node_hash_map& operator=(node_hash_map&& other)noexcept {if(this!=&other){node_hash_map fresh(static_cast<node_hash_map&&>(other));swap(fresh);}return *this;}
  node_hash_map& operator=(std::initializer_list<value_type> values){node_hash_map fresh(values,0,hash_,equal_,alloc_);swap(fresh);return *this;}
  void swap(node_hash_map& other)noexcept {
    auto a=alloc_;alloc_=other.alloc_;other.alloc_=a;
    auto h=hash_;hash_=other.hash_;other.hash_=h;
    auto e=equal_;equal_=other.equal_;other.equal_=e;
    slots_.swap(other.slots_);
    auto n=size_;size_=other.size_;other.size_=n;
    n=erased_;erased_=other.erased_;other.erased_=n;
    auto f=max_load_;max_load_=other.max_load_;other.max_load_=f;
  }
  void clear()noexcept {for(auto& bucket:slots_){if(bucket.node)release(bucket.node);bucket={};}size_=erased_=0;}
  bool empty()const noexcept{return size_==0;}
  size_t size()const noexcept{return size_;}
  size_t max_size()const noexcept{return SIZE_MAX/(sizeof(slot)*2);}
  size_t bucket_count()const noexcept{return slots_.size();}
  float load_factor()const noexcept{return slots_.empty()?0.f:static_cast<float>(size_)/static_cast<float>(slots_.size());}
  float max_load_factor()const noexcept{return max_load_;}
  void max_load_factor(float value){if(!(value>0))throw bounds_error{};max_load_=value;}
  Hash hash_function()const noexcept{return hash_;}
  Equal key_eq()const noexcept{return equal_;}
  Allocator get_allocator()const noexcept{return alloc_;}
  void reserve(size_t elements){
    size_t capacity=slots_.size()?slots_.size():8;
    if(elements>max_size())throw std::bad_alloc{};
    while(elements>threshold(capacity)){if(capacity>slots_.max_size()/2)throw std::bad_alloc{};capacity*=2;}
    if(capacity>slots_.size())rehash(capacity);
  }
  void rehash(size_t requested){
    size_t capacity=8;
    while(capacity<requested || size_>threshold(capacity)){if(capacity>slots_.max_size()/2)throw std::bad_alloc{};capacity*=2;}
    auto fresh=prepare_slots(capacity);
    slots_.swap(fresh);erased_=0;
  }
  iterator begin()noexcept{return {this,0};}iterator end()noexcept{return {this,slots_.size()};}
  const_iterator begin()const noexcept{return {this,0};}const_iterator end()const noexcept{return {this,slots_.size()};}
  const_iterator cbegin()const noexcept{return begin();}const_iterator cend()const noexcept{return end();}
  template<class Q> iterator find(const Q& key){const auto i=locate(key,hash_(key));return {this,i==absent?slots_.size():i};}
  template<class Q> const_iterator find(const Q& key)const{const auto i=locate(key,hash_(key));return {this,i==absent?slots_.size():i};}
  template<class Q> bool contains(const Q& key)const{return locate(key,hash_(key))!=absent;}
  template<class Q> size_t count(const Q& key)const{return contains(key)?1:0;}
  template<class Q> Value& at(const Q& key){auto i=find(key);if(i==end())throw bounds_error{};return i->second;}
  template<class Q> const Value& at(const Q& key)const{auto i=find(key);if(i==end())throw bounds_error{};return i->second;}
  template<class K,class... A> insertion_result try_emplace(K&& key,A&&... args){
    const auto code=hash_(key);auto existing=locate(key,code);
    if(existing!=absent)return {{this,existing},false};
    const auto capacity=prepared_capacity();
    dynamic_array<slot,Allocator> prepared(alloc_);
    if(capacity)prepared=prepare_slots(capacity);
    auto* memory=alloc_.allocate(sizeof(value_type),alignof(value_type));value_type* node;
    try{node=::new(memory)value_type(static_cast<K&&>(key),static_cast<A&&>(args)...);}
    catch(...){alloc_.deallocate(memory,sizeof(value_type),alignof(value_type));throw;}
    // Commit only after both index allocation and value construction succeed.
    if(capacity){slots_.swap(prepared);erased_=0;}
    const auto index=insert_index(code);auto& bucket=slots_[index];if(bucket.hash && !bucket.node)--erased_;
    bucket={node,code};++size_;return {{this,index},true};
  }
  template<class K,class V> insertion_result insert_or_assign(K&& key,V&& value){
    auto found=find(key);if(found!=end()){found->second=static_cast<V&&>(value);return {found,false};}
    return try_emplace(static_cast<K&&>(key),static_cast<V&&>(value));
  }
  Value& operator[](const Key& key){return try_emplace(key).first->second;}
  Value& operator[](Key&& key){return try_emplace(static_cast<Key&&>(key)).first->second;}
  insertion_result insert(const value_type& value){return try_emplace(value.first,value.second);}
  insertion_result insert(value_type&& value){return try_emplace(value.first,static_cast<Value&&>(value.second));}
  template<class P>requires requires(P p){p.first;p.second;}
  insertion_result insert(P&& value){return try_emplace(static_cast<P&&>(value).first,static_cast<P&&>(value).second);}
  template<class It>void insert(It first,It last){for(;first!=last;++first)insert(*first);}
  void insert(std::initializer_list<value_type> values){insert(values.begin(),values.end());}
  template<class K,class V> insertion_result emplace(K&& key,V&& value){return try_emplace(static_cast<K&&>(key),static_cast<V&&>(value));}
  template<class P> insertion_result emplace(P&& value){return insert(static_cast<P&&>(value));}
  iterator erase(const_iterator position){
    const auto index=position.index_;auto next=iterator(this,index);++next;
    auto& bucket=slots_[index];auto* node=bucket.node;bucket={nullptr,1};--size_;++erased_;release(node);return next;
  }
  iterator erase(iterator position){return erase(const_iterator(position));}
  iterator erase(const_iterator first,const_iterator last){while(first!=last)first=erase(first);return {this,last.index_};}
  template<class Q> size_t erase(const Q& key){auto found=find(key);if(found==end())return 0;erase(found);return 1;}
  bool operator==(const node_hash_map& other)const {
    if(size_!=other.size_)return false;
    for(const auto& item:*this){auto found=other.find(item.first);if(found==other.end() || !(item.second==found->second))return false;}
    return true;
  }
};
} // namespace ce
} // export
// CE_MODULE_BODY_END
