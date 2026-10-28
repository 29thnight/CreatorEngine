module;
#include <stddef.h>
#include <stdint.h>
export module Creator.Containers:fixed_bitset;
export import :fixed_array;

// CE_MODULE_BODY_BEGIN
export {
// CE_HEADER_INCLUDE #include "fixed_array.h"
namespace ce {
// No allocation and no shared state. The unused high bits are always zero.
template<size_t N> class fixed_bitset {
  static constexpr size_t words=N/64+(N%64!=0);
  fixed_array<uint64_t,words> storage_{};
  static constexpr uint64_t last_mask()noexcept {
    if constexpr(N%64)return (uint64_t{1}<<(N%64))-1;
    else return ~uint64_t{0};
  }
  constexpr void trim()noexcept {if constexpr(words)storage_[words-1]&=last_mask();}
  static constexpr void check(size_t position){if(position>=N)throw bounds_error{};}
public:
  class reference {
    uint64_t* word_;uint64_t mask_;
    friend class fixed_bitset;
    constexpr reference(uint64_t& word,size_t bit)noexcept:word_(&word),mask_(uint64_t{1}<<bit){}
  public:
    constexpr operator bool()const noexcept{return (*word_&mask_)!=0;}
    constexpr reference& operator=(bool value)noexcept{if(value)*word_|=mask_;else *word_&=~mask_;return *this;}
    constexpr reference& operator=(const reference& other)noexcept{return *this=bool(other);}
    constexpr bool operator~()const noexcept{return !bool(*this);}
    constexpr reference& flip()noexcept{*word_^=mask_;return *this;}
  };
  constexpr fixed_bitset()noexcept=default;
  constexpr fixed_bitset(unsigned long long value)noexcept {if constexpr(words){storage_[0]=value;trim();}}
  static constexpr size_t size()noexcept{return N;}
  constexpr bool operator[](size_t position)const noexcept{return (storage_[position/64]>>(position%64))&1;}
  constexpr reference operator[](size_t position)noexcept{return reference(storage_[position/64],position%64);}
  constexpr bool test(size_t position)const{if constexpr(N==0){throw bounds_error{};}else{check(position);return (*this)[position];}}
  constexpr fixed_bitset& set(size_t position,bool value=true){if constexpr(N==0){throw bounds_error{};}else{check(position);(*this)[position]=value;return *this;}}
  constexpr fixed_bitset& reset(size_t position){return set(position,false);}
  constexpr fixed_bitset& flip(size_t position){if constexpr(N==0){throw bounds_error{};}else{check(position);storage_[position/64]^=uint64_t{1}<<(position%64);return *this;}}
  constexpr fixed_bitset& set()noexcept{storage_.fill(~uint64_t{0});trim();return *this;}
  constexpr fixed_bitset& reset()noexcept{storage_.fill(0);return *this;}
  constexpr fixed_bitset& flip()noexcept{for(auto& word:storage_)word=~word;trim();return *this;}
  constexpr bool any()const noexcept{for(auto word:storage_)if(word)return true;return false;}
  constexpr bool none()const noexcept{return !any();}
  constexpr bool all()const noexcept{
    for(size_t i=0;i<words;++i)if(storage_[i]!=(i+1==words?last_mask():~uint64_t{0}))return false;
    return true;
  }
  constexpr size_t count()const noexcept{
    size_t result=0;
    for(uint64_t word:storage_){
      word-=(word>>1)&UINT64_C(0x5555555555555555);
      word=(word&UINT64_C(0x3333333333333333))+((word>>2)&UINT64_C(0x3333333333333333));
      word=(word+(word>>4))&UINT64_C(0x0f0f0f0f0f0f0f0f);
      result+=size_t((word*UINT64_C(0x0101010101010101))>>56);
    }
    return result;
  }
  constexpr fixed_bitset& operator&=(const fixed_bitset& other)noexcept{for(size_t i=0;i<words;++i)storage_[i]&=other.storage_[i];return *this;}
  constexpr fixed_bitset& operator|=(const fixed_bitset& other)noexcept{for(size_t i=0;i<words;++i)storage_[i]|=other.storage_[i];return *this;}
  constexpr fixed_bitset& operator^=(const fixed_bitset& other)noexcept{for(size_t i=0;i<words;++i)storage_[i]^=other.storage_[i];return *this;}
  constexpr fixed_bitset& operator<<=(size_t shift)noexcept{
    if(shift>=N)return reset();
    const auto whole=shift/64,part=shift%64;
    for(size_t i=words;i-- >0;){
      uint64_t word=i>=whole?storage_[i-whole]<<part:0;
      if(part && i>whole)word|=storage_[i-whole-1]>>(64-part);
      storage_[i]=word;
    }
    trim();return *this;
  }
  constexpr fixed_bitset& operator>>=(size_t shift)noexcept{
    if(shift>=N)return reset();
    const auto whole=shift/64,part=shift%64;
    for(size_t i=0;i<words;++i){
      uint64_t word=i+whole<words?storage_[i+whole]>>part:0;
      if(part && i+whole+1<words)word|=storage_[i+whole+1]<<(64-part);
      storage_[i]=word;
    }
    return *this;
  }
  constexpr fixed_bitset operator~()const noexcept{auto result=*this;return result.flip();}
  constexpr fixed_bitset operator<<(size_t n)const noexcept{auto result=*this;return result<<=n;}
  constexpr fixed_bitset operator>>(size_t n)const noexcept{auto result=*this;return result>>=n;}
  friend constexpr fixed_bitset operator&(fixed_bitset a,const fixed_bitset& b)noexcept{return a&=b;}
  friend constexpr fixed_bitset operator|(fixed_bitset a,const fixed_bitset& b)noexcept{return a|=b;}
  friend constexpr fixed_bitset operator^(fixed_bitset a,const fixed_bitset& b)noexcept{return a^=b;}
  constexpr bool operator==(const fixed_bitset&)const noexcept=default;
};
} // namespace ce
} // export
// CE_MODULE_BODY_END
